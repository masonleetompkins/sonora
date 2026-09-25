#include "AudioEngine.h"
#include "AudioTakes.h"
#include "Export.h"
#include "Fx.h"
#include "KitSamples.h"
#include "PitchCorrect.h"
#include "ProjectIO.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

// Rewrites a current-version document into an older version's shape so
// migration paths decode realistic legacy files (not just relabeled v6).
inline juce::var downshapeToVersion(juce::var document, int version)
{
    auto* root = document.getDynamicObject();
    auto* tracks = root->getProperty("tracks").getArray();
    auto* melody = tracks->getReference(0).getDynamicObject();
    auto* drums = tracks->getReference(1).getDynamicObject();
    if (version <= 5)
    {
        melody->setProperty("notes", melody->getProperty("patterns").getArray()->getReference(0));
        melody->removeProperty("patterns");
        drums->setProperty("steps", drums->getProperty("grids").getArray()->getReference(0));
        drums->removeProperty("grids");
        root->getProperty("song").getDynamicObject()->removeProperty("melodyPatterns");
        root->getProperty("song").getDynamicObject()->removeProperty("drumPatterns");
    }
    if (version <= 4)
        root->removeProperty("takes");
    if (version <= 3)
    {
        root->removeProperty("master");
        melody->removeProperty("fx");
        drums->removeProperty("fx");
    }
    if (version <= 2)
    {
        root->removeProperty("song");
        root->removeProperty("songMode");
    }
    root->setProperty("version", version);
    return document;
}

sonora::ProjectState fixture()
{

    sonora::ProjectState project;
    project.bpm = 123.0;
    project.melodies[0].count = 4;
    project.melodies[0].notes[0] = { 1, 0, 960, 60, 100 };
    project.melodies[0].notes[1] = { 2, 960, 240, 60, 90 };
    project.melodies[0].notes[2] = { 3, 240, 4320, 67, 110 };
    project.melodies[0].notes[3] = { 4, sonora::patternTicks - 240, 240, 60, 100 };
    for (int step = 0; step < sonora::gridSteps; step += 2)
        project.drumPatterns[0].steps[2][static_cast<std::size_t>(step)] = step % 4 == 0 ? 100 : 70;
    for (int step : { 0, 16, 32, 48 })
        project.drumPatterns[0].steps[0][static_cast<std::size_t>(step)] = 110;
    for (int step : { 4, 12, 20, 28, 36, 44, 52, 60 })
        project.drumPatterns[0].steps[1][static_cast<std::size_t>(step)] = 100;
    return project;
}

struct Event
{
    std::int64_t sample;
    int pitch;
    bool on;
    bool operator==(const Event&) const = default;
};

std::vector<Event> schedule(int blockSize, double sampleRate, double bpm, std::int64_t total)
{
    sonora::LoopScheduler scheduler;
    scheduler.configure(sampleRate, bpm);
    const auto project = fixture();
    std::vector<Event> events;
    for (std::int64_t start = 0; start < total;)
    {
        const auto count = static_cast<int>(std::min<std::int64_t>(blockSize, total - start));
        scheduler.scheduleDrums(project.drumPatterns[0], count, [&](int pad, std::uint8_t, int offset) {
            require(offset >= 0 && offset < count, "drum event outside callback");
            events.push_back({ start + offset, sonora::drumBaseNote + pad, true });
        });
        scheduler.process(project.melodies[0], count, false, [&](const sonora::Note& note, bool on, int offset) {
            require(offset >= 0 && offset < count, "event outside callback");
            events.push_back({ start + offset, note.pitch, on });
        });
        start += count;
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return std::tie(a.sample, a.on, a.pitch) < std::tie(b.sample, b.on, b.pitch);
    });
    return events;
}

void testTiming()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto bpm : { 40.0, 123.0, 240.0 })
        {
            sonora::LoopScheduler scheduler;
            scheduler.configure(rate, bpm);
            const auto loop = scheduler.lengthInSamples();
            const auto total = loop * 3 + 13;
            const auto reference = schedule(127, rate, bpm, total);
            require(schedule(1, rate, bpm, 4096) == schedule(127, rate, bpm, 4096),
                    "single-sample callbacks changed timing");
            for (int block : { 64, 127, 256, 1024, static_cast<int>(loop + 31) })
                require(schedule(block, rate, bpm, total) == reference, "timing depends on buffer size");
            const auto expected = std::llround(rate * 60.0 / bpm);
            require(std::find(reference.begin(), reference.end(), Event { expected, 60, false }) != reference.end(),
                    "note-off not on exact beat");
            require(std::find(reference.begin(), reference.end(), Event { expected, 60, true }) != reference.end(),
                    "adjacent note-on not on exact beat");
            require(std::find(reference.begin(), reference.end(), Event { loop, 60, true }) != reference.end(),
                    "loop seam loses first note");
            require(std::find(reference.begin(), reference.end(), Event { loop, sonora::drumBaseNote, true }) != reference.end(),
                    "drums do not share melody loop boundary");
        }

    sonora::LoopScheduler scheduler;
    scheduler.configure(48000, 120);
    const auto project = fixture();
    scheduler.process(project.melodies[0], 1000, false, [](const auto&, bool, int) {});
    int chased = 0;
    scheduler.process(project.melodies[0], 1, true, [&](const auto& note, bool on, int offset) {
        if (note.id == 1 && on && offset == 0)
            ++chased;
    });
    require(chased == 1, "held note not chased after edit");
    const auto before = scheduler.tickPosition();
    scheduler.configure(48000, 180);
    require(std::abs(scheduler.tickPosition() - before) < 0.1, "tempo change jumps musical position");
    scheduler.rewind();
    require(scheduler.tickPosition() == 0.0, "rewind failed");
}

void testPersistence()
{
    const auto original = fixture();
    sonora::ProjectState loaded;
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(original), loaded).wasOk(), "decode failed");
    require(loaded == original, "round-trip changed notes or tempo");
    const auto good = loaded;
    require(sonora::ProjectIO::decode("{broken json", loaded).failed(), "malformed JSON accepted");
    require(loaded == good, "failed load mutated the project");
    auto json = sonora::ProjectIO::encode(original);
    require(sonora::ProjectIO::decode(json.replace("\"version\": 8", "\"version\": 9"), loaded).failed(),
            "unknown version accepted");
    require(sonora::ProjectIO::decode(json.replace("\"version\": 8", "\"version\": 4294967297"), loaded).failed(),
            "overflowed version accepted");
    require(sonora::ProjectIO::decode(json.replace("\"velocity\": 100", "\"velocity\": 0"), loaded).failed(),
            "zero velocity accepted");
    require(sonora::ProjectIO::decode(json.replace("\"pitch\": 60", "\"pitch\": 4294967356"), loaded).failed(),
            "overflowed pitch accepted");
    auto invalid = original;
    invalid.melodies[0].notes[1].start = 480;
    require(!invalid.valid(), "same-pitch overlap accepted");
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(invalid), loaded).failed(), "overlap decoded");
    invalid = original;
    invalid.melodies[0].notes[1].id = 1;
    require(!invalid.valid(), "duplicate ID accepted");
    invalid = original;
    invalid.melodies[0].notes[3].duration += 1;
    require(!invalid.valid(), "note extends past loop");

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("sonora-roundtrip", ".json");
    require(sonora::ProjectIO::save(file, original).wasOk(), "atomic save failed");
    require(sonora::ProjectIO::save(file, invalid).failed(), "invalid save accepted");
    require(sonora::ProjectIO::load(file, loaded).wasOk() && loaded == original,
            "failed save destroyed good file");
    auto updated = original;
    updated.bpm = 150;
    require(sonora::ProjectIO::save(file, updated).wasOk(), "replacement save failed");
    require(sonora::ProjectIO::load(file, loaded).wasOk() && loaded == updated, "replacement not persisted");
    require(file.deleteFile(), "test file cleanup failed");
}

void testDrumPersistence()
{
    auto original = fixture();
    original.melodyMix = { 0.47f, false, true };
    original.drumMix = { 1.12f, true, false };
    const auto json = sonora::ProjectIO::encode(original);
    sonora::ProjectState loaded;
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == original,
            "track mix or drum steps changed on round-trip");

    // Construct an actual version-1 document, with notes at the root and no tracks.
    auto legacy = juce::JSON::parse(json);
    auto* object = legacy.getDynamicObject();
    const auto notes = object->getProperty("tracks").getArray()->getReference(0)
                          .getDynamicObject()->getProperty("patterns").getArray()->getReference(0);
    object->setProperty("notes", notes);
    object->setProperty("version", 1);
    object->removeProperty("tracks");
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk(), "legacy project migration failed");
    require(loaded.melodies[0] == original.melodies[0] && loaded.bpm == original.bpm && loaded.drumPatterns[0].hitCount() == 0,
            "migration changed melody or added drum hits");
    require(loaded.melodyMix.volume == 1.0f && !loaded.melodyMix.mute && !loaded.drumMix.solo,
            "legacy mixer defaults wrong");
    const auto migrated = loaded;
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(migrated), loaded).wasOk() && loaded == migrated,
            "migrated project cannot be saved/reopened");

    auto reject = [&](auto change) {
        auto document = juce::JSON::parse(json);
        auto* tracks = document.getDynamicObject()->getProperty("tracks").getArray();
        change(*tracks);
        loaded = original;
        require(sonora::ProjectIO::decode(juce::JSON::toString(document), loaded).failed(),
                "malformed drum/track state accepted");
        require(loaded == original, "bad drum project destroyed current state");
    };
    reject([](auto& tracks) { tracks.getReference(1).getDynamicObject()->setProperty("id", 1); });
    reject([](auto& tracks) { tracks.getReference(1).getDynamicObject()->setProperty("instrument", "unknown-kit"); });
    reject([](auto& tracks) { tracks.getReference(0).getDynamicObject()->setProperty("volume", -1.0); });
    reject([](auto& tracks) { tracks.getReference(0).getDynamicObject()->setProperty("mute", "false"); });
    reject([](auto& tracks) { tracks.getReference(1).getDynamicObject()->getProperty("grids").getArray()->getReference(0).getArray()->removeLast(); });
    reject([](auto& tracks) {
        tracks.getReference(1).getDynamicObject()->getProperty("grids").getArray()->getReference(0).getArray()->getReference(0).getArray()->removeLast();
    });
    reject([](auto& tracks) {
        tracks.getReference(1).getDynamicObject()->getProperty("grids").getArray()->getReference(0).getArray()->getReference(0).getArray()->set(0, 128);
    });
    reject([](auto& tracks) {
        tracks.getReference(1).getDynamicObject()->getProperty("grids").getArray()->getReference(0).getArray()->getReference(0).getArray()->set(0, 0.5);
    });
}

void testQueue()
{
    sonora::SnapshotQueue<int, 4> queue;
    require(queue.push(1) && queue.push(2) && queue.push(3), "queue initial push failed");
    require(!queue.push(4), "queue overwrote unread state");
    int value = 0;
    require(queue.pop(value) && value == 1, "queue lost FIFO ordering");
    require(queue.push(4), "queue wrap failed");
    for (int expected : { 2, 3, 4 })
        require(queue.pop(value) && value == expected, "queue wrap corrupted state");
    require(!queue.pop(value), "empty queue returned state");

    sonora::SnapshotQueue<sonora::ProjectState> states;
    constexpr int iterations = 10000;
    std::thread producer([&] {
        for (int i = 1; i <= iterations; ++i)
        {
            auto state = fixture();
            state.melodies[0].notes[0].id = static_cast<std::uint32_t>(i);
            while (!states.push(state))
                std::this_thread::yield();
        }
    });
    bool coherent = true;
    for (int i = 1; i <= iterations; ++i)
    {
        sonora::ProjectState state;
        while (!states.pop(state))
            std::this_thread::yield();
        coherent = coherent && state.melodies[0].notes[0].id == static_cast<std::uint32_t>(i)
            && state.melodies[0].notes[3] == fixture().melodies[0].notes[3];
    }
    producer.join();
    require(coherent, "concurrent snapshot transfer was torn or reordered");
}

std::vector<float> render(int blockSize, const sonora::ProjectState& project = fixture(), double sampleRate = 48000)
{
    sonora::AudioEngine engine;
    engine.prepare(sampleRate);
    require(engine.submit(project), "engine rejected valid pattern");
    engine.setPlaying(true);
    constexpr int samples = 48000;
    std::vector<float> output;
    output.reserve(samples);
    juce::AudioBuffer<float> buffer(2, blockSize + 16);
    for (int start = 0; start < samples; start += blockSize)
    {
        const int count = std::min(blockSize, samples - start);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(channel, i, 0.25f);
        // A nonzero start detects MIDI/audio offset mismatches.
        engine.process({ &buffer, 7, count });
        require(buffer.getSample(0, 6) == 0.25f && buffer.getSample(0, 7 + count) == 0.25f,
                "engine touched samples outside active region");
        for (int i = 0; i < count; ++i)
        {
            const auto sample = buffer.getSample(0, 7 + i);
            require(std::isfinite(sample), "nonfinite audio output");
            output.push_back(sample);
        }
    }
    engine.stop();
    engine.process({ &buffer, 0, blockSize });
    require(buffer.getMagnitude(0, blockSize) == 0.0f, "stop left sounding notes");
    require(engine.getTickPosition() == 0.0, "stop did not rewind");
    engine.release();
    return output;
}

void testAudio()
{
    const auto reference = render(64);
    require(std::any_of(reference.begin(), reference.end(), [](float x) { return std::abs(x) > 0.01f; }),
            "synth rendered silence");
    for (int size : { 127, 512, 1024 })
    {
        const auto candidate = render(size);
        require(candidate.size() == reference.size(), "render length mismatch");
        for (std::size_t i = 0; i < reference.size(); ++i)
            require(std::abs(candidate[i] - reference[i]) < 1.0e-6f, "audio depends on callback size");
    }

    sonora::AudioEngine engine;
    engine.prepare(48000);
    sonora::ProjectState shortNote;
    shortNote.melodies[0].count = 1;
    shortNote.melodies[0].notes[0] = { 1, 0, 240, 60, 100 };
    require(engine.submit(shortNote), "short note rejected");
    engine.setPlaying(true);
    juce::AudioBuffer<float> buffer(2, 256);
    for (int i = 0; i < 120; ++i)
        engine.process({ &buffer, 0, 256 });
    require(buffer.getMagnitude(0, 256) == 0.0f, "released note never became silent");
    engine.stop();
    engine.process({ &buffer, 0, 256 });
    engine.setPlaying(true);
    engine.process({ &buffer, 0, 256 });
    require(buffer.getMagnitude(0, 256) > 0.001f, "restart did not play first note");
    shortNote.melodies[0] = {};
    require(engine.submit(shortNote), "empty pattern rejected");
    engine.process({ &buffer, 0, 256 });
    require(buffer.getMagnitude(0, 256) == 0.0f, "deleting a playing note left a stuck voice");
}

void testDrumAudio()
{
    // Every generated pad must be finite, audible, bounded, and repeatable.
    sonora::DrumSampler sampler, duplicate;
    for (int pad = 0; pad < sonora::drumPads; ++pad)
    {
        const auto& data = sampler.sample(pad);
        require(data == duplicate.sample(pad), "starter samples are not deterministic");
        require(!data.empty() && std::all_of(data.begin(), data.end(), [](float x) {
            return std::isfinite(x) && std::abs(x) <= 1.0f;
        }), "invalid generated sample");
        require(std::any_of(data.begin(), data.end(), [](float x) { return std::abs(x) > 0.1f; }),
                "starter pad is silent");
    }
    juce::AudioBuffer<float> buffer(2, 24000);
    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::noteOn(10, sonora::drumBaseNote, static_cast<juce::uint8>(127)), 100);
    buffer.clear();
    sampler.render(buffer, 0, 24000, events, 0.8f);
    require(buffer.getMagnitude(0, 100) == 0.0f && buffer.getMagnitude(100, 1000) > 0.01f,
            "drum trigger lost its sample offset");

    events.clear();
    sampler.stop();
    sampler.trigger(3, 1.0f);
    buffer.clear();
    sampler.render(buffer, 0, 4800, events, 0.8f);
    sampler.trigger(2, 1.0f);
    buffer.clear();
    sampler.render(buffer, 0, 24000, events, 0.8f);
    require(buffer.getMagnitude(12000, 12000) == 0.0f, "closed hat did not choke open hat");

    auto drumsOnly = fixture();
    drumsOnly.melodies[0] = {};
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto small = render(127, drumsOnly, rate);
        const auto large = render(1024, drumsOnly, rate);
        require(std::any_of(small.begin(), small.end(), [](float x) { return std::abs(x) > 0.01f; }),
                "drums-only project is silent");
        for (std::size_t i = 0; i < small.size(); ++i)
            require(std::abs(small[i] - large[i]) < 1.0e-6f, "resampled drums depend on callback size");
    }
    auto muted = fixture();
    muted.melodyMix.mute = muted.drumMix.mute = true;
    const auto silence = render(256, muted);
    require(std::all_of(silence.begin(), silence.end(), [](float x) { return x == 0.0f; }), "muted tracks make sound");
    auto soloDrums = fixture();
    soloDrums.drumMix.solo = true;
    require(render(256, soloDrums) == render(256, drumsOnly), "drum solo leaks melody");
    auto melodyOnly = fixture();
    melodyOnly.drumPatterns[0] = {};
    auto soloMelody = fixture();
    soloMelody.melodyMix.solo = true;
    require(render(256, soloMelody) == render(256, melodyOnly), "melody solo leaks drums");
    auto bothSolo = fixture();
    bothSolo.melodyMix.solo = bothSolo.drumMix.solo = true;
    require(render(256, bothSolo) == render(256), "two soloed tracks should play together");

    sonora::AudioEngine engine;
    engine.prepare(48000);
    require(engine.auditionDrum(0), "pad audition rejected");
    buffer.clear();
    engine.process({ &buffer, 0, 256 });
    require(buffer.getMagnitude(0, 256) > 0.01f, "pad audition needs running transport");
    engine.panic();
    engine.process({ &buffer, 0, 256 });
    require(buffer.getMagnitude(0, 256) == 0.0f, "panic left a drum ringing");
    require(!engine.auditionDrum(-1) && !engine.auditionDrum(8) && !engine.auditionDrum(0, 128),
            "invalid audition accepted");
}
void testArrangement()
{
    // Song mode tiles the 4-bar pattern across gated sections, then ends.
    sonora::LoopScheduler scheduler;
    scheduler.configure(48000, 120);
    auto project = fixture();
    project.song.sections = 3;
    project.song.melodyOn = { true, false, true, true, true, true, true, true };
    project.song.drumsOn = { true, true, false, true, true, true, true, true };
    std::vector<Event> events;
    const auto loopFrames = std::llround(48000.0 * 60.0 / 120.0 * sonora::patternTicks / sonora::ticksPerQuarter);
    bool finished = false;
    std::int64_t position = 0;
    while (!finished)
    {
        finished = scheduler.processSong(project.melodies, project.song, 512, false,
            [&](const sonora::Note& note, bool on, int offset) {
                events.push_back({ position + offset, note.pitch, on });
            });
        scheduler.scheduleDrumsSong(project.drumPatterns, project.song, 512,
            [&](int pad, std::uint8_t, int offset) {
                events.push_back({ position + offset, sonora::drumBaseNote + pad, true });
            });
        position += 512;
        require(position < loopFrames * 4, "song never finished");
    }
    require(position >= loopFrames * 3, "song ended early");
    // Drum hits land on baseNote+pad (36-43); the fixture melody lives above.
    const auto isDrumHit = [](const Event& event) {
        return event.pitch >= sonora::drumBaseNote && event.pitch < sonora::drumBaseNote + 8;
    };
    bool melodyInSection0 = false, melodyInSection2 = false, drumsInSection1 = false;
    for (const auto& event : events)
    {
        // An off exactly on a seam ends the previous section's note, so it
        // attributes backwards; ons always attribute to their own section.
        auto section = event.sample / loopFrames;
        if (!event.on && event.sample % loopFrames == 0 && section > 0)
            --section;
        require(section >= 0 && section < 3, "event outside song");
        if (isDrumHit(event))
        {
            require(project.song.drumsOn[static_cast<std::size_t>(section)], "muted section plays drums");
            drumsInSection1 = drumsInSection1 || section == 1;
        }
        else
        {
            require(project.song.melodyOn[static_cast<std::size_t>(section)], "muted section plays melody");
            melodyInSection0 = melodyInSection0 || section == 0;
            melodyInSection2 = melodyInSection2 || section == 2;
        }
    }
    // Presence, not just absence: every enabled section actually sounds.
    require(melodyInSection0 && melodyInSection2, "enabled melody section silent");
    require(drumsInSection1, "enabled drum section silent");
    require(std::none_of(events.begin(), events.end(), [&](const Event& e) {
        if (isDrumHit(e))
            return false;
        // Ons must live in enabled sections; offs may sit exactly on a seam
        // where they end the previous section's note.
        if (e.on)
            return e.sample >= loopFrames && e.sample < loopFrames * 2;
        return e.sample > loopFrames && e.sample < loopFrames * 2;
    }), "disabled melody section leaked");
    // Per-section patterns: section 1 plays slot B while section 0 plays A.
    {
        sonora::LoopScheduler variation;
        variation.configure(48000, 120);
        sonora::ProjectState song = fixture();
        song.melodies[0] = {};
        song.melodies[0].count = 1;
        song.melodies[0].notes[0] = { 1, 0, 240, 60, 100 };
        song.melodies[1] = {};
        song.melodies[1].count = 1;
        song.melodies[1].notes[0] = { 2, 0, 240, 72, 100 };
        song.song.sections = 2;
        song.song.melodyPattern = { 0, 1, 0, 0, 0, 0, 0, 0 };
        std::vector<Event> sectioned;
        std::int64_t position = 0;
        bool finished = false;
        while (!finished)
        {
            finished = variation.processSong(song.melodies, song.song, 512, false,
                [&](const sonora::Note& note, bool on, int offset) {
                    if (on && (note.pitch == 60 || note.pitch == 72))
                        sectioned.push_back({ position + offset, note.pitch, on });
                });
            position += 512;
            require(position < loopFrames * 3, "variation song never finished");
        }
        require(sectioned.size() == 2, "variation song emitted wrong note count");
        require(sectioned[0].pitch == 60 && sectioned[0].sample == 0, "section 0 plays wrong pattern");
        require(sectioned[1].pitch == 72 && sectioned[1].sample == loopFrames, "section 1 plays wrong pattern");
    }
    // Loop mode still wraps via processLoop (processSong is song-only now).
    scheduler.rewind();
    scheduler.processLoop(project.melodies[0], 512, false, [](const auto&, bool, int) {});
    // Engine loop preview follows the selected library slot.
    {
        sonora::AudioEngine preview;
        preview.prepare(48000);
        sonora::ProjectState song = fixture();
        song.melodies[0] = {};
        song.melodies[1].count = 1;
        song.melodies[1].notes[0] = { 5, 0, 240, 71, 100 };
        song.drumPatterns[0] = {};
        require(preview.submit(song), "variation project rejected");
        juce::AudioBuffer<float> buffer(2, 512);
        preview.setPlaying(true);
        preview.process({ &buffer, 0, 512 });
        preview.process({ &buffer, 0, 512 });
        require(buffer.getMagnitude(0, 512) < 1.0e-5f, "unselected slot leaks into loop preview");
        preview.setLoopPatterns(1, 0);
        preview.stop();
        preview.process({ &buffer, 0, 512 });
        preview.setPlaying(true);
        preview.process({ &buffer, 0, 512 });
        preview.process({ &buffer, 0, 512 });
        require(buffer.getMagnitude(0, 512) > 0.01f, "selected slot silent in loop preview");
    }

    // Persistence: v3 round-trip plus v2 migration with default 2-section song.
    sonora::ProjectState songProject = fixture();
    songProject.songMode = true;
    songProject.song.sections = 3;
    songProject.song.melodyOn[1] = false;
    sonora::ProjectState loaded;
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(songProject), loaded).wasOk()
        && loaded == songProject, "song arrangement round-trip failed");
    auto legacy = downshapeToVersion(juce::JSON::parse(sonora::ProjectIO::encode(songProject)), 2);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk(), "v2 migration failed");
    require(!loaded.songMode && loaded.song.sections == 2, "v2 default arrangement wrong");
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(loaded), loaded).wasOk(), "migrated v2 re-save failed");
    auto bad = juce::JSON::parse(sonora::ProjectIO::encode(songProject));
    bad.getDynamicObject()->getProperty("song").getDynamicObject()->setProperty("sections", 9);
    require(sonora::ProjectIO::decode(juce::JSON::toString(bad), loaded).failed(), "oversized song accepted");

    // Engine: song mode stops at the end; loop mode keeps playing.
    sonora::AudioEngine engine;
    engine.prepare(48000);
    songProject.melodies[0] = {};
    songProject.song.sections = 1;
    require(engine.submit(songProject), "song submit rejected");
    engine.setPlaying(true);
    juce::AudioBuffer<float> buffer(2, 512);
    for (int i = 0; i < 2000 && engine.isPlaying(); ++i)
        engine.process({ &buffer, 0, 512 });
    require(!engine.isPlaying(), "song did not stop at end");
}
}

void testExport()
{
    // Loop bounce renders exactly one pattern length with audible content.
    sonora::ExportJob loop;
    loop.project = fixture();
    auto result = sonora::OfflineExport::render(loop);
    require(result.ok(), "loop export failed");
    const int loopFrames = sonora::OfflineExport::expectedFrames(loop);
    require(result.audio.getNumSamples() == loopFrames, "loop export length wrong");
    require(result.audio.getNumChannels() == 2, "export is not stereo");
    require(result.audio.getMagnitude(0, loopFrames) > 0.01f, "loop export is silent");
    require(std::abs(result.peak - result.audio.getMagnitude(0, loopFrames)) < 1.0e-6f, "peak misreported");
    // Offline bounce matches live engine output sample-for-sample.
    sonora::AudioEngine liveEngine;
    liveEngine.prepare(48000.0);
    require(liveEngine.submit(fixture()), "live engine rejected fixture");
    liveEngine.setPlaying(true);
    juce::AudioBuffer<float> liveBlock(2, 512);
    for (int start = 0; start < loopFrames; start += 512)
    {
        const int count = std::min(512, loopFrames - start);
        liveEngine.process({ &liveBlock, 0, count });
        for (int i = 0; i < count; ++i)
            require(std::abs(result.audio.getSample(0, start + i) - liveBlock.getSample(0, i)) < 1.0e-6f,
                    "export differs from live playback");
    }

    // Song bounce covers the arrangement plus tail, then rings out.
    sonora::ExportJob song;
    song.project = fixture();
    song.project.song.sections = 2;
    song.songRange = true;
    song.tailSeconds = 0.5;
    auto songResult = sonora::OfflineExport::render(song);
    require(songResult.ok(), "song export failed");
    require(songResult.audio.getNumSamples() == sonora::OfflineExport::expectedFrames(song),
            "song export length wrong");
    require(songResult.audio.getMagnitude(0, songResult.audio.getNumSamples()) > 0.01f, "song export silent");

    // Normalization brings the peak to 0.99 without clipping.
    sonora::ExportJob loud = loop;
    loud.project.melodyMix.volume = 1.5f;
    loud.project.drumMix.volume = 1.5f;
    loud.normalize = true;
    auto loudResult = sonora::OfflineExport::render(loud);
    require(loudResult.ok() && loudResult.normalized, "normalize export failed");
    require(std::abs(loudResult.peak - 0.99f) < 1.0e-4f, "normalized peak wrong");
    require(!loudResult.clipped, "normalized export reports clipping");

    // Invalid jobs are rejected, and cancellation aborts rendering.
    sonora::ExportJob bad = loop;
    bad.sampleRate = 22050.0;
    require(!sonora::OfflineExport::render(bad).ok(), "bad sample rate accepted");
    bad = loop;
    bad.bitDepth = 8;
    require(!sonora::OfflineExport::render(bad).ok(), "bad bit depth accepted");
    int calls = 0;
    auto cancelled = sonora::OfflineExport::render(loop, [&](double) { return ++calls < 3; });
    require(!cancelled.ok(), "cancelled export reported success");

    // WAV round-trip: file parses with the expected format and duration.
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("sonora-export", ".wav");
    require(sonora::OfflineExport::writeWav(file, result, 16).wasOk(), "WAV write failed");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
        new juce::FileInputStream(file), true));
    require(reader != nullptr, "WAV file unreadable");
    require(reader->numChannels == 2 && reader->bitsPerSample == 16
        && std::abs(reader->sampleRate - 48000.0) < 1.0
        && reader->lengthInSamples == loopFrames, "WAV header wrong");
    require(sonora::OfflineExport::writeWav(file, result, 8).failed(), "bad bit depth write accepted");
    require(file.deleteFile(), "export test file cleanup failed");

    // Song exports include recorded takes found in the job's media folder.
    const auto media = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getNonexistentChildFile("sonora-take-media", "");
    require(media.createDirectory().wasOk(), "media dir creation failed");
    {
        juce::WavAudioFormat takeFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(takeFormat.createWriterFor(
            new juce::FileOutputStream(media.getChildFile("take-3.wav")), 48000.0, 1, 16, {}, 0));
        require(writer != nullptr, "take fixture write failed");
        juce::AudioBuffer<float> dc(1, 4800);
        for (int i = 0; i < 4800; ++i)
            dc.setSample(0, i, 0.3f);
        require(writer->writeFromAudioSampleBuffer(dc, 0, 4800), "take fixture samples failed");
    }
    sonora::ExportJob withTake;
    withTake.project = fixture();
    withTake.project.melodies[0] = {};
    withTake.project.drumPatterns[0] = {};
    withTake.project.song.sections = 1;
    withTake.project.takeCount = 1;
    withTake.project.takes[0].id = 3;
    withTake.project.takes[0].setFileName("take-3.wav");
    withTake.project.takes[0].startTick = 0;
    withTake.project.takes[0].frames = 4800;
    withTake.songRange = true;
    withTake.tailSeconds = 0.0;
    withTake.mediaDir = media;
    auto takeBounce = sonora::OfflineExport::render(withTake);
    require(takeBounce.ok(), "take export failed");
    require(takeBounce.audio.getMagnitude(100, 1000) > 0.2f, "export dropped the recorded take");
    require(media.deleteRecursively(), "media dir cleanup failed");
}

void testFx()
{
    auto sine = [](juce::AudioBuffer<float>& buffer) {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(channel, i, 0.2f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0));
    };
    auto peakOf = [](juce::AudioBuffer<float>& buffer) { return buffer.getMagnitude(0, buffer.getNumSamples()); };

    // Each default stage is transparent on its own.
    {
        juce::AudioBuffer<float> buffer(2, 8192);
        sine(buffer);
        const auto before = peakOf(buffer);
        sonora::ThreeBandEq eq;
        eq.prepare(48000.0);
        eq.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        require(std::abs(peakOf(buffer) - before) / before < 0.02, "flat EQ colors audio");
    }
    {
        juce::AudioBuffer<float> buffer(2, 8192);
        sine(buffer);
        const auto before = peakOf(buffer);
        sonora::Compressor comp;
        comp.prepare(48000.0);
        comp.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        require(std::abs(peakOf(buffer) - before) / before < 0.02, "1:1 compressor colors audio");
    }
    {
        juce::AudioBuffer<float> buffer(2, 8192);
        sine(buffer);
        const auto before = peakOf(buffer);
        sonora::TempoDelay delay;
        delay.prepare(48000.0);
        delay.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        juce::AudioBuffer<float> dry(2, 8192);
        sine(dry);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            require(std::abs(buffer.getSample(0, i) - dry.getSample(0, i)) < 1.0e-6f, "dry delay differs");
        require(std::abs(peakOf(buffer) - before) < 1.0e-6f, "dry delay changes level");
    }
    {
        juce::AudioBuffer<float> buffer(2, 8192);
        sine(buffer);
        sonora::SimpleReverb reverb;
        reverb.prepare(48000.0);
        reverb.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        juce::AudioBuffer<float> dry(2, 8192);
        sine(dry);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            require(std::abs(buffer.getSample(0, i) - dry.getSample(0, i)) < 1.0e-6f, "dry reverb differs");
    }
    // Default full chains are transparent; disabled master passes audio through.
    {
        juce::AudioBuffer<float> buffer(2, 8192);
        sine(buffer);
        const auto before = peakOf(buffer);
        sonora::TrackChain chain;
        chain.prepare(48000.0);
        chain.process(buffer, 0, buffer.getNumSamples());
        require(std::abs(peakOf(buffer) - before) / before < 0.02, "default chain colors audio");
        sonora::BrickLimiter limiter;
        limiter.prepare(48000.0);
        limiter.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        require(std::abs(peakOf(buffer) - before) / before < 0.02, "idle limiter colors audio");
    }
    // EQ actually equalizes: +12 dB low shelf on a 90 Hz tone gets louder.
    {
        juce::AudioBuffer<float> buffer(2, 8192);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(channel, i, 0.2f * std::sin(2.0 * juce::MathConstants<double>::pi * 90.0 * i / 48000.0));
        sonora::ThreeBandEq eq;
        eq.prepare(48000.0);
        sonora::EqParams boosted;
        boosted.low = 12.0f;
        eq.setParams(boosted);
        eq.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        require(peakOf(buffer) > 0.5f, "low shelf boost did not amplify bass");
    }
    // Compressor actually compresses: hot signal with low threshold gets quieter.
    {
        juce::AudioBuffer<float> buffer(2, 8192);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(channel, i, 0.9f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0));
        sonora::Compressor comp;
        comp.prepare(48000.0);
        sonora::CompParams squashed;
        squashed.thresholdDb = -20.0f;
        squashed.ratio = 8.0f;
        comp.setParams(squashed);
        comp.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        // Steady state only: the attack transient and makeup ramp are louder by design.
        require(buffer.getMagnitude(4096, 4096) < 0.6f, "compression did not reduce hot signal");
    }
    // Delay with mix produces echoes after the input stops; limiter caps overs.
    {
        juce::AudioBuffer<float> buffer(2, 24000);
        buffer.clear();
        for (int i = 0; i < 100; ++i)
        {
            buffer.setSample(0, i, 0.8f);
            buffer.setSample(1, i, 0.8f);
        }
        sonora::TempoDelay delay;
        delay.prepare(48000.0);
        sonora::DelayParams echo;
        echo.timeMs = 100.0f;
        echo.feedback = 0.5f;
        echo.mix = 0.8f;
        delay.setParams(echo);
        for (int i = 0; i < 40; ++i)
            delay.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        require(buffer.getMagnitude(4800, 4800) > 0.05f, "delay produced no echo");
        sonora::BrickLimiter limiter;
        limiter.prepare(48000.0);
        juce::AudioBuffer<float> hot(2, 4096);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < hot.getNumSamples(); ++i)
                hot.setSample(channel, i, 2.0f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0));
        limiter.process(hot.getWritePointer(0), hot.getWritePointer(1), hot.getNumSamples());
        require(hot.getMagnitude(0, hot.getNumSamples()) <= 0.96f, "limiter let overs through");
        require(limiter.getReductionDb() < -1.0f, "limiter reports no reduction");
    }
    // Invalid parameters are rejected by validation.
    {
        sonora::TrackFx fx;
        require(fx.valid(), "default fx invalid");
        fx.eq.low = 99.0f;
        require(!fx.valid(), "wild EQ accepted");
        fx = sonora::TrackFx();
        fx.delay.feedback = 0.99f;
        require(!fx.valid(), "runaway feedback accepted");
        sonora::LimiterParams master;
        require(master.valid(), "default master invalid");
        master.ceilingDb = 3.0f;
        require(!master.valid(), "above-zero ceiling accepted");
    }
}

void testFxPersistence()
{
    auto original = fixture();
    original.melodyFx.eq.low = 4.5f;
    original.melodyFx.eq.midFreq = 800.0f;
    original.melodyFx.comp.thresholdDb = -12.0f;
    original.melodyFx.comp.ratio = 4.0f;
    original.melodyFx.delay.mix = 0.25f;
    original.melodyFx.delay.feedback = 0.4f;
    original.melodyFx.reverb.mix = 0.2f;
    original.melodyFx.reverb.enabled = false;
    original.drumFx.eq.high = -3.0f;
    original.drumFx.comp.enabled = false;
    original.master.ceilingDb = -1.0f;
    original.master.releaseMs = 120.0f;
    sonora::ProjectState loaded;
    const auto json = sonora::ProjectIO::encode(original);
    require(json.contains("\"version\": 8"), "projects must save as v8");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == original,
            "fx round-trip changed parameters");

    // Version 3 documents migrate with transparent default effects.
    auto legacy = downshapeToVersion(juce::JSON::parse(json), 3);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk(), "v3 migration failed");
    require(loaded == fixture(), "v3 migration did not restore transparent defaults");
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(loaded), loaded).wasOk(), "migrated v3 re-save failed");

    auto reject = [&](auto change, const char* what) {
        auto document = juce::JSON::parse(json);
        change(document);
        loaded = original;
        require(sonora::ProjectIO::decode(juce::JSON::toString(document), loaded).failed(), what);
        require(loaded == original, "bad fx project destroyed current state");
    };
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
            .getDynamicObject()->getProperty("fx").getDynamicObject()
            ->getProperty("eq").getDynamicObject()->setProperty("low", 99.0);
    }, "wild EQ accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
            .getDynamicObject()->getProperty("fx").getDynamicObject()
            ->getProperty("delay").getDynamicObject()->setProperty("feedback", 0.99);
    }, "runaway feedback accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("tracks").getArray()->getReference(1)
            .getDynamicObject()->getProperty("fx").getDynamicObject()->removeProperty("reverb");
    }, "incomplete chain accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("master").getDynamicObject()->setProperty("ceilingDb", 3.0);
    }, "above-zero ceiling accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("master").getDynamicObject()->setProperty("enabled", "yes");
    }, "non-bool master bypass accepted");

    // Non-default effects audibly change the bounce but stay finite and bounded.
    sonora::ExportJob job;
    job.project = original;
    auto effected = sonora::OfflineExport::render(job);
    require(effected.ok(), "effected export failed");
    sonora::ExportJob plain;
    plain.project = fixture();
    auto clean = sonora::OfflineExport::render(plain);
    require(clean.ok(), "clean export failed");
    bool differs = false;
    for (int i = 0; i < effected.audio.getNumSamples(); ++i)
        if (std::abs(effected.audio.getSample(0, i) - clean.audio.getSample(0, i)) > 1.0e-4f)
        {
            differs = true;
            break;
        }
    require(differs, "effects made no audible difference to export");
    require(effected.peak <= 1.0f, "limiter let export clip");
}

void testTakePersistence()
{
    auto original = fixture();
    original.takeCount = 2;
    original.takes[0].id = 7;
    original.takes[0].setFileName("take-7.wav");
    original.takes[0].startTick = 3840;
    original.takes[0].frames = 48000;
    original.takes[0].gain = 0.9f;
    original.takes[0].channels = 2;
    original.takes[1].id = 9;
    original.takes[1].setFileName("take-9.wav");
    original.takes[1].startTick = 0;
    original.takes[1].frames = 24000;
    original.takes[1].mute = true;
    sonora::ProjectState loaded;
    const auto json = sonora::ProjectIO::encode(original);
    require(json.contains("\"version\": 8"), "take projects must save as v8");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == original,
            "take round-trip changed metadata");

    // Version 4 documents migrate with an empty take list.
    auto legacy = downshapeToVersion(juce::JSON::parse(json), 4);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk(), "v4 migration failed");
    require(loaded.takeCount == 0, "v4 migration invented takes");
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(loaded), loaded).wasOk(), "migrated v4 re-save failed");

    auto reject = [&](auto change, const char* what) {
        auto document = juce::JSON::parse(json);
        change(document);
        loaded = original;
        require(sonora::ProjectIO::decode(juce::JSON::toString(document), loaded).failed(), what);
        require(loaded == original, "bad take project destroyed current state");
    };
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("takes").getArray()->getReference(0)
            .getDynamicObject()->setProperty("channels", 6);
    }, "six-channel take accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("takes").getArray()->getReference(0)
            .getDynamicObject()->setProperty("gain", 5.0);
    }, "over-gained take accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("takes").getArray()->getReference(1)
            .getDynamicObject()->setProperty("id", 7);
    }, "duplicate take ID accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("takes").getArray()->getReference(0)
            .getDynamicObject()->setProperty("file", "");
    }, "empty take file accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("takes").getArray()->getReference(0)
            .getDynamicObject()->setProperty("frames", -100);
    }, "negative take length accepted");

    // Latency compensation: 512 samples at 48 kHz / 120 BPM is ~21 ticks.
    require(sonora::latencyCompensationTicks(0, 25.0) == 0, "zero latency misreported");
    require(sonora::latencyCompensationTicks(512, 25.0) == 20, "latency math wrong");
    require(sonora::latencyCompensationTicks(-5, 25.0) == 0, "negative latency accepted");
    require(sonora::latencyCompensationTicks(512, 0.0) == 0, "zero rate accepted");
    // Top-of-song takes compensate latency with a negative start (trimmed head).
    {
        sonora::AudioTakeMeta preRoll;
        preRoll.id = 1;
        preRoll.setFileName("take-1.wav");
        preRoll.startTick = -480;
        preRoll.frames = 48000;
        require(preRoll.valid(), "latency pre-roll take rejected");
        preRoll.startTick = -sonora::patternTicks - 1;
        require(!preRoll.valid(), "excessive pre-roll accepted");
    }
}

void testRecorder()
{
    // Record a synthetic stereo ramp through the FIFO + writer thread.
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("sonora-take", ".wav");
    sonora::TakeRecorder recorder;
    require(recorder.start(file, 48000.0, 2), "recorder failed to start");
    require(!recorder.start(file, 48000.0, 2), "double start accepted");
    constexpr int blocks = 20, blockSize = 512;
    std::vector<float> left(blockSize), right(blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            left[static_cast<std::size_t>(i)] = static_cast<float>(b * blockSize + i) / 48000.0f;
            right[static_cast<std::size_t>(i)] = -left[static_cast<std::size_t>(i)];
        }
        const float* ptrs[2] = { left.data(), right.data() };
        recorder.push(ptrs, 2, blockSize);
    }
    const int frames = recorder.stop();
    require(frames == blocks * blockSize, "recorder lost frames");
    require(recorder.getOverruns() == 0, "recorder reported phantom overruns");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
        new juce::FileInputStream(file), true));
    require(reader != nullptr && reader->numChannels == 2
        && reader->lengthInSamples == frames, "recorded WAV header wrong");
    juce::AudioBuffer<float> back(2, frames);
    reader->read(&back, 0, frames, 0, true, true);
    for (int i = 0; i < frames; i += 7)
    {
        const float expected = static_cast<float>(i) / 48000.0f;
        require(std::abs(back.getSample(0, i) - expected) < 0.001f, "recorded left channel corrupt");
        require(std::abs(back.getSample(1, i) + expected) < 0.001f, "recorded right channel corrupt");
    }
    require(file.deleteFile(), "recorder test file cleanup failed");
    require(recorder.stop() < 0, "stopped idle recorder accepted");
    require(!recorder.start(file, 48000.0, 3), "three-channel record accepted");
}

void testTakePlayback()
{
    // A preloaded take renders at its punch-in offset in song mode.
    auto project = fixture();
    project.melodies[0] = {};
    project.drumPatterns[0] = {};
    project.song.sections = 1;
    project.songMode = true;
    sonora::AudioEngine engine;
    engine.prepare(48000.0);
    auto* set = new sonora::TakeSet();
    sonora::PreloadedTake take;
    take.id = 1;
    take.startTick = 960; // one beat in
    take.gain = 0.5f;
    take.audio.setSize(1, 4800);
    for (int i = 0; i < 4800; ++i)
        take.audio.setSample(0, i, 0.4f);
    set->takes.push_back(std::move(take));
    require(engine.submit(project), "take project rejected");
    engine.retireTakeSet(set);
    engine.setPlaying(true);
    juce::AudioBuffer<float> buffer(2, 512);
    const double framesPerTick = 48000.0 * 60.0 / (project.bpm * sonora::ticksPerQuarter);
    const int startFrame = static_cast<int>(960 * framesPerTick);
    std::vector<float> output;
    for (int start = 0; start < startFrame + 2000; start += 512)
    {
        engine.process({ &buffer, 0, 512 });
        for (int i = 0; i < 512; ++i)
            output.push_back(buffer.getSample(0, i));
    }
    // Silence before the punch-in, full level (0.4 * 0.5 gain, then limiter
    // ceiling is far above) during the take.
    require(std::all_of(output.begin(), output.begin() + startFrame,
                        [](float x) { return std::abs(x) < 1.0e-5f; }), "take leaks before punch-in");
    float takeLevel = 0.0f;
    for (int i = startFrame + 100; i < startFrame + 1500; ++i)
        takeLevel = std::max(takeLevel, std::abs(output[static_cast<std::size_t>(i)]));
    require(std::abs(takeLevel - 0.2f) < 0.02f, "take plays at wrong level or offset");
    // Muted takes stay silent mid-take; unmuting restores them.
    set->takes[0].mute = true;
    engine.process({ &buffer, 0, 512 });
    require(buffer.getMagnitude(0, 512) < 1.0e-5f, "muted take audible");
    set->takes[0].mute = false;
    engine.process({ &buffer, 0, 512 });
    require(buffer.getMagnitude(0, 512) > 0.1f, "unmuted take silent");
    // Loop mode ignores takes entirely.
    project.songMode = false;
    require(engine.submit(project), "loop project rejected");
    engine.stop();
    engine.process({ &buffer, 0, 512 });
    engine.setPlaying(true);
    engine.process({ &buffer, 0, 512 });
    require(buffer.getMagnitude(0, 512) < 1.0e-5f, "take leaks into loop mode");
    engine.retireTakeSet(nullptr);
    delete set;
}

void testPitch()
{
    constexpr double rate = 48000.0;
    auto tone = [&](double hz, double seconds, double centsDrift = 0.0) {
        const int n = static_cast<int>(seconds * rate);
        std::vector<float> audio(static_cast<std::size_t>(n));
        double phase = 0.0;
        for (int i = 0; i < n; ++i)
        {
            // True FM vibrato: integrate the instantaneous frequency so the
            // deviation stays bounded at +/- centsDrift.
            const double drift = centsDrift * std::sin(2.0 * juce::MathConstants<double>::pi * 5.0 * i / rate);
            phase += 2.0 * juce::MathConstants<double>::pi * hz * std::pow(2.0, drift / 1200.0) / rate;
            audio[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(phase));
        }
        return audio;
    };
    auto medianF0 = [](const sonora::PitchContour& contour) {
        std::vector<float> voiced;
        for (const auto& frame : contour.frames)
            if (frame.voiced)
                voiced.push_back(frame.f0Hz);
        require(!voiced.empty(), "no voiced frames detected");
        std::nth_element(voiced.begin(), voiced.begin() + voiced.size() / 2, voiced.end());
        return voiced[voiced.size() / 2];
    };

    // YIN finds a steady 220 Hz tone and rejects silence.
    {
        const auto audio = tone(220.0, 1.0);
        sonora::YinDetector detector(rate);
        const auto contour = detector.analyze(audio.data(), static_cast<int>(audio.size()));
        require(!contour.empty(), "empty contour on audible tone");
        require(std::abs(medianF0(contour) - 220.0) / 220.0 < 0.01, "YIN inaccurate on pure tone");
        std::vector<float> silence(4800, 0.0f);
        const auto quiet = detector.analyze(silence.data(), 4800);
        require(std::all_of(quiet.frames.begin(), quiet.frames.end(),
                            [](const auto& f) { return !f.voiced; }), "silence detected as voiced");
    }
    // Scale snapping: F# in C major goes to F; chromatic rounds normally.
    {
        sonora::PitchScale chromatic;
        require(chromatic.quantize(66.4) == 66.0, "chromatic quantize wrong");
        require(chromatic.quantize(66.6) == 67.0, "chromatic quantize wrong");
        sonora::PitchScale cMajor { 0, 1 };
        require(cMajor.quantize(66.0) == 65.0, "C major should pull F# down to F");
        require(cMajor.quantize(64.0) == 64.0, "C major moved an in-scale note");
        sonora::PitchScale aMinor { 9, 2 };
        require(aMinor.quantize(66.0) == 65.0, "A minor should pull F# down to F");
    }
    // A tone ~46 cents sharp of A3 corrects to A3 chromatically.
    {
        const auto audio = tone(226.0, 1.0);
        sonora::CorrectionSettings settings;
        settings.amount = 1.0f;
        settings.speedMs = 10.0f;
        sonora::PitchContour contour;
        const auto fixed = sonora::correctTake(audio.data(), static_cast<int>(audio.size()),
                                               rate, settings, &contour);
        require(!contour.empty(), "no contour from correction");
        sonora::YinDetector detector(rate);
        const auto recheck = detector.analyze(fixed.data(), static_cast<int>(fixed.size()));
        require(std::abs(medianF0(recheck) - 220.0) / 220.0 < 0.02, "correction missed the target note");
    }
    // Amount 0 reproduces the input; quiet noise passes through bit-exact.
    {
        const auto audio = tone(233.0, 0.5);
        sonora::CorrectionSettings settings;
        settings.amount = 0.0f;
        const auto dry = sonora::correctTake(audio.data(), static_cast<int>(audio.size()), rate, settings);
        for (std::size_t i = 0; i < audio.size(); ++i)
            require(std::abs(dry[i] - audio[i]) < 1.0e-4f, "zero-amount correction alters audio");
        std::vector<float> noise(9600, 1.0e-5f);
        const auto same = sonora::correctTake(noise.data(), 9600, rate, settings);
        for (std::size_t i = 0; i < noise.size(); ++i)
            require(same[i] == noise[i], "unvoiced audio not passed through");
    }
    // Vibrato (±50 cents at 5 Hz) collapses toward the center note.
    {
        const auto audio = tone(220.0, 1.0, 50.0);
        sonora::CorrectionSettings settings;
        settings.amount = 1.0f;
        settings.speedMs = 20.0f;
        const auto fixed = sonora::correctTake(audio.data(), static_cast<int>(audio.size()), rate, settings);
        sonora::YinDetector detector(rate);
        const auto recheck = detector.analyze(fixed.data(), static_cast<int>(fixed.size()));
        double worst = 0.0;
        int voiced = 0;
        for (const auto& frame : recheck.frames)
        {
            if (!frame.voiced)
                continue;
            ++voiced;
            worst = std::max(worst, std::abs(sonora::hzToMidi(frame.f0Hz) - 57.0));
        }
        require(voiced > 10, "vibrato correction lost voicing");
        require(worst < 0.3, "vibrato not tamed by correction");
    }
    // Invalid settings and empty input are rejected safely.
    {
        std::vector<float> audio(4800, 0.1f);
        sonora::CorrectionSettings bad;
        bad.amount = 2.0f;
        require(!bad.valid(), "wild amount accepted");
        const auto dry = sonora::correctTake(audio.data(), 4800, rate, bad);
        for (std::size_t i = 0; i < audio.size(); ++i)
            require(dry[i] == audio[i], "invalid settings altered audio");
        const auto empty = sonora::correctTake(nullptr, 0, rate, sonora::CorrectionSettings());
        require(empty.empty(), "null input produced output");
    }
}

void testMidiHardware()
{
    using namespace sonora::midi;
    // Arturia bank A/B map to the 8 drum voices in pad order.
    for (int pad = 0; pad < 8; ++pad)
    {
        require(arturiaPadForNote(36 + pad) == pad, "Arturia bank A misaligned");
        require(arturiaPadForNote(44 + pad) == pad, "Arturia bank B misaligned");
    }
    require(arturiaPadForNote(35) < 0 && arturiaPadForNote(52) < 0 && arturiaPadForNote(60) < 0,
            "non-pad note mapped to drums");
    // Mackie transport notes.
    require(mcuTransportAction(94, true) == McuAction::Play, "MCU play wrong");
    require(mcuTransportAction(93, true) == McuAction::Stop, "MCU stop wrong");
    require(mcuTransportAction(95, true) == McuAction::Record, "MCU record wrong");
    require(mcuTransportAction(86, true) == McuAction::ToggleLoop, "MCU cycle wrong");
    require(mcuTransportAction(94, false) == McuAction::None, "note-off acted as transport");
    require(mcuTransportAction(60, true) == McuAction::None, "musical note acted as transport");
    // Auto-connect decisions.
    std::vector<MidiPort> ports { { "id-midi", "Minilab3 MIDI" }, { "id-mcu", "Minilab3 MCU" },
                                  { "id-thru", "Minilab3 DIN THRU" }, { "id-keys", "My Keyboard" } };
    {
        const auto result = midiAutoConnect(ports, {});
        require(result.enableIds.size() == 3, "auto-connect missed ports");
        require(result.mcuIds.size() == 1 && result.mcuIds[0] == "id-mcu", "MCU port not flagged");
        require(result.miniLabPresent, "MiniLab not detected");
        for (const auto& id : result.enableIds)
            require(id != "id-thru", "THRU port auto-enabled");
    }
    {
        const auto result = midiAutoConnect(ports, { "id-midi", "id-mcu", "id-keys" });
        require(result.enableIds.empty(), "enabled ports re-enabled");
        require(result.mcuIds.size() == 1, "enabled MCU port lost");
    }
    {
        const auto result = midiAutoConnect({ { "id-t", "Some DIN THRU" } }, {});
        require(result.enableIds.empty() && !result.miniLabPresent, "THRU-only setup wrong");
    }
    {
        const auto result = midiAutoConnect({ { "id-t", "Midi Through Port-0" } }, {});
        require(result.enableIds.empty(), "system Through port auto-enabled");
    }
    require(midiStatusText({}).contains("no MIDI"), "empty MIDI status wrong");
    require(midiStatusText({ { "x", "Minilab3 MIDI" } }) == "Minilab3", "status shortening wrong");
}

namespace
{
void addMessageToQueueAt(juce::MidiMessageCollector& collector, const juce::MidiMessage& message)
{
    // The collector requires real timestamps (it asserts on zero).
    auto stamped = message;
    stamped.setTimeStamp(juce::Time::getMillisecondCounterHiRes());
    collector.addMessageToQueue(stamped);
}
}

void testExpression()
{
    constexpr double rate = 48000.0;
    auto medianF0 = [](const std::vector<float>& audio) {
        sonora::YinDetector detector(rate);
        sonora::PitchContour contour = detector.analyze(audio.data(), static_cast<int>(audio.size()));
        std::vector<float> voiced;
        for (const auto& frame : contour.frames)
            if (frame.voiced)
                voiced.push_back(frame.f0Hz);
        require(!voiced.empty(), "expression render has no voiced frames");
        std::nth_element(voiced.begin(), voiced.begin() + voiced.size() / 2, voiced.end());
        return voiced[voiced.size() / 2];
    };
    auto renderLive = [&](sonora::AudioEngine& engine, int samples) {
        std::vector<float> output;
        juce::AudioBuffer<float> buffer(2, 512);
        for (int start = 0; start < samples; start += 512)
        {
            engine.process({ &buffer, 0, 512 });
            for (int i = 0; i < 512; ++i)
                output.push_back(buffer.getSample(0, i));
        }
        return output;
    };
    // Pitch bend center, full up (+2 st -> B4), full down (-2 st -> G4).
    for (auto [bend, expected] : { std::pair<int, double> { 8192, 440.0 }, { 16383, 493.88 }, { 0, 392.0 } })
    {
        sonora::AudioEngine engine;
        engine.prepare(rate);
        require(engine.submit(sonora::ProjectState()), "empty project rejected");
        addMessageToQueueAt(engine.midiCollector, juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100));
        addMessageToQueueAt(engine.midiCollector, juce::MidiMessage::pitchWheel(1, bend));
        auto output = renderLive(engine, 19200);
        output.erase(output.begin(), output.begin() + 4800); // skip attack
        require(std::abs(medianF0(output) - expected) / expected < 0.015, "pitch bend mistuned");
        addMessageToQueueAt(engine.midiCollector, juce::MidiMessage::noteOff(1, 69));
    }
    // Mod wheel keeps center pitch while staying audible.
    {
        sonora::AudioEngine engine;
        engine.prepare(rate);
        require(engine.submit(sonora::ProjectState()), "empty project rejected");
        addMessageToQueueAt(engine.midiCollector, juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100));
        addMessageToQueueAt(engine.midiCollector, juce::MidiMessage::controllerEvent(1, 1, 127));
        auto output = renderLive(engine, 19200);
        output.erase(output.begin(), output.begin() + 4800);
        require(std::abs(medianF0(output) - 440.0) / 440.0 < 0.03, "mod wheel detuned center");
        require(std::any_of(output.begin(), output.end(), [](float x) { return std::abs(x) > 0.02f; }),
                "mod wheel silenced the voice");
    }
    // Sustain pedal holds the note past note-off, releases on pedal-up.
    {
        sonora::AudioEngine engine;
        engine.prepare(rate);
        require(engine.submit(sonora::ProjectState()), "empty project rejected");
        auto& collector = engine.midiCollector;
        addMessageToQueueAt(collector, juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100));
        addMessageToQueueAt(collector, juce::MidiMessage::controllerEvent(1, 64, 127));
        addMessageToQueueAt(collector, juce::MidiMessage::noteOff(1, 60));
        auto held = renderLive(engine, 19200);
        float heldPeak = 0.0f;
        for (auto x : held)
            heldPeak = std::max(heldPeak, std::abs(x));
        require(heldPeak > 0.03f, "sustain pedal did not hold the note");
        addMessageToQueueAt(collector, juce::MidiMessage::controllerEvent(1, 64, 0));
        auto released = renderLive(engine, 48000);
        float tailPeak = 0.0f;
        for (std::size_t i = 24000; i < released.size(); ++i)
            tailPeak = std::max(tailPeak, std::abs(released[i]));
        require(tailPeak < 0.005f, "note stuck after sustain release");
    }
}

void testVariations()
{
    auto original = fixture();
    original.melodies[1].count = 2;
    original.melodies[1].notes[0] = { 11, 0, 480, 64, 100 };
    original.melodies[1].notes[1] = { 12, 960, 480, 67, 90 };
    original.melodies[3].count = 1;
    original.melodies[3].notes[0] = { 13, 0, 240, 71, 110 };
    original.drumPatterns[2].steps[0][0] = 120;
    original.drumPatterns[2].steps[4][16] = 100;
    original.song.melodyPattern = { 3, 2, 1, 0, 0, 1, 2, 3 };
    original.song.drumPattern = { 0, 0, 2, 0, 2, 0, 0, 0 };
    original.song.sections = 4;
    sonora::ProjectState loaded;
    const auto json = sonora::ProjectIO::encode(original);
    require(json.contains("\"version\": 8"), "variation projects must save as v8");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == original,
            "variation round-trip changed slots or indices");

    // Version 5 documents migrate with slot A content and slot-A sections.
    auto legacy = downshapeToVersion(juce::JSON::parse(json), 5);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk(), "v5 migration failed");
    require(loaded.melodies[0] == original.melodies[0] && loaded.drumPatterns[0] == original.drumPatterns[0],
            "v5 migration changed slot A");
    require(loaded.melodies[1].count == 0 && loaded.drumPatterns[2].hitCount() == 0,
            "v5 migration invented slot content");
    for (int i = 0; i < sonora::maxSections; ++i)
        require(loaded.song.melodyPattern[static_cast<std::size_t>(i)] == 0
                && loaded.song.drumPattern[static_cast<std::size_t>(i)] == 0,
                "v5 migration invented section slots");
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(loaded), loaded).wasOk(),
            "migrated v5 re-save failed");

    auto reject = [&](auto change, const char* what) {
        auto document = juce::JSON::parse(json);
        change(document);
        loaded = original;
        require(sonora::ProjectIO::decode(juce::JSON::toString(document), loaded).failed(), what);
        require(loaded == original, "bad variation project destroyed current state");
    };
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("song").getDynamicObject()
            ->getProperty("melodyPatterns").getArray()->set(3, 9);
    }, "out-of-range slot index accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
            .getDynamicObject()->getProperty("patterns").getArray()->removeLast();
    }, "short pattern library accepted");
    reject([](auto& document) {
        // Force slot B's second note onto the first: same pitch, overlapping time.
        auto* second = document.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
                           .getDynamicObject()->getProperty("patterns").getArray()->getReference(1)
                           .getArray()->getReference(1).getDynamicObject();
        second->setProperty("start", 240);
        second->setProperty("pitch", 64);
    }, "overlapping slot-B notes accepted");
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("tracks").getArray()->getReference(1)
            .getDynamicObject()->setProperty("kitVariant", 9);
    }, "unknown kit variant accepted");
}

void testKitVariants()
{
    // Factory variants are deterministic, bounded, audible, and distinct.
    std::array<sonora::SampleBank, sonora::numKitVariants> kits {
        sonora::buildStarterBank(sonora::kitVariantParams(0)),
        sonora::buildStarterBank(sonora::kitVariantParams(1)),
        sonora::buildStarterBank(sonora::kitVariantParams(2))
    };
    require(kits[0].data[0] == sonora::buildStarterBank().data[0], "default variant changed the starter kit");
    for (int variant = 0; variant < sonora::numKitVariants; ++variant)
    {
        for (int pad = 0; pad < sonora::drumPads; ++pad)
        {
            const auto& data = kits[static_cast<std::size_t>(variant)].data[static_cast<std::size_t>(pad)];
            require(!data.empty(), "variant pad empty");
            float peak = 0.0f;
            for (const auto value : data)
            {
                require(std::isfinite(value) && std::abs(value) <= 1.0f, "variant sample out of range");
                peak = std::max(peak, std::abs(value));
            }
            require(peak > 0.1f, "variant pad silent");
        }
    }
    // Deep is lower/slower than Starter on the kick; Crisp is brighter.
    const auto& starter = kits[0].data[0];
    const auto& deep = kits[1].data[0];
    const auto& crisp = kits[2].data[0];
    require(deep.size() > starter.size() && crisp.size() < starter.size(), "variant lengths wrong");
    auto zeroCrossings = [](const std::vector<float>& data) {
        int flips = 0;
        for (std::size_t i = 1; i < data.size(); ++i)
            if ((data[i - 1] < 0.0f) != (data[i] < 0.0f))
                ++flips;
        return flips / (data.size() / 48000.0);
    };
    require(zeroCrossings(deep) < zeroCrossings(starter), "Deep kit not lower than Starter");
    // Variant persistence round-trips through the drum track field.
    auto project = fixture();
    project.kitVariant = 2;
    sonora::ProjectState loaded;
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(project), loaded).wasOk()
            && loaded.kitVariant == 2, "kit variant round-trip failed");
    project.kitVariant = 7;
    require(!project.valid(), "wild variant accepted by validation");
}

void testKitPresets()
{
    const auto base = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("sonora-kits", "");
    require(base.createDirectory().wasOk(), "preset base creation failed");
    const auto media = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getNonexistentChildFile("sonora-preset-media", "");
    require(media.createDirectory().wasOk(), "preset media creation failed");
    // Fixture sample to store in the preset.
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
            new juce::FileOutputStream(media.getChildFile("boom.wav")), 48000.0, 1, 16, {}, 0));
        require(writer != nullptr, "preset fixture write failed");
        juce::AudioBuffer<float> sine(1, 4800);
        for (int i = 0; i < 4800; ++i)
            sine.setSample(0, i, 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * 90.0 * i / 48000.0));
        require(writer->writeFromAudioSampleBuffer(sine, 0, 4800), "preset fixture samples failed");
    }
    std::array<juce::String, sonora::drumPads> files {};
    files[0] = "boom.wav";
    require(sonora::listKitPresets(base).empty(), "preset list not empty");
    require(sonora::saveKitPreset(base, "My Kit!", 1, files,
                                       [&](const juce::String& f) { return media.getChildFile(f); }).wasOk(),
            "preset save failed");
    // Name sanitized to a safe folder; starter pads store empty names.
    const auto saved = sonora::listKitPresets(base);
    require(saved.size() == 1 && saved[0] == "My_Kit", "preset not listed under sanitized name");
    sonora::KitPreset preset;
    require(sonora::loadKitPreset(base, "My_Kit", preset).wasOk(), "preset load failed");
    require(preset.name == "My_Kit" && preset.variant == 1 && preset.files[0] == "boom.wav"
            && preset.files[1].isEmpty(), "preset content wrong");
    // Loading twice, bad names, and missing samples are all handled.
    require(sonora::loadKitPreset(base, "Nope", preset).failed(), "missing preset loaded");
    require(sonora::saveKitPreset(base, "   ", 0, files,
                                       [&](const juce::String& f) { return media.getChildFile(f); }).failed(),
            "blank preset name accepted");
    require(sonora::saveKitPreset(base, "Bad", 9, files,
                                       [&](const juce::String& f) { return media.getChildFile(f); }).failed(),
            "wild variant preset accepted");
    media.getChildFile("boom.wav").deleteFile();
    require(sonora::saveKitPreset(base, "Broken", 0, files,
                                       [&](const juce::String& f) { return media.getChildFile(f); }).failed(),
            "preset saved without samples");
    require(sonora::deleteKitPreset(base, "My_Kit").wasOk(), "preset delete failed");
    require(sonora::listKitPresets(base).empty(), "deleted preset still listed");
    require(sonora::deleteKitPreset(base, "My_Kit").failed(), "double delete accepted");
    // Tampered JSON is rejected.
    require(sonora::saveKitPreset(base, "Good", 0, {},
                                       [&](const juce::String& f) { return media.getChildFile(f); }).wasOk(),
            "empty preset save failed");
    const auto jsonFile = base.getChildFile("Good/kit.json");
    require(jsonFile.replaceWithText("{broken"), "tamper setup failed");
    require(sonora::loadKitPreset(base, "Good", preset).failed(), "corrupt preset loaded");
    require(base.deleteRecursively() && media.deleteRecursively(), "preset test cleanup failed");
}

void testKitPanelLogic()
{
    // KitPanel is a MainComponent nested type; exercise row text/bounds logic
    // through the same helpers the panel uses.
    sonora::ProjectState project;
    sonora::setPadSampleName(project.padSamples[2], "hat.wav");
    for (int pad = 0; pad < sonora::drumPads; ++pad)
    {
        const auto file = sonora::padSampleName(project.padSamples[static_cast<std::size_t>(pad)]);
        if (pad == 2)
            require(file == "hat.wav", "pad filename helper broken");
        else
            require(file.isEmpty(), "pad slot not empty by default");
    }
}

void testKitSamples()
{
    // Fixture: a 440 Hz sine WAV at 44100 Hz in a fake media folder.
    const auto media = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getNonexistentChildFile("sonora-kit-media", "");
    require(media.createDirectory().wasOk(), "media dir creation failed");
    {
        juce::WavAudioFormat takeFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(takeFormat.createWriterFor(
            new juce::FileOutputStream(media.getChildFile("my-kick.wav")), 44100.0, 1, 16, {}, 0));
        require(writer != nullptr, "sample fixture write failed");
        juce::AudioBuffer<float> sine(1, 22050);
        for (int i = 0; i < 22050; ++i)
            sine.setSample(0, i, 0.8f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0));
        require(writer->writeFromAudioSampleBuffer(sine, 0, 22050), "sample fixture samples failed");
    }
    std::array<juce::String, sonora::drumPads> files {};
    files[0] = "my-kick.wav";
    auto bank = sonora::loadSampleBank(files, media, 48000.0, 0);
    require(bank != nullptr, "bank load failed");
    require(bank->sampleRate == 48000.0, "bank rate wrong");
    // Custom kick: resampled to 48 kHz, peak-normalized to the kick target.
    const auto& kick = bank->data[0];
    require(std::abs(static_cast<int>(kick.size()) - 24000) < 64, "resampled length wrong");
    float peak = 0.0f;
    for (const auto value : kick)
        peak = std::max(peak, std::abs(value));
    require(std::abs(peak - 0.65f) < 0.03f, "custom sample not normalized to pad target");
    // Pitch survives the resample: ~440 zero crossings per... count sign flips.
    int flips = 0;
    for (std::size_t i = 1; i < kick.size(); ++i)
        if ((kick[i - 1] < 0.0f) != (kick[i] < 0.0f))
            ++flips;
    const double measuredHz = flips / 2.0 / (kick.size() / 48000.0);
    require(std::abs(measuredHz - 440.0) < 8.0, "resample detuned the sample");
    // Untouched pads equal the deterministic starters.
    const auto starters = sonora::buildStarterBank();
    for (int pad = 1; pad < sonora::drumPads; ++pad)
        require(bank->data[static_cast<std::size_t>(pad)] == starters.data[static_cast<std::size_t>(pad)],
                "starter pad altered by bank load");
    // Missing files fall back to the starter instead of going silent.
    files[3] = "no-such-file.wav";
    auto fallback = sonora::loadSampleBank(files, media, 48000.0, 0);
    require(fallback != nullptr && fallback->data[3] == starters.data[3], "missing file not starter");
    require(media.deleteRecursively(), "media dir cleanup failed");

    // Engine: an injected bank renders custom content; retiring to null
    // restores the starters.
    sonora::AudioEngine engine;
    engine.prepare(48000.0);
    auto project = fixture();
    project.melodies[0] = {};
    require(engine.submit(project), "kit project rejected");
    auto custom = std::make_unique<sonora::SampleBank>();
    custom->sampleRate = 48000.0;
    custom->data[0].assign(4800, 0.0f);
    for (int i = 0; i < 4800; ++i)
        custom->data[0][static_cast<std::size_t>(i)] = 0.5f;
    engine.retirePadBank(custom.get());
    engine.setPlaying(true);
    juce::AudioBuffer<float> buffer(2, 512);
    float loudest = 0.0f;
    for (int i = 0; i < 6; ++i)
    {
        engine.process({ &buffer, 0, 512 });
        loudest = std::max(loudest, buffer.getMagnitude(0, 512));
    }
    // The fixture's bar-0 kick (step 0, velocity 110) must carry the DC block.
    require(loudest > 0.05f, "custom bank silent in engine");
    engine.retirePadBank(nullptr);
    engine.process({ &buffer, 0, 512 });
}

void testKitPersistence()
{
    auto original = fixture();
    sonora::setPadSampleName(original.padSamples[0], "my-kick.wav");
    sonora::setPadSampleName(original.padSamples[7], "shaker-loop.flac");
    original.kitVariant = 1;
    sonora::ProjectState loaded;
    const auto json = sonora::ProjectIO::encode(original);
    require(json.contains("\"version\": 8"), "kit projects must save as v8");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == original,
            "kit round-trip changed pad samples");
    // Version 6 documents migrate to the built-in kit.
    auto legacy = downshapeToVersion(juce::JSON::parse(json), 6);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk(), "v6 migration failed");
    for (const auto& slot : loaded.padSamples)
        require(sonora::padSampleName(slot).isEmpty(), "v6 migration invented custom samples");
    require(loaded.kitVariant == 0, "v6 migration invented a variant");
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(loaded), loaded).wasOk(),
            "migrated v6 re-save failed");
    auto reject = [&](auto change, const char* what) {
        auto document = juce::JSON::parse(json);
        change(document);
        loaded = original;
        require(sonora::ProjectIO::decode(juce::JSON::toString(document), loaded).failed(), what);
        require(loaded == original, "bad kit project destroyed current state");
    };
    reject([](auto& document) {
        document.getDynamicObject()->getProperty("tracks").getArray()->getReference(1)
            .getDynamicObject()->getProperty("samples").getArray()->removeLast();
    }, "short sample list accepted");
    reject([](auto& document) {
        juce::String tooLong;
        for (int i = 0; i < 300; ++i)
            tooLong += "x";
        document.getDynamicObject()->getProperty("tracks").getArray()->getReference(1)
            .getDynamicObject()->getProperty("samples").getArray()->set(0, tooLong);
    }, "overlong sample name accepted");
}

void testInstances()
{
    // Shared sections detach into a free slot with identical content.
    sonora::Arrangement song;
    song.sections = 4;
    std::array<sonora::Pattern, sonora::numPatterns> library {};
    library[0].count = 1;
    library[0].notes[0] = { 1, 0, 240, 60, 100 };
    require(sonora::makeSectionUnique(song, library, 2), "shared section not detached");
    require(song.melodyPattern[2] == 1, "detached section points at wrong slot");
    require(library[1] == library[0], "detached content differs from source");
    require(!sonora::makeSectionUnique(song, library, 2), "unique section detached again");
    require(!sonora::makeSectionUnique(song, library, 9), "out-of-range section detached");
    // Detached content is independent: editing the copy spares the source.
    library[1].notes[0].pitch = 64;
    require(library[0].notes[0].pitch == 60, "detach shares storage with source");
    // Full library: every slot in use somewhere refuses the detach.
    song.melodyPattern = { 0, 1, 2, 3, 0, 1, 2, 3 };
    song.sections = 5;
    song.melodyPattern[4] = 0; // section 0 shared, yet no free slot anywhere
    const auto before = library;
    require(!sonora::makeSectionUnique(song, library, 0), "detach succeeded with full library");
    require(library == before, "refused detach mutated the library");
    std::array<sonora::DrumPattern, sonora::numPatterns> grids {};
    grids[0].steps[0][0] = 100;
    sonora::Arrangement drums;
    drums.sections = 2;
    require(sonora::makeDrumSectionUnique(drums, grids, 1), "shared drum section not detached");
    require(drums.drumPattern[1] == 1 && grids[1].steps[0][0] == 100, "drum detach wrong");
    require(!sonora::makeDrumSectionUnique(drums, grids, 1), "unique drum section detached again");
    drums.sections = 5;
    drums.drumPattern = { 0, 1, 2, 3, 0, 0, 0, 0 };
    const auto gridsBefore = grids;
    require(!sonora::makeDrumSectionUnique(drums, grids, 0), "drum detach succeeded with full library");
    require(grids == gridsBefore, "refused drum detach mutated the library");
    // Sharing queries drive the "shared by N" indicators.
    song.sections = 8;
    const auto shared = sonora::sectionsSharingSlot(song, false, 0);
    require(shared.size() == 2 && shared[0] == 1 && shared[1] == 5, "sharing query wrong");
    require(sonora::sectionsSharingSlot(song, true, 3).empty(), "unused slot reports sharers");
}

int main()
{
    try
    {
        testTiming(); std::cout << "PASS sample-accurate looping, boundaries, chase, tempo\n";
                testFx(); std::cout << "PASS fx transparency, EQ/comp/delay/verb/limiter behavior, validation\n";
        testFxPersistence(); std::cout << "PASS v4 fx round-trip, v3 migration, malformed fx, effected export\n";
        testTakePersistence(); std::cout << "PASS v5 take round-trip, v4 migration, malformed takes, latency math\n";
        testRecorder(); std::cout << "PASS FIFO recorder write/read integrity, overruns, validation\n";
        testTakePlayback(); std::cout << "PASS song-mode take offset/level, mute, loop-mode silence\n";
        testVariations(); std::cout << "PASS pattern library round-trip, v5 migration, malformed slots\n";
        testInstances(); std::cout << "PASS make-unique detach, full-library refusal, sharing queries\n";
        testKitPanelLogic(); std::cout << "PASS kit panel helpers\n";
        testKitSamples(); std::cout << "PASS sample import, resample, normalize, fallback, engine bank swap\n";
        testKitPersistence(); std::cout << "PASS v7 kit round-trip, v6 migration, malformed sample lists\n";
        testKitVariants(); std::cout << "PASS factory kit variants, persistence, validation\n";
        testKitPresets(); std::cout << "PASS preset save/load/delete/browse, sanitize, corrupt rejection\n";
        testPitch(); std::cout << "PASS YIN accuracy, scale snap, correction, identity, vibrato, validation\n";
        testMidiHardware(); std::cout << "PASS Arturia pad map, MCU transport, auto-connect, status text\n";
        testExpression(); std::cout << "PASS pitch bend, mod vibrato, sustain hold/release\n";
        testPersistence(); std::cout << "PASS project round-trip, validation, atomic replacement\n";
        testDrumPersistence(); std::cout << "PASS version-1 migration, drum/mixer persistence, malformed tracks\n";
        testArrangement(); std::cout << "PASS section gating, song end, v2/v3 persistence, engine stop\n";
        testQueue(); std::cout << "PASS bounded snapshot queue and concurrent transfer\n";
        testAudio(); std::cout << "PASS audible offline render, callback invariance, stop cleanup\n";
        testDrumAudio(); std::cout << "PASS sampled drums, hat choke, resampling, mute/solo, pad audition\n";
        testExport(); std::cout << "PASS loop/song bounce, live parity, normalize, cancel, WAV round-trip\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
