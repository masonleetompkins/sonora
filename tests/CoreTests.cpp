#include "AudioEngine.h"
#include "AudioTakes.h"
#include "Export.h"
#include "Fx.h"
#include "KitSamples.h"
#include "KnobMaps.h"
#include "LiveFx.h"
#include "AgentActions.h"
#include "AiAgent.h"
#include "InstrumentSearch.h"
#include "Sampler.h"
#include "TimeStretch.h"
#include "AiMelody.h"
#include "AiDictation.h"
#include "IdeaCapture.h"
#include "MiniLabDisplay.h"
#include "MiniLabSender.h"
#include "OmarchyTheme.h"
#include "PitchCorrect.h"
#include "ProjectIO.h"
#include <algorithm>
#include <iostream>
#include <memory>
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
// migration paths decode realistic legacy files (not just relabeled v9).
// Files before v13 store 8 arrangement rows and no part types.
inline void toPreV13Song(juce::var& document)
{
    auto* song = document.getDynamicObject()->getProperty("song").getDynamicObject();
    for (const char* key : { "slots", "cells" })
        if (auto* rows = song->getProperty(key).getArray())
            rows->removeRange(sonora::legacyMaxSections, rows->size());
    song->removeProperty("parts");
}

inline juce::var downshapeToVersion(juce::var document, int version)
{
    auto* root = document.getDynamicObject();
    auto* tracks = root->getProperty("tracks").getArray();
    auto* melody9 = tracks->getReference(0).getDynamicObject();
    auto* drums9 = tracks->getReference(1).getDynamicObject();
    auto* song9 = root->getProperty("song").getDynamicObject();
    if (version <= 2)
    {
        root->removeProperty("song");
        root->removeProperty("songMode");
    }
    else
    {
        juce::Array<juce::var> melodyFlags, drumFlags;
        for (int s = 0; s < sonora::legacyMaxSections; ++s)
        {
            melodyFlags.add(song9->getProperty("cells").getArray()->getReference(s)
                                .getArray()->getReference(0));
            drumFlags.add(song9->getProperty("cells").getArray()->getReference(s)
                              .getArray()->getReference(1));
        }
        song9->setProperty("melody", melodyFlags);
        song9->setProperty("drums", drumFlags);
        if (version >= 6)
        {
            juce::Array<juce::var> melodySlots, drumSlots;
            for (int s = 0; s < sonora::legacyMaxSections; ++s)
            {
                melodySlots.add(song9->getProperty("slots").getArray()->getReference(s)
                                    .getArray()->getReference(0));
                drumSlots.add(song9->getProperty("slots").getArray()->getReference(s)
                                  .getArray()->getReference(1));
            }
            song9->setProperty("melodyPatterns", melodySlots);
            song9->setProperty("drumPatterns", drumSlots);
        }
        song9->removeProperty("slots");
        song9->removeProperty("cells");
        song9->removeProperty("parts");
    }
    // Legacy two-track list rebuilt from slots 0 and 1.
    auto legacyMelody = std::make_unique<juce::DynamicObject>();
    legacyMelody->setProperty("id", 1);
    legacyMelody->setProperty("instrument", "sonora.sine-keys.v1");
    legacyMelody->setProperty("volume", melody9->getProperty("volume"));
    legacyMelody->setProperty("mute", melody9->getProperty("mute"));
    legacyMelody->setProperty("solo", melody9->getProperty("solo"));
    if (version <= 5)
        legacyMelody->setProperty("notes", melody9->getProperty("patterns").getArray()->getReference(0));
    else
        legacyMelody->setProperty("patterns", melody9->getProperty("patterns"));
    if (version >= 4)
        legacyMelody->setProperty("fx", melody9->getProperty("fx"));
    auto legacyDrums = std::make_unique<juce::DynamicObject>();
    legacyDrums->setProperty("id", 2);
    legacyDrums->setProperty("instrument", "sonora.starter-kit.v1");
    legacyDrums->setProperty("volume", drums9->getProperty("volume"));
    legacyDrums->setProperty("mute", drums9->getProperty("mute"));
    legacyDrums->setProperty("solo", drums9->getProperty("solo"));
    if (version <= 5)
        legacyDrums->setProperty("steps", drums9->getProperty("grids").getArray()->getReference(0));
    else
        legacyDrums->setProperty("grids", drums9->getProperty("grids"));
    if (version >= 4)
        legacyDrums->setProperty("fx", drums9->getProperty("fx"));
    if (version >= 7)
        legacyDrums->setProperty("samples", drums9->getProperty("samples"));
    if (version >= 8)
        legacyDrums->setProperty("kitVariant", drums9->getProperty("kitVariant"));
    juce::Array<juce::var> legacyTracks;
    legacyTracks.add(juce::var(legacyMelody.release()));
    legacyTracks.add(juce::var(legacyDrums.release()));
    root->setProperty("tracks", legacyTracks);
    if (version <= 4)
        root->removeProperty("takes");
    if (version <= 3)
        root->removeProperty("master");
    root->setProperty("version", version);
    return document;
}

sonora::ProjectState fixture()
{
    sonora::ProjectState project = sonora::defaultProject();
    project.bpm = 123.0;
    project.tracks[0].melodies[0].count = 4;
    project.tracks[0].melodies[0].notes[0] = { 1, 0, 960, 60, 100 };
    project.tracks[0].melodies[0].notes[1] = { 2, 960, 240, 60, 90 };
    project.tracks[0].melodies[0].notes[2] = { 3, 240, 4320, 67, 110 };
    project.tracks[0].melodies[0].notes[3] = { 4, sonora::patternTicks - 240, 240, 60, 100 };
    for (int step = 0; step < sonora::gridSteps; step += 2)
        project.tracks[1].drumPatterns[0].steps[2][static_cast<std::size_t>(step)] = step % 4 == 0 ? 100 : 70;
    for (int step : { 0, 16, 32, 48 })
        project.tracks[1].drumPatterns[0].steps[0][static_cast<std::size_t>(step)] = 110;
    for (int step : { 4, 12, 20, 28, 36, 44, 52, 60 })
        project.tracks[1].drumPatterns[0].steps[1][static_cast<std::size_t>(step)] = 100;
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
        scheduler.scheduleDrums(project.tracks[1].drumPatterns[0], count, [&](int pad, std::uint8_t, int offset) {
            require(offset >= 0 && offset < count, "drum event outside callback");
            events.push_back({ start + offset, sonora::drumBaseNote + pad, true });
        });
        scheduler.process(project.tracks[0].melodies[0], count, false, [&](const sonora::Note& note, bool on, int offset) {
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
    scheduler.process(project.tracks[0].melodies[0], 1000, false, [](const auto&, bool, int) {});
    int chased = 0;
    scheduler.process(project.tracks[0].melodies[0], 1, true, [&](const auto& note, bool on, int offset) {
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

void testGroove()
{
    // Swing helper: even 16ths stay, odd 16ths delay, zero swing is identity.
    require(sonora::swingTicks(0, 0.0f) == 0, "swing changed an even step");
    require(sonora::swingTicks(4 * sonora::stepTicks, 0.5f) == 4 * sonora::stepTicks,
            "swing changed an even step");
    require(sonora::swingTicks(sonora::stepTicks, 0.5f)
                == sonora::stepTicks + sonora::stepTicks / 2,
            "swing delay wrong");
    require(sonora::swingTicks(5 * sonora::stepTicks, 0.0f) == 5 * sonora::stepTicks,
            "zero swing not identity");

    // Quantize: full strength snaps to grid, zero leaves untouched, partial
    // interpolates, and the result is always a valid pattern.
    sonora::Pattern loose;
    loose.count = 3;
    loose.notes[0] = { 1, 100, 240, 60, 100 };
    loose.notes[1] = { 2, 500, 240, 62, 100 };
    loose.notes[2] = { 3, 300, 240, 60, 100 };
    auto untouched = loose;
    sonora::quantizePattern(loose, 0.0f);
    require(loose == untouched, "zero-strength quantize moved notes");
    sonora::quantizePattern(loose, 1.0f);
    require(loose.valid(), "quantized pattern invalid");
    for (int i = 0; i < loose.count; ++i)
        require(loose.notes[static_cast<std::size_t>(i)].start % sonora::stepTicks == 0,
                "quantize missed the grid");
    auto half = untouched;
    sonora::quantizePattern(half, 0.5f);
    require(half.valid(), "half quantize invalid");
    require(half.notes[0].start > 0 && half.notes[0].start < sonora::stepTicks,
            "half quantize did not interpolate");

    // Humanize is seeded (same seed, same result) and stays valid; drums
    // keep their steps and only gain velocity wander.
    auto human = untouched;
    sonora::humanizePattern(human, 0.5f, 0.5f, 1234u);
    auto humanAgain = untouched;
    sonora::humanizePattern(humanAgain, 0.5f, 0.5f, 1234u);
    require(human == humanAgain, "humanize not reproducible");
    require(human.valid(), "humanized pattern invalid");
    require(!(human == untouched), "humanize changed nothing");
    sonora::DrumPattern drums;
    drums.steps[0][0] = 100;
    drums.steps[1][16] = 90;
    auto drumsAgain = drums;
    sonora::humanizeDrums(drums, 0.5f, 99u);
    require(drums.valid(), "humanized drums invalid");
    require(drums.steps[0][0] != 100 || drums.steps[1][16] != 90, "drum humanize changed nothing");
    sonora::humanizeDrums(drumsAgain, 0.0f, 99u);
    require(drumsAgain.steps[0][0] == 100, "zero humanize touched drums");

    // Scheduler: swung odd steps land late in both loop and song mode.
    {
        sonora::LoopScheduler scheduler;
        scheduler.configure(48000.0, 120.0);
        sonora::DrumPattern kick;
        kick.steps[0][0] = 100;
        kick.steps[0][1] = 100;
        std::vector<int> straight, swung;
        scheduler.scheduleDrumsAt(kick, 200000, 0, [&](int, std::uint8_t, int offset) {
            straight.push_back(offset);
        });
        scheduler.scheduleDrumsAt(kick, 200000, 0, [&](int, std::uint8_t, int offset) {
            swung.push_back(offset);
        }, 0.5f);
        require(straight.size() == 2 && swung.size() == 2, "swing dropped hits");
        require(swung[0] == straight[0], "swing moved an even step");
        // 0.5 swing delays the offbeat by half a 16th: 120 ticks at 25 frames
        // per tick (48 kHz, 120 BPM).
        require(swung[1] - straight[1] == 120 * 25, "swing offset wrong");
    }

    // Engine: a lone offbeat hit renders a full swung delay later.
    {
        auto render = [](float swing) {
            auto engineStorage = std::make_unique<sonora::AudioEngine>();
            auto& engine = *engineStorage;
            engine.prepare(48000.0);
            auto project = fixture();
            project.tracks[0].melodies[0] = {};
            project.tracks[1].drumPatterns[0] = {};
            project.tracks[1].drumPatterns[0].steps[0][1] = 110;
            project.tracks[1].swing = swing;
            require(engine.submit(project), "groove project rejected");
            engine.setPlaying(true);
            juce::AudioBuffer<float> buffer(2, 512);
            int firstAudible = -1;
            for (int i = 0; i < 60; ++i)
            {
                buffer.clear();
                engine.process({ &buffer, 0, 512 });
                if (firstAudible < 0 && buffer.getMagnitude(0, 512) > 0.02f)
                    firstAudible = i;
            }
            return firstAudible;
        };
        const int straight = render(0.0f), swung = render(0.5f);
        require(straight >= 0 && swung >= 0, "groove probe silent");
        // Step 1 sits at 6000 samples (block 11); +3000 swung samples lands
        // six blocks later.
        require(straight == 11, "straight offbeat at wrong block");
        require(swung == straight + 6, "engine swing delay wrong");
    }
}

void testLiveFx()
{
    using sonora::ArpMode;
    using sonora::ArpRate;
    constexpr double rate = 48000.0, bpm = 120.0; // 1/16 = 6000 samples.
    auto ons = [](const std::vector<sonora::FxEvent>& events) {
        std::vector<int> pitches;
        for (const auto& e : events)
            if (e.on)
                pitches.push_back(e.pitch);
        return pitches;
    };
    // Idle FX passes nothing through (the engine routes originals itself).
    sonora::LiveArp idle;
    require(idle.process(0, 512, { { 0, true, 60, 100 } }, rate, bpm).empty(), "idle FX emitted");

    // One-finger chord: C stamps C major, and the shared tone survives the
    // first root's release when G major still holds it.
    sonora::LiveArp chords;
    sonora::LiveFx chordFx;
    chordFx.chordOn = true;
    chords.setParams(chordFx);
    require(ons(chords.process(0, 512, { { 0, true, 60, 90 } }, rate, bpm))
                == std::vector<int>({ 60, 64, 67 }),
            "C major stamp wrong");
    require(ons(chords.process(512, 512, { { 0, true, 67, 80 } }, rate, bpm))
                == std::vector<int>({ 67, 71, 74 }),
            "G major stamp wrong");
    auto release = chords.process(1024, 512, { { 0, false, 60, 0 } }, rate, bpm);
    std::vector<int> offs;
    for (const auto& e : release)
        if (!e.on)
            offs.push_back(e.pitch);
    require(offs == std::vector<int>({ 60, 64 }), "shared chord tone cut early");
    // Velocity follows the pressed key.
    sonora::LiveArp vel;
    vel.setParams(chordFx);
    const auto stamped = vel.process(0, 512, { { 0, true, 60, 90 } }, rate, bpm);
    require(!stamped.empty() && stamped.front().velocity == 90, "chord lost velocity");

    // Arp up over C-E-G at 1/16: 60, 64, 67, 60 across four steps.
    sonora::LiveArp up;
    sonora::LiveFx upFx;
    upFx.arp = ArpMode::Up;
    up.setParams(upFx);
    const auto arpOut = up.process(0, 24000,
                                   { { 0, true, 60, 100 }, { 0, true, 64, 100 }, { 0, true, 67, 100 } },
                                   rate, bpm);
    require(ons(arpOut) == std::vector<int>({ 60, 64, 67, 60 }), "arp up order wrong");
    // Full release answers at the release offset, not the next step.
    const auto hush = up.process(24000, 6000, { { 100, false, 60, 0 }, { 100, false, 64, 0 },
                                                { 100, false, 67, 0 } },
                                 rate, bpm);
    require(hush.size() == 1 && !hush.front().on && hush.front().offset == 100, "release not answered");

    // 1/8 at 120bpm steps every 12000 samples: two steps per 24000-sample block.
    sonora::LiveArp eighths;
    sonora::LiveFx eighthFx;
    eighthFx.arp = ArpMode::Up;
    eighthFx.rate = ArpRate::Eighth;
    eighths.setParams(eighthFx);
    require(ons(eighths.process(0, 24000, { { 0, true, 60, 100 }, { 0, true, 67, 100 } }, rate, bpm))
                == std::vector<int>({ 60, 67 }),
            "eighth rate mistimed");

    // Down starts at the top; up-down ping-pongs without repeating the ends.
    sonora::LiveArp down;
    sonora::LiveFx downFx;
    downFx.arp = ArpMode::Down;
    down.setParams(downFx);
    require(ons(down.process(0, 12000, { { 0, true, 60, 100 }, { 0, true, 67, 100 } }, rate, bpm))
                == std::vector<int>({ 67, 60 }),
            "arp down start wrong");
    sonora::LiveArp pingpong;
    sonora::LiveFx pongFx;
    pongFx.arp = ArpMode::UpDown;
    pingpong.setParams(pongFx);
    require(ons(pingpong.process(0, 36000,
                                 { { 0, true, 60, 100 }, { 0, true, 64, 100 }, { 0, true, 67, 100 } },
                                 rate, bpm))
                == std::vector<int>({ 60, 64, 67, 64, 60, 64 }),
            "arp up-down wrong");

    // Two octaves double the pool; latch holds past release and restarts fresh.
    sonora::LiveArp wide;
    sonora::LiveFx wideFx;
    wideFx.arp = ArpMode::Up;
    wideFx.octaves = 2;
    wide.setParams(wideFx);
    require(ons(wide.process(0, 18000, { { 0, true, 60, 100 } }, rate, bpm))
                == std::vector<int>({ 60, 72, 60 }),
            "arp octaves wrong");
    sonora::LiveArp latched;
    sonora::LiveFx latchFx;
    latchFx.arp = ArpMode::Up;
    latchFx.latch = true;
    latched.setParams(latchFx);
    latched.process(0, 6000, { { 0, true, 60, 100 } }, rate, bpm);
    require(latched.process(6000, 6000, { { 0, false, 60, 0 } }, rate, bpm).empty(),
            "latch cut the arp");
    require(ons(latched.process(12000, 6000, { { 0, true, 62, 100 } }, rate, bpm))
                == std::vector<int>({ 62 }),
            "latch did not restart fresh");

    // Chord tones join the arp pool when both are on.
    sonora::LiveArp both;
    sonora::LiveFx bothFx;
    bothFx.arp = ArpMode::Up;
    bothFx.chordOn = true;
    both.setParams(bothFx);
    require(ons(both.process(0, 24000, { { 0, true, 60, 100 } }, rate, bpm))
                == std::vector<int>({ 60, 64, 67, 60 }),
            "chord arp pool wrong");

    // releaseAll silences the current step note.
    sonora::LiveArp ringing;
    ringing.setParams(upFx);
    ringing.process(0, 6000, { { 0, true, 60, 100 }, { 0, true, 64, 100 } }, rate, bpm);
    const auto silenced = ringing.releaseAll();
    require(silenced.size() == 1 && !silenced.front().on && silenced.front().pitch == 60,
            "releaseAll missed the step");

    require(juce::String(sonora::arpModeName(ArpMode::UpDown)) == "Up-down", "arp mode name wrong");
    require(juce::String(sonora::arpRateName(ArpRate::SixteenthTriplet)) == "1/16T", "arp rate name wrong");
    require(std::abs(sonora::arpStepBeats(ArpRate::EighthTriplet) - 1.0 / 3.0) < 1.0e-9,
            "triplet math wrong");
    sonora::LiveFx bad;
    bad.octaves = 0;
    require(!bad.valid(), "zero octaves validated");
    bad = sonora::LiveFx {};
    bad.rate = sonora::ArpRate::numRates;
    require(!bad.valid(), "unknown rate validated");
    require(sonora::LiveFx {}.valid() && !sonora::LiveFx {}.active(), "default FX wrong");
}

void testChordTrack()
{
    namespace ai = sonora::ai;
    using sonora::ChordType;
    using sonora::SectionChord;
    // Labels, transpose math, validation.
    require(juce::String(sonora::chordLabel({ 9, ChordType::Minor })) == "Am", "Am label wrong");
    require(juce::String(sonora::chordLabel({ 6, ChordType::Dom7 })) == "F#7", "7 label wrong");
    require(juce::String(sonora::chordLabel({})) == "-", "empty chord label wrong");
    require(sonora::chordTranspose({ 2, ChordType::Major }, 0) == 2, "D-over-C shift wrong");
    require(sonora::chordTranspose({ 9, ChordType::Minor }, 2) == 7, "relative shift wrong");
    require(sonora::chordTranspose({}, 0) == 0, "empty chord transposes");
    require(!SectionChord { 12, ChordType::Major }.valid(), "root octave validated");
    require(!SectionChord { 0, ChordType::numChords }.valid(), "unknown type validated");

    // Section edits carry chords along.
    sonora::Arrangement song;
    song.sections = 3;
    song.chords[0] = { 0, ChordType::Major };
    song.chords[2] = { 7, ChordType::Minor };
    require(song.insertSection(1)
                && song.chords[0].root == 0 && !song.chords[1].set() && !song.chords[2].set()
                && song.chords[3].root == 7,
            "insert dropped chords");
    require(song.removeSection(0) && !song.chords[0].set() && !song.chords[1].set()
                && song.chords[2].root == 7,
            "remove dropped chords");
    require(song.moveSection(2, 0) && song.chords[0].root == 7 && !song.chords[2].set(),
            "move dropped chords");
    sonora::Arrangement dup;
    dup.sections = 2;
    dup.chords[0] = { 5, ChordType::Dom7 };
    require(dup.duplicateSection(0) && dup.chords[0].root == 5 && dup.chords[1].root == 5,
            "duplicate did not copy the chord");

    // Song playback transposes chorded sections; offs match their ons.
    auto project = fixture();
    project.song.sections = 2;
    project.song.chords[1] = { 2, ChordType::Major }; // D over C: +2
    sonora::LoopScheduler scheduler;
    scheduler.configure(48000.0, 120.0);
    const auto loopFrames = scheduler.lengthInSamples();
    std::vector<int> sectionOns[2];
    std::vector<int> sectionOffs[2];
    const bool done = scheduler.processSong(project.tracks, project.song, static_cast<int>(loopFrames) * 2,
                                            false,
                                            [&](int track, const sonora::Note& note, bool on, int offset) {
                                                require(track == 0, "drums leaked into song melody");
                                                (on ? sectionOns : sectionOffs)[offset < loopFrames ? 0 : 1]
                                                    .push_back(note.pitch);
                                            },
                                            0);
    require(done, "two-section song did not finish");
    require(sectionOns[0] == std::vector<int>({ 60, 60, 67, 60 }), "plain section altered");
    require(sectionOns[1] == std::vector<int>({ 62, 62, 69, 62 }), "chord section not transposed");
    require(sectionOffs[0] == std::vector<int>({ 60, 60, 67 }), "plain offs altered");
    // The boundary note's off lives in section 2 but keeps its onset shift.
    require(sectionOffs[1] == std::vector<int>({ 60, 62, 62, 69 }), "transposed off mismatched");

    // The AI sees the progression and the part chord.
    project.musicKey = 0;
    project.song.chords[0] = { 9, ChordType::Minor };
    ai::SongRequest songRequest;
    songRequest.project = project;
    songRequest.message = "Compose";
    const auto songMessage = ai::buildSongMessage(songRequest);
    require(songMessage.contains("[chord Am") && songMessage.contains("transpose 9"),
            "progression missing from song context");
    require(ai::songSystemPrompt().contains("chords of the sections they play in"),
            "chord rule missing from composer prompt");
    ai::AssistantRequest assistantRequest;
    assistantRequest.project = project;
    assistantRequest.track = 0;
    assistantRequest.part = 0;
    assistantRequest.message = "A hook";
    require(ai::buildAssistantMessage(assistantRequest).contains("chord Am"),
            "part chord missing from assistant context");
}

void testMixer()
{
    // Mix validation ranges.
    sonora::TrackMix mix;
    require(mix.valid(), "default mix rejected");
    mix.pan = -1.0f;
    mix.sendDelay = 1.0f;
    mix.sendReverb = 1.0f;
    require(mix.valid(), "edge mix rejected");
    mix.pan = 1.5f;
    require(!mix.valid(), "wide pan validated");
    mix.pan = 0.0f;
    mix.sendReverb = -0.1f;
    require(!mix.valid(), "negative send validated");
    sonora::ProjectSends sends;
    require(sends.valid(), "default sends rejected");
    sends.delayReturn = 1.5f;
    sends.reverbReturn = 1.5f;
    require(sends.valid(), "hot returns rejected");
    sends.delayReturn = 1.6f;
    require(!sends.valid(), "clipping return validated");

    // Hard-left pan silences the right channel exactly; center is symmetric.
    auto render = [](const sonora::ProjectState& project, int blocks) {
        auto engine = std::make_unique<sonora::AudioEngine>();
        engine->prepare(48000.0);
        require(engine->submit(project), "mix project rejected");
        engine->setPlaying(true);
        juce::AudioBuffer<float> buffer(2, 512);
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < blocks; ++i)
        {
            engine->process({ &buffer, 0, 512 });
            peakL = std::max(peakL, buffer.getMagnitude(0, 0, 512));
            peakR = std::max(peakR, buffer.getMagnitude(1, 0, 512));
        }
        return std::pair<float, float> { peakL, peakR };
    };
    auto project = fixture();
    project.tracks[1].mix.mute = true; // drums out; sine loop only
    project.songMode = false;
    const auto [centerL, centerR] = render(project, 200);
    require(centerL > 0.05f && centerR > 0.05f, "center mix too quiet");
    require(std::abs(centerL - centerR) < 0.01f, "center mix lopsided");
    project.tracks[0].mix.pan = -1.0f;
    const auto [leftL, leftR] = render(project, 200);
    require(leftL > 0.05f && leftR < 1.0e-6f, "hard-left leaks right");
    project.tracks[0].mix.pan = 0.0f;

    // A fed delay return changes the mix; an unfed one is bit-transparent.
    project.tracks[0].mix.sendDelay = 1.0f;
    project.sends.delay.timeMs = 100.0f;
    project.sends.delay.feedback = 0.0f;
    project.sends.delayReturn = 1.0f;
    auto dryProject = project;
    dryProject.tracks[0].mix.sendDelay = 0.0f;
    auto renderInto = [](const sonora::ProjectState& state, std::vector<float>& out) {
        auto engine = std::make_unique<sonora::AudioEngine>();
        engine->prepare(48000.0);
        require(engine->submit(state), "send project rejected");
        engine->setPlaying(true);
        juce::AudioBuffer<float> buffer(2, 512);
        for (int i = 0; i < 200; ++i)
        {
            engine->process({ &buffer, 0, 512 });
            for (int s = 0; s < 512; ++s)
                out.push_back(buffer.getSample(0, s));
        }
    };
    std::vector<float> dry, wet;
    renderInto(dryProject, dry);
    renderInto(project, wet);
    require(dry.size() == wet.size(), "render length mismatch");
    float diff = 0.0f;
    for (std::size_t i = 0; i < dry.size(); ++i)
        diff = std::max(diff, std::abs(dry[i] - wet[i]));
    require(diff > 0.01f, "delay send inaudible");
    // Return at zero with a live send stays bit-transparent past the ramp.
    project.sends.delayReturn = 0.0f;
    std::vector<float> gated;
    renderInto(project, gated);
    float tail = 0.0f;
    for (std::size_t i = 48000; i < dry.size(); ++i)
        tail = std::max(tail, std::abs(dry[i] - gated[i]));
    require(tail < 1.0e-6f, "zero return colours the mix");
}

void testAutomation()
{
    using sonora::AutomationTarget;
    // Curve evaluation.
    sonora::AutomationLane empty;
    require(empty.eval(500, 0.8f) == 0.8f, "empty lane ignored base");
    sonora::AutomationLane flat;
    flat.count = 1;
    flat.points[0] = { 0, 0.25f };
    require(flat.eval(0, 0.8f) == 0.25f && flat.eval(9999, 0.8f) == 0.25f, "single point not held");
    sonora::AutomationLane ramp;
    ramp.count = 2;
    ramp.points[0] = { 0, 0.0f };
    ramp.points[1] = { 100, 1.0f };
    require(ramp.eval(25, 9.0f) == 0.25f, "ramp interpolation wrong");
    require(ramp.eval(-5, 9.0f) == 0.0f && ramp.eval(101, 9.0f) == 1.0f, "ramp ends not held");
    sonora::AutomationLane bad;
    bad.count = 2;
    bad.points[0] = { 50, 0.0f };
    bad.points[1] = { 40, 1.0f };
    require(!bad.valid(), "unsorted lane validated");
    bad.count = sonora::maxAutomationPoints + 1;
    require(!bad.valid(), "overfull lane validated");
    require(juce::String(sonora::automationTargetName(AutomationTarget::DelayMix)) == "Delay mix",
            "target name wrong");

    // The engine follows lanes: gated volume, panned sends, wet delay throws.
    auto project = fixture();
    project.tracks[1].mix.mute = true; // drums out; sine loop only
    project.songMode = false;
    auto& volumeLane = project.tracks[0]
                           .automation[0][static_cast<std::size_t>(AutomationTarget::Volume)];
    volumeLane.count = 1;
    volumeLane.points[0] = { 0, 0.0f };
    auto gateEngine = std::make_unique<sonora::AudioEngine>();
    gateEngine->prepare(48000.0);
    require(gateEngine->submit(project), "gated project rejected");
    gateEngine->setPlaying(true);
    juce::AudioBuffer<float> buffer(2, 512);
    float tail = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        gateEngine->process({ &buffer, 0, 512 });
        if (i >= 30)
            tail = std::max({ tail, buffer.getMagnitude(0, 0, 512), buffer.getMagnitude(1, 0, 512) });
    }
    require(tail < 1.0e-4f, "volume lane did not gate the loop");
    // A delay-throw lane is audible against the dry mix.
    auto thrown = fixture();
    thrown.tracks[1].mix.mute = true;
    thrown.songMode = false;
    thrown.tracks[0].mix.sendDelay = 0.0f;
    auto& throwLane = thrown.tracks[0]
                          .automation[0][static_cast<std::size_t>(AutomationTarget::DelayMix)];
    throwLane.count = 1;
    throwLane.points[0] = { 0, 1.0f };
    thrown.tracks[0].fx.delay.timeMs = 100.0f;
    thrown.tracks[0].fx.delay.feedback = 0.0f;
    auto renderDryWet = [](const sonora::ProjectState& state, std::vector<float>& out) {
        auto mixEngine = std::make_unique<sonora::AudioEngine>();
        mixEngine->prepare(48000.0);
        require(mixEngine->submit(state), "throw project rejected");
        mixEngine->setPlaying(true);
        juce::AudioBuffer<float> mixBuffer(2, 512);
        for (int i = 0; i < 200; ++i)
        {
            mixEngine->process({ &mixBuffer, 0, 512 });
            for (int s = 0; s < 512; ++s)
                out.push_back(mixBuffer.getSample(0, s));
        }
    };
    auto dry = fixture();
    dry.tracks[1].mix.mute = true;
    dry.songMode = false;
    dry.tracks[0].fx.delay.timeMs = 100.0f;
    dry.tracks[0].fx.delay.feedback = 0.0f;
    std::vector<float> dryOut, wetOut;
    renderDryWet(dry, dryOut);
    renderDryWet(thrown, wetOut);
    float diff = 0.0f;
    for (std::size_t i = 0; i < dryOut.size(); ++i)
        diff = std::max(diff, std::abs(dryOut[i] - wetOut[i]));
    require(diff > 0.01f, "delay throw inaudible");
}

void testTimeStretch()
{
    // A 1-second 440 Hz sine doubled in time stays 440 Hz and twice as long.
    constexpr int rate = 48000;
    juce::AudioBuffer<float> sine(1, rate);
    for (int i = 0; i < rate; ++i)
        sine.setSample(0, i, 0.5f * std::sin(juce::MathConstants<double>::twoPi * 440 * i / rate));
    const auto doubled = sonora::stretchAudio(sine, rate, 2.0);
    require(doubled.getNumChannels() == 1
                && std::abs(doubled.getNumSamples() - 2 * rate) < rate / 20,
            "stretched length wrong");
    int crossings = 0;
    for (int i = 1; i < doubled.getNumSamples(); ++i)
        if ((doubled.getSample(0, i - 1) < 0.0f) != (doubled.getSample(0, i) < 0.0f))
            ++crossings;
    // 440 Hz over ~2 s gives ~1760 zero crossings; allow wide wobble room.
    require(crossings > 1500 && crossings < 2100, "stretched pitch drifted");
    const auto halved = sonora::stretchAudio(sine, rate, 0.5);
    require(std::abs(halved.getNumSamples() - rate / 2) < rate / 20, "compressed length wrong");
    // Degenerate inputs fail safe.
    require(sonora::stretchAudio(sine, rate, 1.0).getNumSamples() == rate, "unity copy wrong");
    require(sonora::stretchAudio(sine, rate, 0.01).getNumSamples() == 0, "absurd ratio accepted");
    require(sonora::stretchAudio(juce::AudioBuffer<float>(1, 0), rate, 2.0).getNumSamples() == 0,
            "empty input accepted");

    // Takes stretch at load: a x2 take renders twice as long.
    const auto media = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getNonexistentChildFile("sonora-stretch-media", "");
    require(media.createDirectory().wasOk(), "stretch media dir creation failed");
    {
        juce::WavAudioFormat takeFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(takeFormat.createWriterFor(
            new juce::FileOutputStream(media.getChildFile("take-1.wav")), 48000.0, 1, 16, {}, 0));
        require(writer != nullptr, "stretch fixture write failed");
        juce::AudioBuffer<float> tone(1, 4800);
        for (int i = 0; i < 4800; ++i)
            tone.setSample(0, i, 0.3f * std::sin(juce::MathConstants<double>::twoPi * 440 * i / 48000.0));
        require(writer->writeFromAudioSampleBuffer(tone, 0, 4800), "stretch fixture samples failed");
    }
    std::array<sonora::AudioTakeMeta, sonora::maxTakes> metas {};
    metas[0].id = 1;
    metas[0].setFileName("take-1.wav");
    metas[0].frames = 4800;
    metas[0].stretch = 2.0f;
    const auto stretched = sonora::loadTakes(metas, 1, media, 48000.0);
    require(stretched != nullptr && stretched->takes.size() == 1
                && std::abs(stretched->takes[0].audio.getNumSamples() - 9600) < 960,
            "take did not stretch at load");
    // Unity stretch passes the PCM through untouched.
    metas[0].stretch = 1.0f;
    const auto plain = sonora::loadTakes(metas, 1, media, 48000.0);
    require(plain != nullptr && plain->takes.size() == 1
                && plain->takes[0].audio.getNumSamples() == 4800 + 64,
            "unity stretch altered the take");
    require(media.deleteRecursively(), "stretch media cleanup failed");
}

void testKeyTools()
{
    using sonora::MusicScale;
    // Membership across a few scales.
    require(sonora::pitchInScale(60, 0, MusicScale::Major), "C not in C major");
    require(!sonora::pitchInScale(61, 0, MusicScale::Major), "C# in C major");
    require(sonora::pitchInScale(61, 9, MusicScale::Major), "C# not in A major");
    require(!sonora::pitchInScale(63, 9, MusicScale::Major), "Eb tritone accepted in A major");
    require(sonora::pitchInScale(60, 0, MusicScale::NaturalMinor), "C not in C minor");
    require(!sonora::pitchInScale(64, 0, MusicScale::NaturalMinor), "E natural in C minor");
    require(sonora::pitchInScale(66, 0, MusicScale::Blues), "Gb not in C blues");
    require(sonora::pitchInScale(69, 0, MusicScale::MajorPentatonic), "A missing from C major pentatonic");
    require(!sonora::pitchInScale(70, 0, MusicScale::MajorPentatonic), "Bb in C major pentatonic");
    // Snapping: nearest, ties up, edges clamp.
    require(sonora::snapPitchToScale(61, 0, MusicScale::Major) == 62, "snap tie did not go up");
    require(sonora::snapPitchToScale(63, 0, MusicScale::Major) == 64, "snap wrong");
    require(sonora::snapPitchToScale(60, 0, MusicScale::Major) == 60, "in-scale snap moved");
    require(sonora::snapPitchToScale(-5, 0, MusicScale::Major) == 0, "low snap unclamped");
    require(sonora::snapPitchToScale(200, 0, MusicScale::Major) == 127, "high snap unclamped");
    require(sonora::snapPitchToScale(63, 2, MusicScale::Major) == 64, "snap ignored key");
    // Chord voicings, low roots keep every tone in range.
    const auto maj = sonora::chordPitches(60, sonora::ChordType::Major);
    require(maj == std::vector<int>({ 60, 64, 67 }), "major voicing wrong");
    const auto min7 = sonora::chordPitches(60, sonora::ChordType::Min7);
    require(min7 == std::vector<int>({ 60, 63, 67, 70 }), "min7 voicing wrong");
    const auto top = sonora::chordPitches(127, sonora::ChordType::Major);
    require(top == std::vector<int>({ 127 }), "high root kept out-of-range tones");
    // Velocity ramp interpolates first-to-last across start order.
    sonora::Pattern ramp;
    ramp.count = 4;
    ramp.notes[0] = { 1, 3000, 240, 60, 10 };
    ramp.notes[1] = { 2, 0, 240, 62, 20 };
    ramp.notes[2] = { 3, 1000, 240, 64, 30 };
    ramp.notes[3] = { 4, 2000, 240, 65, 40 };
    sonora::applyVelocityRamp(ramp, 60, 100);
    require(ramp.notes[1].velocity == 60 && ramp.notes[2].velocity == 73
            && ramp.notes[3].velocity == 87 && ramp.notes[0].velocity == 100,
            "ramp order or interpolation wrong");
    require(ramp.valid(), "ramped pattern invalid");
    sonora::applyVelocityRamp(ramp, 300, -50);
    for (int i = 0; i < ramp.count; ++i)
        require(ramp.notes[static_cast<std::size_t>(i)].velocity >= 1
                && ramp.notes[static_cast<std::size_t>(i)].velocity <= 127,
                "ramp escaped velocity range");
    sonora::Pattern single;
    single.count = 1;
    single.notes[0] = { 1, 0, 240, 60, 64 };
    sonora::applyVelocityRamp(single, 90, 100);
    require(single.notes[0].velocity == 90, "single-note ramp wrong");
    sonora::Pattern empty;
    sonora::applyVelocityRamp(empty, 0, 127);
    require(empty.count == 0, "ramp created notes");
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
    require(sonora::ProjectIO::decode(json.replace("\"version\": 22", "\"version\": 23"), loaded).failed(),
            "unknown version accepted");
    require(sonora::ProjectIO::decode(json.replace("\"version\": 22", "\"version\": 4294967297"), loaded).failed(),
            "overflowed version accepted");
    require(sonora::ProjectIO::decode(json.replace("\"velocity\": 100", "\"velocity\": 0"), loaded).failed(),
            "zero velocity accepted");
    require(sonora::ProjectIO::decode(json.replace("\"pitch\": 60", "\"pitch\": 4294967356"), loaded).failed(),
            "overflowed pitch accepted");
    auto invalid = original;
    invalid.tracks[0].melodies[0].notes[1].start = 480;
    require(!invalid.valid(), "same-pitch overlap accepted");
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(invalid), loaded).failed(), "overlap decoded");
    invalid = original;
    invalid.tracks[0].melodies[0].notes[1].id = 1;
    require(!invalid.valid(), "duplicate ID accepted");
    invalid = original;
    invalid.tracks[0].melodies[0].notes[3].duration += 1;
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
    original.tracks[0].mix = { 0.47f, false, true };
    original.tracks[1].mix = { 1.12f, true, false };
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
    require(loaded.tracks[0].melodies[0] == original.tracks[0].melodies[0] && loaded.bpm == original.bpm && loaded.tracks[1].drumPatterns[0].hitCount() == 0,
            "migration changed melody or added drum hits");
    require(loaded.tracks[0].mix.volume == 1.0f && !loaded.tracks[0].mix.mute && !loaded.tracks[1].mix.solo,
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
            state.tracks[0].melodies[0].notes[0].id = static_cast<std::uint32_t>(i);
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
        coherent = coherent && state.tracks[0].melodies[0].notes[0].id == static_cast<std::uint32_t>(i)
            && state.tracks[0].melodies[0].notes[3] == fixture().tracks[0].melodies[0].notes[3];
    }
    producer.join();
    require(coherent, "concurrent snapshot transfer was torn or reordered");
}

std::vector<float> render(int blockSize, const sonora::ProjectState& project = fixture(), double sampleRate = 48000)
{
    auto engineStorage = std::make_unique<sonora::AudioEngine>();
    auto& engine = *engineStorage;
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

    auto engineStorage = std::make_unique<sonora::AudioEngine>();
    auto& engine = *engineStorage;
    engine.prepare(48000);
    sonora::ProjectState shortNote = sonora::defaultProject();
    shortNote.bpm = 120.0;
    shortNote.tracks[0].melodies[0].count = 1;
    shortNote.tracks[0].melodies[0].notes[0] = { 1, 0, 240, 60, 100 };
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
    shortNote.tracks[0].melodies[0] = {};
    require(engine.submit(shortNote), "empty pattern rejected");
    engine.process({ &buffer, 0, 256 });
    require(buffer.getMagnitude(0, 256) == 0.0f, "deleting a playing note left a stuck voice");
}

void testDrumAudio()
{
    // Loop preview follows per-track slots: two drum tracks with hits in
    // different slots must both sound at once.
    {
        auto engineStorage = std::make_unique<sonora::AudioEngine>();
        auto& engine = *engineStorage;
        engine.prepare(48000.0);
        auto project = fixture();
        project.tracks[0].melodies[0] = {};
        project.tracks[1].drumPatterns[0] = {};
        project.tracks[2].id = 3;
        project.tracks[2].setTrackName("Extra drums");
        project.tracks[2].kind = sonora::TrackKind::Drums;
        project.tracks[2].icon = 1;
        for (int step : { 8, 24, 40, 56 })
            project.tracks[2].drumPatterns[1].steps[0][static_cast<std::size_t>(step)] = 110;
        for (int step : { 0, 16, 32, 48 })
            project.tracks[1].drumPatterns[0].steps[0][static_cast<std::size_t>(step)] = 110;
        require(engine.submit(project), "two-drum project rejected");
        engine.setLoopSelection(1, 0, 0);
        engine.setLoopSelection(2, 0, 1);
        engine.setPlaying(true);
        juce::AudioBuffer<float> buffer(2, 512);
        auto loudest = [&](int blocks) {
            float peak = 0.0f;
            for (int i = 0; i < blocks; ++i)
            {
                buffer.clear();
                engine.process({ &buffer, 0, 512 });
                peak = std::max(peak, buffer.getMagnitude(0, 512));
            }
            return peak;
        };
        require(loudest(200) > 0.05f, "per-track loop slots silent");
        // Each track's slot must sound on its own: mute one, the other stays.
        project.tracks[1].mix.mute = true;
        require(engine.submit(project), "muted project rejected");
        require(loudest(200) > 0.05f, "second drum track lost under mute");
        project.tracks[1].mix.mute = false;
        project.tracks[2].mix.mute = true;
        require(engine.submit(project), "muted project rejected");
        require(loudest(200) > 0.05f, "first drum track lost under mute");
    }
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
    drumsOnly.tracks[0].melodies[0] = {};
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
    muted.tracks[0].mix.mute = muted.tracks[1].mix.mute = true;
    const auto silence = render(256, muted);
    require(std::all_of(silence.begin(), silence.end(), [](float x) { return x == 0.0f; }), "muted tracks make sound");
    auto soloDrums = fixture();
    soloDrums.tracks[1].mix.solo = true;
    require(render(256, soloDrums) == render(256, drumsOnly), "drum solo leaks melody");
    auto melodyOnly = fixture();
    melodyOnly.tracks[1].drumPatterns[0] = {};
    auto soloMelody = fixture();
    soloMelody.tracks[0].mix.solo = true;
    require(render(256, soloMelody) == render(256, melodyOnly), "melody solo leaks drums");
    auto bothSolo = fixture();
    bothSolo.tracks[0].mix.solo = bothSolo.tracks[1].mix.solo = true;
    require(render(256, bothSolo) == render(256), "two soloed tracks should play together");

    auto engineStorage = std::make_unique<sonora::AudioEngine>();
    auto& engine = *engineStorage;
    engine.prepare(48000);
    require(engine.submit(fixture()), "audition project rejected");
    require(engine.auditionDrum(1, 0), "pad audition rejected");
    buffer.clear();
    engine.process({ &buffer, 0, 256 });
    require(buffer.getMagnitude(0, 256) > 0.01f, "pad audition needs running transport");
    engine.panic();
    engine.process({ &buffer, 0, 256 });
    require(buffer.getMagnitude(0, 256) == 0.0f, "panic left a drum ringing");
    require(!engine.auditionDrum(-1, 0) && !engine.auditionDrum(1, 8) && !engine.auditionDrum(1, 0, 128)
            && !engine.auditionDrum(0, 0) && !engine.auditionDrum(2, 0),
            "invalid audition accepted");
}
void testArrangement()
{
    // Song mode tiles the 4-bar pattern across gated sections, then ends.
    sonora::LoopScheduler scheduler;
    scheduler.configure(48000, 120);
    auto project = fixture();
    project.song.sections = 3;
    for (int s = 0; s < 3; ++s)
    {
        project.song.trackOn[static_cast<std::size_t>(s)][0] = (s != 1);
        project.song.trackOn[static_cast<std::size_t>(s)][1] = (s != 2);
    }
    std::vector<Event> events;
    const auto loopFrames = std::llround(48000.0 * 60.0 / 120.0 * sonora::patternTicks / sonora::ticksPerQuarter);
    bool finished = false;
    std::int64_t position = 0;
    while (!finished)
    {
        finished = scheduler.processSong(project.tracks, project.song, 512, false,
            [&](int, const sonora::Note& note, bool on, int offset) {
                events.push_back({ position + offset, note.pitch, on });
            });
        scheduler.scheduleDrumsSong(project.tracks[1].drumPatterns, project.song, 1, 512,
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
            require(project.song.trackOn[static_cast<std::size_t>(section)][1], "muted section plays drums");
            drumsInSection1 = drumsInSection1 || section == 1;
        }
        else
        {
            require(project.song.trackOn[static_cast<std::size_t>(section)][0], "muted section plays melody");
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
        song.tracks[0].melodies[0] = {};
        song.tracks[0].melodies[0].count = 1;
        song.tracks[0].melodies[0].notes[0] = { 1, 0, 240, 60, 100 };
        song.tracks[0].melodies[1] = {};
        song.tracks[0].melodies[1].count = 1;
        song.tracks[0].melodies[1].notes[0] = { 2, 0, 240, 72, 100 };
        song.song.sections = 2;
        song.song.slots[1][0] = 1;
        std::vector<Event> sectioned;
        std::int64_t position = 0;
        bool finished = false;
        while (!finished)
        {
            finished = variation.processSong(song.tracks, song.song, 512, false,
                [&](int, const sonora::Note& note, bool on, int offset) {
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
    scheduler.processLoop(project.tracks[0].melodies[0], 512, false, [](const auto&, bool, int) {});
    // Engine loop preview follows the selected library slot.
    {
        sonora::AudioEngine preview;
        preview.prepare(48000);
        sonora::ProjectState song = fixture();
        song.tracks[0].melodies[0] = {};
        song.tracks[0].melodies[1].count = 1;
        song.tracks[0].melodies[1].notes[0] = { 5, 0, 240, 71, 100 };
        song.tracks[1].drumPatterns[0] = {};
        require(preview.submit(song), "variation project rejected");
        juce::AudioBuffer<float> buffer(2, 512);
        preview.setPlaying(true);
        preview.process({ &buffer, 0, 512 });
        preview.process({ &buffer, 0, 512 });
        require(buffer.getMagnitude(0, 512) < 1.0e-5f, "unselected slot leaks into loop preview");
        preview.setLoopSelection(0, 1, 0);
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
    songProject.song.trackOn[1][0] = false;
    sonora::ProjectState loaded;
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(songProject), loaded).wasOk()
        && loaded == songProject, "song arrangement round-trip failed");
    auto legacy = downshapeToVersion(juce::JSON::parse(sonora::ProjectIO::encode(songProject)), 2);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk(), "v2 migration failed");
    require(!loaded.songMode && loaded.song.sections == 2, "v2 default arrangement wrong");
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(loaded), loaded).wasOk(), "migrated v2 re-save failed");
    auto bad = juce::JSON::parse(sonora::ProjectIO::encode(songProject));
    bad.getDynamicObject()->getProperty("song").getDynamicObject()->setProperty("sections", sonora::maxSections + 1);
    require(sonora::ProjectIO::decode(juce::JSON::toString(bad), loaded).failed(), "oversized song accepted");

    // Engine: song mode stops at the end; loop mode keeps playing.
    auto engineStorage = std::make_unique<sonora::AudioEngine>();
    auto& engine = *engineStorage;
    engine.prepare(48000);
    songProject.tracks[0].melodies[0] = {};
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
    loud.project.tracks[0].mix.volume = 1.5f;
    loud.project.tracks[1].mix.volume = 1.5f;
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
    withTake.project.tracks[0].melodies[0] = {};
    withTake.project.tracks[1].drumPatterns[0] = {};
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
    original.tracks[0].fx.eq.low = 4.5f;
    original.tracks[0].fx.eq.midFreq = 800.0f;
    original.tracks[0].fx.comp.thresholdDb = -12.0f;
    original.tracks[0].fx.comp.ratio = 4.0f;
    original.tracks[0].fx.delay.mix = 0.25f;
    original.tracks[0].fx.delay.feedback = 0.4f;
    original.tracks[0].fx.reverb.mix = 0.2f;
    original.tracks[0].fx.reverb.enabled = false;
    original.tracks[1].fx.eq.high = -3.0f;
    original.tracks[1].fx.comp.enabled = false;
    original.master.ceilingDb = -1.0f;
    original.master.releaseMs = 120.0f;
    sonora::ProjectState loaded;
    const auto json = sonora::ProjectIO::encode(original);
    require(json.contains("\"version\": 22"), "projects must save as v22");
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
    require(json.contains("\"version\": 22"), "take projects must save as v22");
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
    project.tracks[0].melodies[0] = {};
    project.tracks[1].drumPatterns[0] = {};
    project.song.sections = 1;
    project.songMode = true;
    auto engineStorage = std::make_unique<sonora::AudioEngine>();
    auto& engine = *engineStorage;
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

void testTakeSolo()
{
    // Opposing DC takes cancel when both play; soloing one isolates it, and
    // solo wins over mute. Gating follows the submitted project, not the set.
    auto project = fixture();
    project.tracks[0].melodies[0] = {};
    project.tracks[1].drumPatterns[0] = {};
    project.song.sections = 1;
    project.songMode = true;
    project.takeCount = 2;
    for (int t = 0; t < 2; ++t)
    {
        project.takes[static_cast<std::size_t>(t)].id = static_cast<std::uint32_t>(t + 1);
        project.takes[static_cast<std::size_t>(t)].setFileName("take" + juce::String(t + 1) + ".wav");
        project.takes[static_cast<std::size_t>(t)].frames = 48000;
    }
    auto engineStorage = std::make_unique<sonora::AudioEngine>();
    auto& engine = *engineStorage;
    engine.prepare(48000.0);
    auto* set = new sonora::TakeSet();
    for (int t = 0; t < 2; ++t)
    {
        sonora::PreloadedTake take;
        take.id = static_cast<std::uint32_t>(t + 1);
        take.audio.setSize(1, 48000);
        for (int i = 0; i < 48000; ++i)
            take.audio.setSample(0, i, t == 0 ? 0.4f : -0.4f);
        set->takes.push_back(std::move(take));
    }
    require(engine.submit(project), "comp project rejected");
    engine.retireTakeSet(set);
    engine.setPlaying(true);
    juce::AudioBuffer<float> buffer(2, 512);
    auto level = [&] {
        engine.process({ &buffer, 0, 512 });
        return buffer.getSample(0, 256);
    };
    for (int i = 0; i < 4; ++i)
        level();
    require(std::abs(level()) < 0.03f, "takes did not cancel");
    project.takes[1].solo = true;
    require(engine.submit(project), "solo submit rejected");
    require(std::abs(level() + 0.4f) < 0.03f, "solo did not isolate take 2");
    project.takes[1].mute = true;
    require(engine.submit(project), "solo+mute submit rejected");
    require(std::abs(level() + 0.4f) < 0.03f, "mute overrode solo");
    project.takes[1].solo = false;
    require(engine.submit(project), "unsolo submit rejected");
    require(std::abs(level() - 0.4f) < 0.03f, "mute ignored after unsolo");
    project.takes[1].mute = false;
    require(engine.submit(project), "unmute submit rejected");
    require(std::abs(level()) < 0.03f, "takes did not cancel again");
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
    // Live keys need a synth track; clear the loops so only the live note sounds.
    auto liveProject = [&]() {
        auto project = fixture();
        for (auto& melody : project.tracks[0].melodies)
            melody = {};
        for (auto& pattern : project.tracks[1].drumPatterns)
            pattern = {};
        return project;
    };
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
        auto engineStorage = std::make_unique<sonora::AudioEngine>();
        auto& engine = *engineStorage;
        engine.prepare(rate);
        require(engine.submit(liveProject()), "live project rejected");
        addMessageToQueueAt(engine.midiCollector, juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100));
        addMessageToQueueAt(engine.midiCollector, juce::MidiMessage::pitchWheel(1, bend));
        auto output = renderLive(engine, 19200);
        output.erase(output.begin(), output.begin() + 4800); // skip attack
        require(std::abs(medianF0(output) - expected) / expected < 0.015, "pitch bend mistuned");
        addMessageToQueueAt(engine.midiCollector, juce::MidiMessage::noteOff(1, 69));
    }
    // Mod wheel keeps center pitch while staying audible.
    {
        auto engineStorage = std::make_unique<sonora::AudioEngine>();
        auto& engine = *engineStorage;
        engine.prepare(rate);
        require(engine.submit(liveProject()), "live project rejected");
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
        auto engineStorage = std::make_unique<sonora::AudioEngine>();
        auto& engine = *engineStorage;
        engine.prepare(rate);
        require(engine.submit(liveProject()), "live project rejected");
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
    original.tracks[0].melodies[1].count = 2;
    original.tracks[0].melodies[1].notes[0] = { 11, 0, 480, 64, 100 };
    original.tracks[0].melodies[1].notes[1] = { 12, 960, 480, 67, 90 };
    original.tracks[0].melodies[3].count = 1;
    original.tracks[0].melodies[3].notes[0] = { 13, 0, 240, 71, 110 };
    original.tracks[1].drumPatterns[2].steps[0][0] = 120;
    original.tracks[1].drumPatterns[2].steps[4][16] = 100;
    for (int s = 0; s < 8; ++s)
    {
        original.song.slots[static_cast<std::size_t>(s)][0] = static_cast<std::uint8_t>(((int[]) { 3, 2, 1, 0, 0, 1, 2, 3 })[s]);
        original.song.slots[static_cast<std::size_t>(s)][1] = static_cast<std::uint8_t>(((int[]) { 0, 0, 2, 0, 2, 0, 0, 0 })[s]);
    }
    original.song.sections = 4;
    sonora::ProjectState loaded;
    const auto json = sonora::ProjectIO::encode(original);
    require(json.contains("\"version\": 22"), "variation projects must save as v22");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == original,
            "variation round-trip changed slots or indices");

    // Version 5 documents migrate with slot A content and slot-A sections.
    auto legacy = downshapeToVersion(juce::JSON::parse(json), 5);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk(), "v5 migration failed");
    require(loaded.tracks[0].melodies[0] == original.tracks[0].melodies[0] && loaded.tracks[1].drumPatterns[0] == original.tracks[1].drumPatterns[0],
            "v5 migration changed slot A");
    require(loaded.tracks[0].melodies[1].count == 0 && loaded.tracks[1].drumPatterns[2].hitCount() == 0,
            "v5 migration invented slot content");
    for (int i = 0; i < sonora::maxSections; ++i)
        require(loaded.song.slots[static_cast<std::size_t>(i)][0] == 0
                && loaded.song.slots[static_cast<std::size_t>(i)][1] == 0,
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
            ->getProperty("slots").getArray()->getReference(3).getArray()->set(0, 9);
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

    // Track reorder carries each track's arrangement column with it.
    auto moved = original;
    moved.song.trackOn[0][0] = false;
    moved.song.trackOn[0][1] = true;
    require(sonora::moveTrackState(moved, 0, 1), "adjacent reorder rejected");
    require(moved.tracks[0].kind == sonora::TrackKind::Drums
            && moved.tracks[1].kind == sonora::TrackKind::Synth, "reorder did not swap tracks");
    require(moved.song.slots[0][0] == 0 && moved.song.slots[0][1] == 3, "reorder scrambled slots");
    require(moved.song.trackOn[0][0] && !moved.song.trackOn[0][1], "reorder did not carry gates");
    require(moved.valid(), "reordered project invalid");
    require(!sonora::moveTrackState(moved, 0, 0), "no-op reorder accepted");
    require(!sonora::moveTrackState(moved, 1, 7), "reorder onto empty track accepted");
    const auto roundTripped = sonora::ProjectIO::encode(moved);
    require(sonora::ProjectIO::decode(roundTripped, loaded).wasOk() && loaded == moved,
            "reordered project does not persist");
}

void testKitVariants()
{
    // Factory variants are deterministic, bounded, audible, and distinct.
    std::vector<sonora::SampleBank> kits;
    for (int variant = 0; variant < sonora::numKitVariants; ++variant)
        kits.push_back(sonora::buildStarterBank(sonora::kitVariantParams(variant)));
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
    // The later voicings are distinct from each other and from Starter, and
    // Tight is shorter / Boom longer than Starter on the kick.
    for (int a = 0; a < sonora::numKitVariants; ++a)
        for (int b = 0; b < a; ++b)
            require(kits[static_cast<std::size_t>(a)].data[0] != kits[static_cast<std::size_t>(b)].data[0],
                    "kit variants not distinct");
    require(kits[3].data[0].size() < starter.size() && kits[4].data[0].size() > starter.size(),
            "Tight/Boom lengths wrong");
    require(zeroCrossings(kits[4].data[0]) < zeroCrossings(deep), "Boom not lower than Deep");
    for (int variant = 0; variant < sonora::numKitVariants; ++variant)
        require(juce::String(sonora::kitVariantName(variant)).isNotEmpty(), "unnamed kit variant");
    // Variant persistence round-trips through the drum track field.
    auto project = fixture();
    project.tracks[1].kitVariant = 2;
    sonora::ProjectState loaded;
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(project), loaded).wasOk()
            && loaded.tracks[1].kitVariant == 2, "kit variant round-trip failed");
    project.tracks[1].kitVariant = 7;
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
    sonora::setPadSampleName(project.tracks[1].padSamples[2], "hat.wav");
    for (int pad = 0; pad < sonora::drumPads; ++pad)
    {
        const auto file = sonora::padSampleName(project.tracks[1].padSamples[static_cast<std::size_t>(pad)]);
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
    auto engineStorage = std::make_unique<sonora::AudioEngine>();
    auto& engine = *engineStorage;
    engine.prepare(48000.0);
    auto project = fixture();
    project.tracks[0].melodies[0] = {};
    require(engine.submit(project), "kit project rejected");
    auto custom = std::make_unique<sonora::SampleBank>();
    custom->sampleRate = 48000.0;
    custom->data[0].assign(4800, 0.0f);
    for (int i = 0; i < 4800; ++i)
        custom->data[0][static_cast<std::size_t>(i)] = 0.5f;
    engine.retirePadBank(1, custom.get());
    engine.setPlaying(true);
    juce::AudioBuffer<float> buffer(2, 512);
    float loudest = 0.0f;
    for (int i = 0; i < 6; ++i)
    {
        buffer.clear();
        engine.process({ &buffer, 0, 512 });
        loudest = std::max(loudest, buffer.getMagnitude(0, 512));
    }
    // The fixture's bar-0 kick (step 0, velocity 110) must carry the DC block.
    require(loudest > 0.05f, "custom bank silent in engine");
    engine.retirePadBank(1, nullptr);
    engine.process({ &buffer, 0, 512 });
}

void testKitPersistence()
{
    auto original = fixture();
    sonora::setPadSampleName(original.tracks[1].padSamples[0], "my-kick.wav");
    sonora::setPadSampleName(original.tracks[1].padSamples[7], "shaker-loop.flac");
    original.tracks[1].kitVariant = 1;
    sonora::ProjectState loaded;
    const auto json = sonora::ProjectIO::encode(original);
    require(json.contains("\"version\": 22"), "kit projects must save as v22");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == original,
            "kit round-trip changed pad samples");
    // Version 6 documents migrate to the built-in kit.
    auto legacy = downshapeToVersion(juce::JSON::parse(json), 6);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk(), "v6 migration failed");
    for (const auto& slot : loaded.tracks[1].padSamples)
        require(sonora::padSampleName(slot).isEmpty(), "v6 migration invented custom samples");
    require(loaded.tracks[1].kitVariant == 0, "v6 migration invented a variant");
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
    require(sonora::makeSectionUnique(song, library, 2, 0), "shared section not detached");
    require(song.slots[2][0] == 1, "detached section points at wrong slot");
    require(library[1] == library[0], "detached content differs from source");
    require(!sonora::makeSectionUnique(song, library, 2, 0), "unique section detached again");
    require(!sonora::makeSectionUnique(song, library, 9, 0), "out-of-range section detached");
    // Detached content is independent: editing the copy spares the source.
    library[1].notes[0].pitch = 64;
    require(library[0].notes[0].pitch == 60, "detach shares storage with source");
    // Full library: every slot in use somewhere refuses the detach.
    for (int s = 0; s < 8; ++s)
    {
        song.slots[static_cast<std::size_t>(s)][0] = static_cast<std::uint8_t>(s % 4);
        song.slots[static_cast<std::size_t>(s)][1] = static_cast<std::uint8_t>(s % 3);
    }
    song.sections = 5;
    song.slots[4][0] = 0; // section 0 shared, yet no free slot anywhere
    const auto before = library;
    require(!sonora::makeSectionUnique(song, library, 0, 0), "detach succeeded with full library");
    require(library == before, "refused detach mutated the library");
    std::array<sonora::DrumPattern, sonora::numPatterns> grids {};
    grids[0].steps[0][0] = 100;
    sonora::Arrangement drums;
    drums.sections = 2;
    require(sonora::makeDrumSectionUnique(drums, grids, 1, 1), "shared drum section not detached");
    require(drums.slots[1][1] == 1 && grids[1].steps[0][0] == 100, "drum detach wrong");
    require(!sonora::makeDrumSectionUnique(drums, grids, 1, 1), "unique drum section detached again");
    drums.sections = 5;
    for (int s = 0; s < 8; ++s)
    {
        drums.slots[static_cast<std::size_t>(s)][1] = static_cast<std::uint8_t>(((int[]) { 0, 1, 2, 3, 0, 0, 0, 0 })[s]);
        drums.slots[static_cast<std::size_t>(s)][0] = 0;
    }
    const auto gridsBefore = grids;
    require(!sonora::makeDrumSectionUnique(drums, grids, 0, 1), "drum detach succeeded with full library");
    require(grids == gridsBefore, "refused drum detach mutated the library");
    // Sharing queries drive the "shared by N" indicators.
    song.sections = 8;
    const auto shared = sonora::sectionsSharingSlot(song, 0, 0);
    require(shared.size() == 2 && shared[0] == 1 && shared[1] == 5, "sharing query wrong");
    require(sonora::sectionsSharingSlot(song, 1, 3).empty(), "unused slot reports sharers");
    // Repeated clicks cycle A > B > C > D > empty; an off cell always starts on A.
    auto cell = sonora::nextArrangementCell(0, false);
    require(cell.first == 0 && cell.second, "off cell did not start on A");
    for (std::uint8_t slot = 0; slot < sonora::numPatterns - 1; ++slot)
    {
        cell = sonora::nextArrangementCell(slot, true);
        require(cell.first == slot + 1 && cell.second, "cell did not advance to next loop");
    }
    cell = sonora::nextArrangementCell(sonora::numPatterns - 1, true);
    require(cell.first == 0 && !cell.second, "last loop did not cycle back to empty");
}

void testAcceptance()
{
    // The reference song: demo-style melody A plus a sparser variation B,
    // drums plus a busier variation, four alternating sections, mixed FX,
    // and a sung take. Everything the app can do, rendered in one pass.
    sonora::ProjectState song = sonora::defaultProject();
    song.bpm = 120.0;
    song.tracks[0].melodies[0].count = 3;
    song.tracks[0].melodies[0].notes[0] = { 1, 0, 960, 48, 90 };
    song.tracks[0].melodies[0].notes[1] = { 2, 0, 720, 60, 100 };
    song.tracks[0].melodies[0].notes[2] = { 3, 960, 720, 64, 95 };
    song.tracks[0].melodies[1].count = 2;
    song.tracks[0].melodies[1].notes[0] = { 4, 0, 480, 55, 100 };
    song.tracks[0].melodies[1].notes[1] = { 5, 960, 480, 67, 100 };
    for (int step = 0; step < sonora::gridSteps; step += 4)
        song.tracks[1].drumPatterns[0].steps[0][static_cast<std::size_t>(step)] = 110;
    for (int step = 0; step < sonora::gridSteps; step += 2)
        song.tracks[1].drumPatterns[1].steps[2][static_cast<std::size_t>(step)] = 80;
    song.song.sections = 4;
    for (int s = 0; s < 8; ++s)
    {
        song.song.slots[static_cast<std::size_t>(s)][0] = static_cast<std::uint8_t>(((int[]) { 0, 1, 0, 1, 0, 0, 0, 0 })[s]);
        song.song.slots[static_cast<std::size_t>(s)][1] = static_cast<std::uint8_t>(((int[]) { 0, 0, 1, 1, 0, 0, 0, 0 })[s]);
    }
    song.songMode = true;
    song.tracks[0].fx.eq.low = 3.0f;
    song.tracks[0].fx.delay.mix = 0.2f;
    song.tracks[1].fx.comp.thresholdDb = -12.0f;
    song.tracks[1].mix.volume = 0.9f;
    song.master.ceilingDb = -1.0f;
    // A two-second sung take starting at bar 2.
    const auto media = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getNonexistentChildFile("sonora-accept-media", "");
    require(media.createDirectory().wasOk(), "acceptance media creation failed");
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
            new juce::FileOutputStream(media.getChildFile("vocal.wav")), 48000.0, 1, 16, {}, 0));
        require(writer != nullptr, "vocal fixture write failed");
        juce::AudioBuffer<float> vocal(1, 96000);
        for (int i = 0; i < 96000; ++i)
            vocal.setSample(0, i, 0.4f * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0));
        require(writer->writeFromAudioSampleBuffer(vocal, 0, 96000), "vocal fixture samples failed");
    }
    song.takeCount = 1;
    song.takes[0].id = 1;
    song.takes[0].setFileName("vocal.wav");
    song.takes[0].startTick = 3840;
    song.takes[0].frames = 96000;
    require(song.valid(), "reference song invalid");
    // Save to disk and reload: the file round-trip is part of acceptance.
    const auto projectFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getNonexistentChildFile("acceptance", ".sonora.json");
    require(sonora::ProjectIO::save(projectFile, song).wasOk(), "acceptance save failed");
    sonora::ProjectState loaded;
    require(sonora::ProjectIO::load(projectFile, loaded).wasOk() && loaded == song,
            "acceptance reload changed the song");
    // Bounce the loop and the full song, both carrying the vocal take.
    sonora::ExportJob loopJob;
    loopJob.project = loaded;
    loopJob.mediaDir = media;
    auto loop = sonora::OfflineExport::render(loopJob);
    require(loop.ok(), "acceptance loop bounce failed");
    sonora::ExportJob songJob = loopJob;
    songJob.songRange = true;
    songJob.tailSeconds = 0.5;
    auto full = sonora::OfflineExport::render(songJob);
    require(full.ok(), "acceptance song bounce failed");
    require(full.audio.getNumSamples() > loop.audio.getNumSamples() * 3, "song bounce too short");
    for (const auto* bounce : { &loop, &full })
    {
        require(bounce->audio.getMagnitude(0, bounce->audio.getNumSamples()) > 0.02f, "bounce silent");
        require(bounce->peak <= 1.0f, "bounce clipped past the limiter");
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < bounce->audio.getNumSamples(); i += 97)
                require(std::isfinite(bounce->audio.getSample(ch, i)), "bounce produced invalid audio");
    }
    // The vocal take is actually in the song bounce: it punches in at bar 2
    // (tick 3840 = frame 96000 at 120 BPM) and runs 96000 frames.
    float songVocal = 0.0f;
    for (int i = 100000; i < 150000 && i < full.audio.getNumSamples(); ++i)
        songVocal = std::max(songVocal, std::abs(full.audio.getSample(0, i)));
    require(songVocal > 0.05f, "vocal take missing from song bounce");
    // The persisted WAV plays back through a reader with sane headers.
    const auto wavFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getNonexistentChildFile("sonora-accept", ".wav");
    require(sonora::OfflineExport::writeWav(wavFile, full, 16).wasOk(), "acceptance WAV write failed");
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(wavFormat.createReaderFor(
        new juce::FileInputStream(wavFile), true));
    require(reader != nullptr && reader->numChannels == 2
        && reader->lengthInSamples == full.audio.getNumSamples(), "acceptance WAV header wrong");
    require(projectFile.deleteFile() && wavFile.deleteFile() && media.deleteRecursively(),
            "acceptance cleanup failed");
}

void testOmarchyTheme()
{
    using namespace sonora::omarchy;
    require(parseHexColor("#66ccff", 0) == 0xff66ccff, "hex parse wrong");
    require(parseHexColor("abc", 0) == 0xffaabbcc, "short hex parse wrong");
    require(parseHexColor("not-a-color", 0x12345678) == 0x12345678, "bad hex not falling back");
    require(parseHexColor("#12345", 7) == 7, "short-length hex not falling back");
    require(parseHexColor("#gggggg", 9) == 9, "non-hex digits not falling back");
    const auto values = parseFlatToml("# comment\n[section]\nmode = \"dark\"\n"
                                      "accent=#8d8d8d # trailing\nbare = 42\n  spaced  =  \"x y\"  \n");
    require(values.at("mode") == "dark", "quoted value wrong");
    require(values.at("accent") == "#8d8d8d", "trailing comment wrong");
    require(values.at("bare") == "42", "bare value wrong");
    require(values.at("spaced") == "x y", "spaced value wrong");
    require(values.find("section") == values.end(), "section header leaked");
    // Mason-like palette maps every role and stays dark.
    std::map<juce::String, juce::String> mason { { "mode", "dark" },
        { "background", "#000000" }, { "foreground", "#ffffff" }, { "muted", "#7a7a7a" },
        { "cyan", "#66ccff" }, { "magenta", "#00cfff" }, { "blue", "#1e90ff" },
        { "red", "#ff3366" }, { "orange", "#009acd" } };
    const auto palette = paletteFromMap(mason, "mason");
    require(palette.background == 0xff000000 && palette.text == 0xffffffff, "bg/fg mapping wrong");
    require(palette.accent == 0xff66ccff && palette.melody == 0xff66ccff, "accent mapping wrong");
    require(palette.drums == 0xff00cfff && palette.audio == 0xff1e90ff, "voice mapping wrong");
    require(palette.danger == 0xffff3366 && palette.warn == 0xff009acd, "signal mapping wrong");
    require(palette.dark && palette.themeName == "mason", "mode/name mapping wrong");
    require(palette.panel != palette.background && palette.raised != palette.panel, "surfaces not derived");
    // Missing keys fall back to the neon defaults, one by one.
    const auto partial = paletteFromMap({ { "background", "#111111" } }, "partial");
    require(partial.background == 0xff111111 && partial.text == 0xffe7f0fc, "fallback wrong");
    require(partial.drums == 0xffb19aff, "drum fallback wrong");
    Palette def;
    require(partial.muted == def.muted && partial.warn == def.warn, "mute/warn fallback wrong");
    // Light mode flag follows the mode key.
    require(paletteFromMap({ { "mode", "light" } }, "x").dark == false, "light mode misread");
    require(paletteFromMap({}, "x").dark == true, "missing mode should default dark");
    // Live system: loading never fails, even without Omarchy installed.
    const auto live = loadOmarchyPalette();
    require(live.background >> 24 == 0xff && live.text >> 24 == 0xff, "live palette not opaque");
    (void) themeFingerprint();
    // Built-in appearance modes behind the header theme toggle.
    require(themeModeFromString("light") == ThemeMode::Light, "light mode parse");
    require(themeModeFromString("DARK") == ThemeMode::Dark, "dark mode parse case");
    require(themeModeFromString("anything-else") == ThemeMode::System, "unknown mode must follow system");
    require(juce::String(themeModeName(ThemeMode::Light)) == "light", "mode name wrong");
    const auto light = sonoraLightPalette(), dark = sonoraDarkPalette();
    require(!light.dark && dark.dark, "built-in mode flags wrong");
    const auto brightness = [](std::uint32_t argb) {
        const auto channel = [&](int shift) { return static_cast<float>((argb >> shift) & 0xff) / 255.0f; };
        return 0.299f * channel(16) + 0.587f * channel(8) + 0.114f * channel(0);
    };
    require(brightness(light.background) > 0.7f && brightness(light.text) < 0.3f, "light palette not light-on-paper");
    require(brightness(dark.background) < 0.2f && brightness(dark.text) > 0.7f, "dark palette not light-on-dark");
    require(light.accent != dark.accent, "modes share an accent");
}

void testInstruments()
{
    auto probeStorage = std::make_unique<sonora::AudioEngine>();
    auto& probe = *probeStorage;
    require(probe.instrumentsAvailable(), "bundled GeneralUser GS bank failed to load");
    for (std::size_t i = 0; i < sonora::instruments.size(); ++i)
        for (std::size_t j = 0; j < i; ++j)
            require(juce::String(sonora::instruments[i].name) != sonora::instruments[j].name,
                    "duplicate instrument name");

    // One held note on track 0; drums silent so only the instrument sounds.
    auto project = fixture();
    project.tracks[0].melodies[0] = {};
    project.tracks[0].melodies[0].count = 1;
    project.tracks[0].melodies[0].notes[0] = { 1, 0, 3840, 60, 110 };
    project.tracks[1].drumPatterns[0] = {};
    auto render = [](const sonora::ProjectState& state, int blocks) {
        auto engineStorage = std::make_unique<sonora::AudioEngine>();
        auto& engine = *engineStorage;
        engine.prepare(48000.0);
        require(engine.submit(state), "instrument project rejected");
        engine.setPlaying(true);
        std::vector<float> out;
        juce::AudioBuffer<float> buffer(2, 512);
        for (int i = 0; i < blocks; ++i)
        {
            engine.process({ &buffer, 0, 512 });
            for (int s = 0; s < 512; ++s)
                out.push_back(buffer.getSample(0, s));
        }
        return out;
    };
    auto peak = [](const std::vector<float>& audio) {
        float value = 0.0f;
        for (auto x : audio)
        {
            require(std::isfinite(x), "instrument produced non-finite audio");
            value = std::max(value, std::abs(x));
        }
        return value;
    };
    const auto sine = render(project, 40);
    int audibleCount = 0;
    for (int preset = 1; preset < static_cast<int>(sonora::instruments.size()); ++preset)
    {
        if (!sonora::isBankInstrument(preset))
            continue; // the Sampler needs a loaded file; see testSampler
        auto variant = project;
        variant.tracks[0].instrumentPreset = preset;
        const auto audio = render(variant, 40);
        const auto level = peak(audio);
        require(level < 1.0f, "sampled instrument clips");
        if (level > 0.01f)
            ++audibleCount;
        require(audio != sine, "sampled preset rendered as the sine voice");
    }
    int bankPresets = 0;
    for (std::size_t i = 1; i < sonora::instruments.size(); ++i)
        bankPresets += sonora::isBankInstrument(static_cast<int>(i)) ? 1 : 0;
    require(audibleCount == bankPresets, "a sampled preset is silent");

    // Every sampled preset plays the full piano roll: SoundFont key ranges
    // are octave-transposed into range instead of going silent.
    // One engine per preset, each pitch isolated by stop + panic (the 85-preset
    // table made an engine per pitch too slow for ctest's timeout).
    for (int preset = 1; preset < static_cast<int>(sonora::instruments.size()); ++preset)
    {
        if (!sonora::isBankInstrument(preset))
            continue;
        auto sweepEngine = std::make_unique<sonora::AudioEngine>();
        sweepEngine->prepare(48000.0);
        juce::AudioBuffer<float> sweepBuffer(2, 512);
        for (int pitch : { 0, 12, 21, 36, 60, 84, 96, 108, 120, 127 })
        {
            auto variant = project;
            variant.tracks[0].instrumentPreset = preset;
            variant.tracks[0].melodies[0].notes[0].pitch = pitch;
            sweepEngine->stop();
            sweepEngine->panic();
            sweepEngine->process({ &sweepBuffer, 0, 512 });
            require(sweepEngine->submit(variant), "sweep project rejected");
            sweepEngine->setPlaying(true);
            float level = 0.0f;
            for (int block = 0; block < 40; ++block)
            {
                sweepEngine->process({ &sweepBuffer, 0, 512 });
                level = std::max(level, sweepBuffer.getMagnitude(0, 0, 512));
            }
            // No digital silence anywhere on the roll. (Extremes on some
            // instruments are quiet by nature of the samples, like the
            // real thing: a synth bass at MIDI 127 settles near -60 dB. A
            // fresh engine reads ~25% hotter there only because its track
            // gain ramps down from 1.0 to the fader during the attack.)
            require(level > 0.0004f, ("sampled preset silent out of range: preset "
                + juce::String(preset) + " pitch " + juce::String(pitch)).toRawUTF8());
        }
    }

    // Panic cuts sampled voices at once; no release tail in the next block.
    {
        auto piano = project;
        piano.tracks[0].instrumentPreset = 1;
        auto engineStorage = std::make_unique<sonora::AudioEngine>();
        auto& engine = *engineStorage;
        engine.prepare(48000.0);
        require(engine.submit(piano), "piano project rejected");
        engine.setPlaying(true);
        juce::AudioBuffer<float> buffer(2, 512);
        for (int i = 0; i < 10; ++i)
            engine.process({ &buffer, 0, 512 });
        require(buffer.getMagnitude(0, 512) > 0.01f, "piano silent before panic");
        engine.stop();
        engine.panic();
        engine.process({ &buffer, 0, 512 });
        require(buffer.getMagnitude(0, 512) == 0.0f, "piano rang after panic");
    }

    // Another track's insert effects must never process this track's audio.
    {
        auto dry = project;
        dry.tracks[0].instrumentPreset = 1;
        auto wet = dry;
        wet.tracks[1].fx.eq.low = 15.0f;
        wet.tracks[1].fx.eq.high = 15.0f;
        wet.tracks[1].fx.reverb.mix = 1.0f;
        require(render(dry, 30) == render(wet, 30), "drum FX leaked into instrument track");
    }

    // Bounced WAVs carry the sampled instrument, not the sine.
    {
        sonora::ExportJob job;
        job.project = project;
        job.project.tracks[0].instrumentPreset = 13; // trumpet
        const auto sampled = sonora::OfflineExport::render(job);
        job.project.tracks[0].instrumentPreset = 0;
        const auto sineBounce = sonora::OfflineExport::render(job);
        require(sampled.ok() && sineBounce.ok() && sampled.peak > 0.01f, "instrument export failed");
        bool differs = false;
        for (int s = 0; s < std::min(sampled.audio.getNumSamples(), sineBounce.audio.getNumSamples()) && !differs; ++s)
            differs = std::abs(sampled.audio.getSample(0, s) - sineBounce.audio.getSample(0, s)) > 1.0e-4f;
        require(differs, "export ignored the instrument preset");
    }

    // v10 persistence, v9 migration to Sine Keys, and malformed presets.
    auto saved = project;
    saved.tracks[0].instrumentPreset = 10;
    saved.tracks[2].id = 3;
    saved.tracks[2].kind = sonora::TrackKind::Synth;
    saved.tracks[2].setTrackName("Horns");
    saved.tracks[2].instrumentPreset = 16;
    const auto json = sonora::ProjectIO::encode(saved);
    sonora::ProjectState loaded;
    require(json.contains("\"version\": 22"), "instrument projects must save as v22");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == saved, "instrument round-trip failed");
    auto legacy = juce::JSON::parse(json);
    legacy.getDynamicObject()->setProperty("version", 9);
    toPreV13Song(legacy);
    for (auto& track : *legacy.getDynamicObject()->getProperty("tracks").getArray())
        track.getDynamicObject()->removeProperty("preset");
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk()
            && loaded.tracks[0].instrumentPreset == 0 && loaded.tracks[2].instrumentPreset == 0,
            "v9 projects must open with Sine Keys");
    for (const auto bad : { juce::var(-1), juce::var(static_cast<int>(sonora::instruments.size())), juce::var("piano") })
    {
        auto document = juce::JSON::parse(json);
        document.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
            .getDynamicObject()->setProperty("preset", bad);
        loaded = saved;
        require(sonora::ProjectIO::decode(juce::JSON::toString(document), loaded).failed(), "bad preset accepted");
        require(loaded == saved, "bad preset destroyed current state");
    }
    auto missing = juce::JSON::parse(json);
    missing.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("preset");
    require(sonora::ProjectIO::decode(juce::JSON::toString(missing), loaded).failed(), "v10 without preset accepted");
    auto invalid = saved;
    invalid.tracks[0].instrumentPreset = static_cast<int>(sonora::instruments.size());
    require(!invalid.valid(), "out-of-range preset validated");
}

// ---- Agent: actions, parameters, parsing ---------------------------------------------------
void testAgentActions()
{
    using namespace sonora;
    namespace ag = sonora::agent;
    const std::vector<juce::String> library { "Drums/Kick 01.wav", "Pad swell.flac", "Vocal chop.wav" };
    auto makeProject = [] { return std::make_unique<ProjectState>(fixture()); };
    auto contextFor = [&](int selected = 0) {
        ag::Context context;
        context.selectedTrack = selected;
        context.library = library;
        return context;
    };
    // Runs model-style JSON through parse + apply, exactly as the app does.
    auto act = [&](ProjectState& project, const char* json, const ag::Context& context) {
        juce::StringArray errors;
        const auto actions = ag::parseActions(juce::JSON::parse(json), errors);
        return ag::applyActions(project, actions, context);
    };
    auto ok = [](const ag::Report& report, const char* what) {
        require(report.failed.isEmpty(), (juce::String(what) + ": " + report.failed.joinIntoString(" | ")).toRawUTF8());
    };

    // ---- registry ----
    {
        const auto& specs = ag::parameters();
        require(specs.size() >= 79, "parameter registry shrank");
        std::set<juce::String> names;
        for (const auto& spec : specs)
            require(names.insert(spec.name).second, ("duplicate parameter " + spec.name).toRawUTF8());
        require(ag::findParam("MIX.VOLUME") != nullptr && ag::findParam("nope") == nullptr, "param lookup wrong");
        const auto reference = ag::describeParameters();
        require(reference.contains("mix: volume[0..1.5, default 0.8]") && reference.contains("synth: wave[Sine/Triangle/Saw/Square/Pulse/Noise")
                    && reference.contains("fx.eq:") && reference.contains("loop[on/off, default off]")
                    && reference.contains("rootNote[0..127") && reference.contains("master:"),
                "parameter reference for the prompt is incomplete");
    }
    {
        // Every parameter, at both extremes, through the real action path, on
        // an instrument track, a drum track, and project-wide: the project
        // stays valid and nothing is rejected.
        for (const auto& spec : ag::parameters())
            for (const float extreme : { spec.min, spec.max })
            {
                for (int track : { 0, 1 })
                {
                    if (spec.perTrack && track == 1 && (spec.name.startsWith("synth.") || spec.name.startsWith("sampler.")))
                        continue; // instrument-only parameters
                    if (!spec.perTrack && track == 1)
                        continue;
                    auto project = makeProject();
                    juce::DynamicObject::Ptr object = new juce::DynamicObject();
                    object->setProperty("op", "set_param");
                    object->setProperty("param", spec.name);
                    object->setProperty("value", static_cast<double>(extreme));
                    object->setProperty("track", track);
                    juce::Array<juce::var> list;
                    list.add(juce::var(object.get()));
                    juce::StringArray errors;
                    const auto actions = ag::parseActions(juce::var(list), errors);
                    const auto report = ag::applyActions(*project, actions, contextFor());
                    require(report.failed.isEmpty() && project->valid(),
                            ("parameter extreme rejected: " + spec.name + " = " + juce::String(extreme)).toRawUTF8());
                    require(std::abs(spec.get(*project, track) - extreme) < 1.0e-3f
                                || spec.name.startsWith("sampler.start") || spec.name.startsWith("sampler.end")
                                || spec.name.startsWith("sampler.loop"),
                            ("parameter did not take its value: " + spec.name).toRawUTF8());
                }
            }
    }

    // ---- parsing hostile and sloppy input ----
    {
        juce::StringArray errors;
        require(ag::parseActions(juce::var(), errors).empty(), "non-array parsed");
        const auto parsed = ag::parseActions(juce::JSON::parse(
            R"([ {"op":"set_tempo","value":99}, 5, "x", {"nope":1}, {"op":"set_param","track":"two"},
                 {"op":"write_melody","track":0,"loop":"b","notes":[
                     {"start":-50,"duration":999999,"pitch":300,"velocity":0},
                     {"start":10,"pitch":"x"}, "junk", {"start":480,"duration":240,"pitch":64,"velocity":90}]},
                 {"op":"write_drums","hits":[{"pad":99,"step":999,"velocity":500},{"pad":1}]},
                 {"op":"set_automation","points":[{"tick":9e9,"value":0.5},{"tick":5,"value":"x"}]},
                 {"op":"NOTHING_REAL"} ])"), errors);
        require(parsed.size() == 5 && errors.size() == 4, "parse kept or dropped the wrong actions");
        require(parsed[0].op == "set_tempo" && parsed[0].value && *parsed[0].value == 99.0, "value not parsed");
        const auto& melody = parsed[1];
        require(melody.loop == 1 && melody.hasNotes && melody.notes.size() == 2, "notes not sanitized by count");
        require(melody.notes[0].start == 0 && melody.notes[0].pitch == 127 && melody.notes[0].velocity == 1
                    && melody.notes[0].start + melody.notes[0].duration <= patternTicks,
                "note not clamped");
        require(parsed[2].hits.size() == 1 && parsed[2].hits[0].pad == 7 && parsed[2].hits[0].step == gridSteps - 1
                    && parsed[2].hits[0].velocity == 127,
                "hit not clamped");
        require(parsed[3].points.size() == 1 && parsed[3].points[0].tick == patternTicks, "point not clamped");
        std::string many = "[";
        for (int i = 0; i < ag::maxActions + 30; ++i)
            many += std::string(i ? "," : "") + R"({"op":"stop"})";
        many += "]";
        errors.clear();
        require(static_cast<int>(ag::parseActions(juce::JSON::parse(many), errors).size()) == ag::maxActions
                    && errors.size() == 1,
                "action cap not enforced");
    }

    // ---- name parsers ----
    require(ag::parsePitchClass("Bb") == 10 && ag::parsePitchClass("f#4") == 6 && ag::parsePitchClass("c") == 0
                && ag::parsePitchClass("Cb") == 11 && !ag::parsePitchClass("H") && !ag::parsePitchClass(""),
            "pitch class parsing wrong");
    require(ag::parseScale("minor") == MusicScale::NaturalMinor && ag::parseScale("Blues") == MusicScale::Blues
                && ag::parseScale("minor pentatonic") == MusicScale::MinorPentatonic && !ag::parseScale("lydian"),
            "scale parsing wrong");
    require(ag::parseChordType("m7") == ChordType::Min7 && ag::parseChordType("Major") == ChordType::Major
                && ag::parseChordType("7") == ChordType::Dom7 && !ag::parseChordType("13"),
            "chord parsing wrong");
    require(ag::parseSongPart("pre-chorus") == SongPart::PreChorus && ag::parseSongPart("DROP") == SongPart::Drop
                && !ag::parseSongPart("coda"),
            "part parsing wrong");
    require(ag::parseTemplate("Hip-hop") == SongTemplate::HipHop && ag::parseTemplate("edm") == SongTemplate::Edm
                && !ag::parseTemplate("jazz"),
            "template parsing wrong");
    require(ag::parseAutomationTarget("send reverb") == AutomationTarget::SendReverb && !ag::parseAutomationTarget("x"),
            "automation target parsing wrong");
    require(ag::parseLoop("c") == 2 && ag::parseLoop("3") == 3 && ag::parseLoop("E") == -1 && ag::parseLoop("AB") == -1,
            "loop parsing wrong");

    // ---- sound resolution ----
    {
        auto name = [&](const char* text) {
            const auto entry = ag::resolveSound(text, library);
            return entry ? entry->name : juce::String("<none>");
        };
        require(name("Grand Piano") == "Grand Piano" && name("grand piano") == "Grand Piano", "exact sound not found");
        require(name("alto sax") == "Alto Sax" && name("xylophone") == "Xylophone", "word match failed");
        require(name("super saw") == "Super Saw" && name("Synth: Super Saw") == "Super Saw", "patch not found");
        require(name("Sonora Synth") == "Sonora Synth (editable)" && name("synth") == "Sonora Synth (editable)",
                "synth aliases wrong");
        require(name("sine keys") == "Sine Keys", "an exact patch name must beat the synth alias");
        require(name("kick 01") == "Drums/Kick 01.wav" && name("Sampler: Pad swell") == "Pad swell.flac",
                "library sample not found by stem");
        require(name("sampler") == "Sampler", "sampler instrument not found");
        juce::StringArray suggestions;
        require(!ag::resolveSound("flute zzz", library, &suggestions) && !suggestions.isEmpty(),
                "no suggestions for a near miss");
        require(!ag::resolveSound("", library), "empty sound resolved");
    }

    // ---- global settings ----
    {
        auto project = makeProject();
        auto report = act(*project, R"([{"op":"set_tempo","value":999},{"op":"set_key","key":"Bb","scale":"minor"}])",
                          contextFor());
        ok(report, "tempo/key");
        require(project->bpm == 240.0 && project->musicKey == 10 && project->musicScale == MusicScale::NaturalMinor
                    && report.changed,
                "tempo or key not applied");
        report = act(*project, R"([{"op":"set_key","key":"H"},{"op":"set_tempo"},{"op":"set_key","key":"C","scale":"lydian"}])",
                     contextFor());
        require(report.failed.size() == 3 && report.done.isEmpty(), "bad global actions not all reported");
        require(project->musicKey == 10, "failed set_key changed the key");
    }

    // ---- generic parameters ----
    {
        auto project = makeProject();
        auto report = act(*project,
            R"([{"op":"set_param","track":0,"param":"mix.volume","value":0.5},
                {"op":"set_param","track":0,"param":"mix.mute","text":"on"},
                {"op":"set_param","track":0,"param":"synth.wave","text":"Saw"},
                {"op":"set_param","track":1,"param":"fx.reverb.mix","value":0.4},
                {"op":"set_param","param":"sends.reverbReturn","value":9},
                {"op":"set_param","param":"project.tempo","value":96}])", contextFor());
        ok(report, "parameters");
        require(project->tracks[0].mix.volume == 0.5f && project->tracks[0].mix.mute
                    && project->tracks[0].synth.wave == WaveSaw && project->tracks[1].fx.reverb.mix == 0.4f
                    && project->sends.reverbReturn == 1.5f && project->bpm == 96.0,
                "parameters not applied or clamped");
        report = act(*project,
            R"([{"op":"set_param","track":1,"param":"synth.cutoff","value":900},
                {"op":"set_param","param":"mix.volumee","value":1},
                {"op":"set_param","param":"mix.pan"},
                {"op":"set_param","track":0,"param":"synth.wave","text":"fuzzy"},
                {"op":"set_param","track":7,"param":"mix.volume","value":1}])", contextFor());
        require(report.failed.size() == 5, "bad parameter actions not all reported");
        require(report.failed[1].contains("did you mean"), "no parameter suggestion");
        // Omitted track: the selected track.
        report = act(*project, R"([{"op":"set_param","param":"mix.pan","value":-0.5}])", contextFor(1));
        require(project->tracks[1].mix.pan == -0.5f && project->tracks[0].mix.pan == 0.0f, "default track ignored");
        // Sampler regions stay valid whatever order the model sets them in.
        report = act(*project, R"([{"op":"set_param","track":0,"param":"sampler.start","value":0.9},
                                   {"op":"set_param","track":0,"param":"sampler.end","value":0.2}])", contextFor());
        ok(report, "sampler regions");
        require(project->valid() && project->tracks[0].sampler.end > project->tracks[0].sampler.start, "regions inverted");
        const auto changed = ag::describeChangedParameters(*project, 0);
        require(changed.contains("mix.volume=0.5") && changed.contains("mix.mute=on") && changed.contains("synth.wave=Saw")
                    && !changed.contains("sampler.") && !changed.contains("fx.reverb"),
                "changed-parameter summary wrong");
        require(ag::describeChangedParameters(*project, 1).contains("fx.reverb.mix=0.4")
                    && !ag::describeChangedParameters(*project, 1).contains("synth."),
                "drum track summary wrong");
    }

    // ---- tracks and sounds ----
    {
        auto project = makeProject();
        auto report = act(*project,
            R"([{"op":"add_track","kind":"synth","name":"Lead","instrument":"alto sax"},
                {"op":"add_track","kind":"drums"},
                {"op":"set_instrument","track":2,"instrument":"super saw"},
                {"op":"add_track","kind":"synth","instrument":"kick 01"}])", contextFor());
        ok(report, "tracks");
        require(project->tracks[2].kind == TrackKind::Synth && project->tracks[2].trackName() == "Lead"
                    && project->tracks[2].synth == synthPatches()[10].params && project->tracks[2].instrumentPreset == 0,
                "new track or patch wrong");
        require(project->tracks[3].kind == TrackKind::Drums && project->tracks[3].trackName() == "Drums 4",
                "drum track wrong");
    }
    {
        auto project = makeProject();
        auto report = act(*project,
            R"([{"op":"add_track","kind":"synth","instrument":"kick 01"},
                {"op":"set_instrument","track":0,"instrument":"Violin"},
                {"op":"set_instrument","track":0,"instrument":"zzzzqq"},
                {"op":"set_instrument","track":1,"instrument":"Violin"},
                {"op":"set_kit","track":1,"name":"Boom"},
                {"op":"set_kit","track":0,"name":"Boom"},
                {"op":"set_kit","track":1,"name":"Imaginary"}])", contextFor());
        require(report.failed.size() == 4, "wrong failures for instrument/kit actions");
        require(project->tracks[2].instrumentPreset == samplerInstrument
                    && project->tracks[2].samplerFileName() == "Drums/Kick 01.wav"
                    && project->tracks[2].trackName() == "Kick 01",
                "sample sound not applied");
        require(juce::String(instruments[static_cast<std::size_t>(project->tracks[0].instrumentPreset)].name) == "Violin",
                "instrument not applied");
        require(project->tracks[1].kitVariant == 4, "kit not applied");
        require(report.failed[0].contains("closest") || report.failed[0].contains("no sound matches"),
                "failed sound lacks guidance");
        // Names follow instruments only when Sonora generated them.
        project->tracks[0].setTrackName("My lead");
        act(*project, R"([{"op":"set_instrument","track":0,"instrument":"Cello"}])", contextFor());
        require(project->tracks[0].trackName() == "My lead", "custom name overwritten");
        project->tracks[0].setTrackName("Cello");
        act(*project, R"([{"op":"set_instrument","track":0,"instrument":"Harp"}])", contextFor());
        require(project->tracks[0].trackName() == "Harp", "generated name did not follow");
        // Capacity, rename, move, remove.
        for (int i = 0; i < 8; ++i)
            act(*project, R"([{"op":"add_track","kind":"synth"}])", contextFor());
        int usable = 0;
        for (const auto& track : project->tracks)
            usable += track.kind != TrackKind::None ? 1 : 0;
        require(usable == 8, "track cap not reached");
        report = act(*project, R"([{"op":"add_track","kind":"drums"}])", contextFor());
        require(report.failed.size() == 1 && report.failed[0].contains("8 tracks"), "9th track accepted");
        report = act(*project,
            R"([{"op":"rename_track","track":0,"name":"Bass\u0007 line"},{"op":"move_track","track":0,"to":1},
                {"op":"remove_track","track":3},{"op":"add_track","kind":"drums","name":"Perc"}])", contextFor());
        ok(report, "rename/move/remove");
        require(project->tracks[1].trackName() == "Bass line" && project->tracks[3].trackName() == "Perc"
                    && project->tracks[3].kind == TrackKind::Drums,
                "rename/move/remove wrong (a new track takes the lowest free slot)");
        // The last track can never be removed.
        auto single = makeProject();
        single->tracks[1] = Track {};
        report = act(*single, R"([{"op":"remove_track","track":0}])", contextFor());
        require(report.failed.size() == 1 && single->tracks[0].kind == TrackKind::Synth, "last track removed");
    }

    // ---- patterns ----
    {
        auto project = makeProject();
        ag::Context context = contextFor();
        context.loopSlot[0] = 2;
        auto report = act(*project,
            R"([{"op":"write_melody","track":0,"loop":"A","notes":[{"start":0,"duration":480,"pitch":60,"velocity":100},
                                                                  {"start":240,"duration":480,"pitch":60,"velocity":90},
                                                                  {"start":960,"duration":480,"pitch":64,"velocity":100}]},
                {"op":"write_melody","track":0,"loop":"B","notes":[{"start":0,"duration":960,"pitch":67,"velocity":100}]},
                {"op":"write_melody","track":0,"notes":[{"start":0,"duration":960,"pitch":72,"velocity":100}]},
                {"op":"write_melody","track":0,"loop":"D","notes":[]},
                {"op":"write_drums","track":1,"loop":"B","hits":[{"pad":0,"step":0,"velocity":110},{"pad":0,"step":0,"velocity":90},{"pad":2,"step":4}]},
                {"op":"write_drums","track":0,"hits":[{"pad":0,"step":0}]},
                {"op":"write_melody","track":1,"notes":[{"start":0,"pitch":60}]}])", context);
        require(report.failed.size() == 2, "wrong failures writing patterns");
        const auto& synth = project->tracks[0];
        require(synth.melodies[0].count == 3 && synth.melodies[0].notes[0].start == 0
                    && synth.melodies[0].notes[0].duration == 240 && synth.melodies[0].valid(),
                "overlapping same-pitch notes not trimmed into a valid pattern");
        require(synth.melodies[1].count == 1 && synth.melodies[1].notes[0].pitch == 67, "loop B not written");
        require(synth.melodies[2].count == 1 && synth.melodies[2].notes[0].pitch == 72, "omitted loop did not use the context slot");
        require(synth.melodies[3].count == 0, "empty notes did not clear");
        require(project->tracks[1].drumPatterns[1].steps[0][0] == 110 && project->tracks[1].drumPatterns[1].hitCount() == 2,
                "drum hits wrong (strongest velocity wins)");
        // Edits.
        report = act(*project,
            R"([{"op":"copy_loop","track":0,"loop":"A","toLoop":"C"},
                {"op":"transpose_loop","track":0,"loop":"C","value":12},
                {"op":"transpose_loop","track":0,"loop":"B","value":100},
                {"op":"velocity_ramp","track":0,"loop":"A","value":40,"value2":120},
                {"op":"copy_loop","track":0,"loop":"A","toLoop":"A"},
                {"op":"transpose_loop","track":0,"loop":"A"},
                {"op":"clear_loop","track":1,"loop":"B"}])", context);
        require(report.failed.size() == 2, "wrong failures editing patterns");
        require(project->tracks[0].melodies[2].count == 3 && project->tracks[0].melodies[2].notes[0].pitch == 72,
                "copy then transpose wrong");
        require(project->tracks[0].melodies[1].notes[0].pitch == 67 + 48, "transpose shift not capped at 48 semitones");
        act(*project, R"([{"op":"transpose_loop","track":0,"loop":"B","value":48}])", context);
        require(project->tracks[0].melodies[1].notes[0].pitch == 127, "transpose did not clamp to MIDI range");
        require(project->tracks[0].melodies[0].notes[0].velocity == 40 && project->tracks[0].melodies[0].notes[2].velocity == 120,
                "velocity ramp wrong");
        require(project->tracks[1].drumPatterns[1].hitCount() == 0, "clear_loop did not clear drums");
        // Quantize pulls off-grid notes onto the grid; humanize is reproducible.
        act(*project, R"([{"op":"write_melody","track":0,"loop":"A","notes":[{"start":200,"duration":240,"pitch":60,"velocity":100}]}])", context);
        act(*project, R"([{"op":"quantize_loop","track":0,"loop":"A"}])", context);
        require(project->tracks[0].melodies[0].notes[0].start == 240, "quantize did not snap");
        auto a = std::make_unique<ProjectState>(*project), b = std::make_unique<ProjectState>(*project);
        act(*a, R"([{"op":"humanize_loop","track":0,"loop":"C","value":0.8,"value2":0.8}])", context);
        act(*b, R"([{"op":"humanize_loop","track":0,"loop":"C","value":0.8,"value2":0.8}])", context);
        require(*a == *b && !(*a == *project), "humanize not reproducible or did nothing");
        require(project->valid() && a->valid(), "pattern edits broke validity");
    }

    // ---- arrangement ----
    {
        auto project = makeProject();
        act(*project, R"([{"op":"add_track","kind":"synth","name":"Bass"}])", contextFor());
        auto report = act(*project,
            R"([{"op":"set_song","sections":[
                  {"part":"Intro","tracks":[{"track":0,"loop":"A"}]},
                  {"part":"Verse","tracks":[{"track":0,"loop":"B"},{"track":1,"loop":"A"},{"track":2,"loop":"A"},{"track":5,"loop":"A"}],"chordKey":"A","chordType":"minor"},
                  {"part":"Chorus","tracks":[{"track":0,"loop":"C"},{"track":1,"loop":"B"},{"track":2,"loop":"off"}],"chordKey":"F"},
                  {"part":"???","tracks":[]}]}])", contextFor());
        ok(report, "set_song");
        const auto& song = project->song;
        require(song.sections == 4 && song.parts[0] == SongPart::Intro && song.parts[2] == SongPart::Chorus
                    && song.parts[3] == SongPart::Section,
                "song sections wrong");
        require(song.trackOn[0][0] && !song.trackOn[0][1] && song.trackOn[1][1] && song.slots[1][0] == 1
                    && song.slots[2][0] == 2 && !song.trackOn[2][2] && !song.trackOn[3][0],
                "song cells wrong (unlisted tracks must be silent)");
        require(song.chords[1].root == 9 && song.chords[1].type == ChordType::Minor && song.chords[2].root == 5
                    && !song.chords[0].set(),
                "song chords wrong");
        require(report.done[0].contains("ignoring 1"), "ignored track not mentioned");
        report = act(*project,
            R"([{"op":"set_part","section":0,"part":"Verse"},
                {"op":"set_chord","section":0,"key":"G","chordType":"7"},
                {"op":"set_chord","section":1,"key":"none"},
                {"op":"set_cell","section":0,"track":1,"loop":"C"},
                {"op":"set_cell","section":0,"track":0,"on":false},
                {"op":"duplicate_section","section":0},
                {"op":"move_section","section":0,"to":3},
                {"op":"add_section"},
                {"op":"remove_section","section":9},
                {"op":"set_part","section":99,"part":"Verse"},
                {"op":"set_part","section":0,"part":"coda"}])", contextFor());
        require(report.failed.size() == 3, "wrong failures editing the arrangement");
        require(project->song.sections == 6, "section add/duplicate wrong");
        report = act(*project, R"([{"op":"apply_template","name":"edm"}])", contextFor());
        ok(report, "template");
        require(project->song.sections == 7 && project->song.parts[2] == SongPart::Drop, "template not applied");
        report = act(*project, R"([{"op":"apply_template","name":"polka"},{"op":"set_song","sections":[]}])", contextFor());
        require(report.failed.size() == 2 && project->song.sections == 7, "bad song/template changed the arrangement");
        for (int i = 0; i < 12; ++i)
            act(*project, R"([{"op":"add_section"}])", contextFor());
        require(project->song.sections == maxSections, "section cap not enforced");
        require(project->valid(), "arrangement edits broke validity");
    }

    // ---- automation, live FX, takes ----
    {
        auto project = makeProject();
        project->takeCount = 1;
        project->takes[0].id = 7;
        project->takes[0].setFileName("take7.wav");
        project->takes[0].frames = 4800;
        std::string many = R"({"op":"set_automation","track":0,"loop":"B","target":"Volume","points":[{"tick":9999,"value":9})";
        for (int i = 0; i < 40; ++i)
            many += ",{\"tick\":" + std::to_string(i * 100) + ",\"value\":0.5}";
        many += "]}";
        auto report = act(*project, juce::String("[" + many + R"(,
            {"op":"set_automation","track":1,"target":"Pan","points":[{"tick":0,"value":-1},{"tick":100,"value":1}]},
            {"op":"set_automation","track":0,"target":"Bogus","points":[]},
            {"op":"set_automation","track":0,"loop":"C","target":"Pan"},
            {"op":"set_live_fx","track":0,"arp":"up-down","rate":"1/8T","value":2,"latch":true,"chordType":"min7"},
            {"op":"set_live_fx","track":1,"arp":"Up"},
            {"op":"set_live_fx","track":0,"arp":"sideways"},
            {"op":"set_take","take":7,"param":"mute","on":true},
            {"op":"set_take","take":7,"param":"stretch","value":9},
            {"op":"set_take","take":7,"param":"gain","value":0.5},
            {"op":"set_take","take":99,"param":"mute","on":true}])").toRawUTF8(), contextFor());
        require(report.failed.size() == 5, ("wrong failures: " + report.failed.joinIntoString(" | ")).toRawUTF8());
        const auto& volume = project->tracks[0].automation[1][static_cast<std::size_t>(AutomationTarget::Volume)];
        require(volume.count == maxAutomationPoints && volume.valid(), "automation not capped or sorted");
        for (int i = 0; i < volume.count; ++i)
            require(volume.points[static_cast<std::size_t>(i)].value <= 1.5f, "automation value not clamped");
        const auto& pan = project->tracks[1].automation[0][static_cast<std::size_t>(AutomationTarget::Pan)];
        require(pan.count == 2 && pan.points[0].value == -1.0f && pan.points[1].value == 1.0f, "pan lane wrong");
        const auto& fx = project->tracks[0].liveFx;
        require(fx.arp == ArpMode::UpDown && fx.rate == ArpRate::EighthTriplet && fx.octaves == 2 && fx.latch
                    && fx.chordOn && fx.chord == ChordType::Min7,
                "live FX wrong");
        require(project->takes[0].mute && project->takes[0].stretch == 2.0f && project->takes[0].gain == 0.5f,
                "take settings wrong");
        report = act(*project, R"([{"op":"clear_automation","track":0,"loop":"B","target":"volume"},
                                   {"op":"clear_automation","track":1}])", contextFor());
        ok(report, "clear automation");
        require(project->tracks[0].automation[1][0].count == 0 && pan.count == 0, "automation not cleared");
    }

    // ---- app effects and the sole-action rule ----
    {
        auto project = makeProject();
        auto report = act(*project,
            R"([{"op":"play","section":2},{"op":"set_view","view":"Sound editor"},{"op":"select_track","track":1},
                {"op":"set_view","view":"hologram"},{"op":"select_track","track":6},{"op":"edit_section","section":0},
                {"op":"save_project"},{"op":"export_audio"},{"op":"stop"},{"op":"panic"},{"op":"undo"}])", contextFor());
        require(report.effects.size() == 8 && report.failed.size() == 3, "app effects or failures wrong");
        require(report.effects[0].kind == ag::UiEffect::Kind::Play && report.effects[0].number == 2
                    && report.effects[1].kind == ag::UiEffect::Kind::SetView && report.effects[1].text == "soundeditor",
                "effects out of order");
        require(!report.changed, "app effects changed the project");
        report = act(*project, R"([{"op":"undo"}])", contextFor());
        require(report.failed.isEmpty() && report.effects.size() == 1 && report.effects[0].kind == ag::UiEffect::Kind::Undo,
                "sole undo refused");
        report = act(*project, R"([{"op":"new_project"}])", contextFor());
        require(report.effects.size() == 1 && report.effects[0].kind == ag::UiEffect::Kind::NewProject, "sole new_project refused");
        report = act(*project, R"([{"op":"set_tempo","value":100},{"op":"new_project"}])", contextFor());
        require(report.failed.size() == 1 && project->bpm == 100.0 && report.effects.empty(),
                "new_project allowed alongside other actions");
        report = act(*project, R"([{"op":"teleport"}])", contextFor());
        require(report.failed.size() == 1 && report.failed[0].contains("unknown action"), "unknown op not reported");
        // An empty or failing batch leaves the project untouched and says so.
        auto before = std::make_unique<ProjectState>(*project);
        report = act(*project, R"([])", contextFor());
        require(!report.changed && *project == *before, "empty batch changed the project");
    }

    // ---- a whole request, in order: new track, its sound, four loops, a song ----
    {
        auto project = makeProject();
        auto report = act(*project,
            R"([{"op":"add_track","kind":"synth","name":"Hook","instrument":"Vibraphone"},
                {"op":"write_melody","track":2,"loop":"A","notes":[{"start":0,"duration":960,"pitch":72,"velocity":100}]},
                {"op":"write_melody","track":2,"loop":"B","notes":[{"start":0,"duration":960,"pitch":74,"velocity":100}]},
                {"op":"write_melody","track":2,"loop":"C","notes":[{"start":0,"duration":960,"pitch":76,"velocity":100}]},
                {"op":"write_melody","track":2,"loop":"D","notes":[{"start":0,"duration":960,"pitch":79,"velocity":100}]},
                {"op":"set_song","sections":[{"part":"Verse","tracks":[{"track":2,"loop":"A"}]},
                                              {"part":"Chorus","tracks":[{"track":2,"loop":"D"},{"track":1,"loop":"A"}]}]},
                {"op":"set_param","track":2,"param":"mix.sendReverb","value":0.5}])", contextFor());
        ok(report, "whole request");
        require(report.done.size() == 7 && project->valid() && report.changed
                    && project->tracks[2].melodies[3].notes[0].pitch == 79 && project->song.sections == 2
                    && project->tracks[2].mix.sendReverb == 0.5f,
                "a multi-step request did not land as one coherent edit");
        // And it survives a save/load round-trip.
        auto loaded = std::make_unique<ProjectState>();
        require(ProjectIO::decode(ProjectIO::encode(*project), *loaded).wasOk() && *loaded == *project,
                "agent-built project did not round-trip");
    }
}

// ---- Agent: prompt, schema, context, and the full pipeline -------------------------------------
void testAgentPipeline()
{
    using namespace sonora;
    namespace ai = sonora::ai;
    namespace ag = sonora::agent;

    // The schema, the prompt, and the executor must agree on the set of actions.
    const auto schema = juce::JSON::parse(ai::agentSchema());
    require(schema.getDynamicObject() != nullptr, "agent schema is not JSON");
    const auto* ops = schema.getProperty("properties", {}).getProperty("actions", {}).getProperty("items", {})
                          .getProperty("properties", {}).getProperty("op", {}).getProperty("enum", {}).getArray();
    require(ops != nullptr && ops->size() >= 40, "schema lost its action list");
    const auto prompt = ai::agentSystemPrompt();
    for (const auto& op : *ops)
    {
        const auto name = op.toString();
        require(prompt.contains(name), ("action missing from the prompt: " + name).toRawUTF8());
        // The executor knows it: whatever else goes wrong, never "unknown action".
        auto project = std::make_unique<ProjectState>(fixture());
        ag::Action action;
        action.op = name;
        const auto report = ag::applyActions(*project, { action }, ag::Context {});
        for (const auto& failure : report.failed)
            require(!failure.contains("unknown action"), ("schema action the executor does not know: " + name).toRawUTF8());
    }
    {
        // The reverse: every op the executor handles is offered in the schema.
        juce::StringArray offered;
        for (const auto& op : *ops)
            offered.add(op.toString());
        for (const char* known : { "set_tempo", "set_param", "write_melody", "write_drums", "set_song", "set_instrument",
                                   "set_automation", "set_live_fx", "set_take", "play", "undo", "take_to_sampler" })
            require(offered.contains(known), ("executor action not offered to the model: " + juce::String(known)).toRawUTF8());
    }
    // The prompt is the model's whole manual: it must teach every capability.
    for (const char* phrase : { "ONE undo step", "Soprano Sax", "Super Saw", "Starter, Deep, Crisp, Tight, Boom, Warm",
                                "mix: volume[0..1.5", "sampler.", "synth.cutoff", "Write N melodies", "960 ticks per quarter",
                                "pad", "only ever as the single action", "ONLY if the producer explicitly asks to record" })
        require(prompt.contains(phrase), ("prompt is missing: " + juce::String(phrase)).toRawUTF8());
    for (const auto& character : prompt)
        require(character < 0x80, "prompt contains a non-ASCII character");

    // Sandbox: no tools, no MCP, no settings; schema and prompt travel as arguments.
    const auto args = ai::agentArguments();
    require(args.indexOf("--tools") >= 0 && args[args.indexOf("--tools") + 1].isEmpty() && args.contains("--strict-mcp-config")
                && args.contains("--no-session-persistence") && args.contains("--json-schema"),
            "agent not sandboxed");

    // ---- the context the model sees ----
    auto request = std::make_unique<ai::AgentRequest>();
    request->project = fixture();
    auto& project = request->project;
    project.tracks[1].setTrackName("Beat");
    project.song.sections = 2;
    project.song.parts[1] = SongPart::Chorus;
    project.song.chords[1] = { 9, ChordType::Minor };
    project.musicKey = 2;
    project.musicScale = MusicScale::Dorian;
    project.tracks[0].synth.cutoff = 900.0f;
    project.tracks[0].liveFx.arp = ArpMode::Up;
    auto& lane = project.tracks[0].automation[0][static_cast<std::size_t>(AutomationTarget::Volume)];
    lane.count = 2;
    lane.points[0] = { 0, 0.5f };
    lane.points[1] = { 7680, 1.0f };
    project.takeCount = 1;
    project.takes[0].id = 3;
    project.takes[0].setFileName("take3.wav");
    project.takes[0].frames = 96000;
    project.takes[0].mute = true;
    request->selectedTrack = 1;
    request->loopSlot[0] = 2;
    request->message = "Make it \x01" "bounce\x07" " more"; // split: \x01b would be one hex escape
    request->library = { "Drums/Kick 01.wav", "Pad swell.flac" };
    request->history = { { true, "earlier idea" }, { false, "earlier reply" } };
    const auto message = ai::buildAgentMessage(*request);
    for (const char* phrase : { "Tempo 123 BPM", "Key: D Dorian", "View: Loop", "Stopped.", "Selected track: 1 \"Beat\"",
                                "The next add_track will be track 2", "Track 0 \"Sine Keys\" - instrument track",
                                "Track 1 \"Beat\" - drum track: kit Starter", "Showing loop C", "synth.cutoff=900",
                                "Live play: arp Up", "Loop A [", "start=0 dur=960 pitch=60", "(empty)",
                                "1. Chorus [chord Am]", "AUTOMATION", "track 0 loop A Volume: 0:0.50 7680:1.00",
                                "take 3: starts bar 1", "muted", "Drums/Kick 01.wav", "Producer: earlier idea",
                                "You: earlier reply", "Make it bounce more" })
        require(message.contains(phrase), ("agent context is missing: " + juce::String(phrase)).toRawUTF8());
    require(!message.contains("\x01") && !message.contains("\x07"), "control characters leaked into the agent context");
    request->project.takeCount = 0;
    request->playing = true;
    request->songView = true;
    request->editPart = 1;
    require(ai::buildAgentMessage(*request).contains("View: Song") && ai::buildAgentMessage(*request).contains("Playing."),
            "view and transport state missing");
    // Long loops: the selected track is listed in full, the others are capped.
    for (int t : { 0, 1 })
        project.tracks[static_cast<std::size_t>(t)].kind = TrackKind::Synth;
    project.tracks[1].id = 2;
    for (int t : { 0, 1 })
    {
        auto& loop = project.tracks[static_cast<std::size_t>(t)].melodies[0];
        loop = {};
        loop.count = 100;
        for (int i = 0; i < 100; ++i)
            loop.notes[static_cast<std::size_t>(i)] = { static_cast<std::uint32_t>(i + 1), i * 120, 60, 40 + i % 30, 90 };
    }
    request->selectedTrack = 0;
    {
        const auto longMessage = ai::buildAgentMessage(*request);
        require(longMessage.contains("(+52 more notes"), "unselected track not capped");
        require(!longMessage.substring(0, longMessage.indexOf("Track 1")).contains("more notes"),
                "selected track was truncated");
    }
    // A completely full project still produces a bounded message.
    for (int t = 0; t < maxTracks; ++t)
    {
        auto& track = project.tracks[static_cast<std::size_t>(t)];
        track.id = static_cast<std::uint32_t>(t + 1);
        track.kind = TrackKind::Synth;
        track.setTrackName("Track " + juce::String(t));
        for (auto& loop : track.melodies)
        {
            loop = {};
            loop.count = Pattern::capacity;
            for (int i = 0; i < Pattern::capacity; ++i)
                loop.notes[static_cast<std::size_t>(i)] = { static_cast<std::uint32_t>(i + 1), i * 60, 60, 30 + i % 60, 90 };
        }
    }
    require(ai::buildAgentMessage(*request).length() < ai::maxContextChars + 40000, "agent context is not bounded");

    // ---- parsing the model's reply ----
    auto envelope = [](const juce::String& body, bool structured = true) {
        return structured ? "{\"is_error\":false,\"result\":\"\",\"structured_output\":" + body + "}"
                          : "{\"is_error\":false,\"result\":" + juce::JSON::toString(juce::var("```json\n" + body + "\n```")) + "}";
    };
    {
        const auto body = R"({"reply":"Done: a tempo change.","actions":[{"op":"set_tempo","value":90},{"nope":1},{"op":"stop"}]})";
        const auto parsed = ai::parseAgentResponse(envelope(body));
        require(parsed.ok() && parsed.reply == "Done: a tempo change." && parsed.actions.size() == 2
                    && parsed.unreadable.size() == 1,
                "structured reply parsed wrong");
        const auto fenced = ai::parseAgentResponse(envelope(body, false));
        require(fenced.ok() && fenced.actions.size() == 2, "fenced fallback parse failed");
        const auto question = ai::parseAgentResponse(envelope(R"({"reply":"Which track?","actions":[]})"));
        require(question.ok() && question.actions.empty(), "a question reply was rejected");
        require(!ai::parseAgentResponse(envelope(R"({"reply":"","actions":[]})")).ok(), "empty reply accepted");
        require(!ai::parseAgentResponse("not json at all").ok(), "garbage accepted");
        const auto failed = ai::parseAgentResponse(R"({"is_error":true,"result":"Rate limited"})");
        require(!failed.ok() && failed.error.contains("Rate limited"), "CLI error not surfaced");
        const auto clean = ai::parseAgentResponse(envelope("{\"reply\":\"ok\\u0007 now\",\"actions\":[]}"));
        require(clean.ok() && clean.reply == "ok now", "control characters leaked into the reply");
    }

    // ---- a model reply, end to end: four melodies, a sound, a song, a mix move ----
    {
        juce::String notes[4];
        const int roots[4] { 72, 74, 76, 79 };
        for (int i = 0; i < 4; ++i)
            notes[i] = "[{\"start\":0,\"duration\":960,\"pitch\":" + juce::String(roots[i])
                + ",\"velocity\":100},{\"start\":1920,\"duration\":480,\"pitch\":" + juce::String(roots[i] + 4)
                + ",\"velocity\":90}]";
        const juce::String body = R"({"reply":"Added a vibraphone hook with four variations and arranged it.","actions":[)"
            R"({"op":"add_track","kind":"synth","name":"Hook","instrument":"Vibraphone"},)"
            R"({"op":"write_melody","track":2,"loop":"A","notes":)" + notes[0] + "},"
            R"({"op":"write_melody","track":2,"loop":"B","notes":)" + notes[1] + "},"
            R"({"op":"write_melody","track":2,"loop":"C","notes":)" + notes[2] + "},"
            R"({"op":"write_melody","track":2,"loop":"D","notes":)" + notes[3] + "},"
            R"({"op":"set_song","sections":[{"part":"Verse","tracks":[{"track":2,"loop":"A"},{"track":1,"loop":"A"}]},)"
            R"({"part":"Chorus","tracks":[{"track":2,"loop":"D"},{"track":1,"loop":"A"}],"chordKey":"A","chordType":"minor"}]},)"
            R"({"op":"set_param","track":2,"param":"mix.sendReverb","value":0.45},{"op":"play"}]})";
        const auto parsed = ai::parseAgentResponse(envelope(body));
        require(parsed.ok() && parsed.actions.size() == 8, "end-to-end reply did not parse");
        auto live = std::make_unique<ProjectState>(fixture());
        ag::Context context;
        context.library = { "Pad swell.flac" };
        const auto report = ag::applyActions(*live, parsed.actions, context);
        require(report.failed.isEmpty() && report.done.size() == 8 && report.effects.size() == 1
                    && report.effects[0].kind == ag::UiEffect::Kind::Play,
                ("end-to-end apply: " + report.failed.joinIntoString(" | ")).toRawUTF8());
        require(live->valid() && live->tracks[2].trackName() == "Hook" && live->song.sections == 2
                    && live->tracks[2].melodies[3].notes[0].pitch == 79 && live->tracks[2].mix.sendReverb == 0.45f,
                "end-to-end project wrong");
        for (int slot = 0; slot < 4; ++slot)
            require(live->tracks[2].melodies[static_cast<std::size_t>(slot)].count == 2, "a melody loop is missing");
    }

    // Opt-in: the real model, with its own switch so the other live tests stay quiet.
    if (juce::SystemStats::getEnvironmentVariable("SONORA_LIVE_AGENT", {}) == "1")
    {
        auto live = std::make_unique<ai::AgentRequest>();
        live->project = fixture();
        live->project.musicKey = 9;
        live->project.musicScale = MusicScale::NaturalMinor;
        live->message = "Add a vibraphone track called Hook and write four different 4-bar melodies on it in the "
                        "project key (A minor), then arrange a verse and a chorus that use them. Make the reverb "
                        "send on the Hook track bigger.";
        live->library = { "Drums/Kick 01.wav" };
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto answer = ai::runAgent(*live, nullptr);
        std::cout << "LIVE agent: " << (answer.ok() ? "ok" : answer.error) << " in "
                  << juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) << " s, "
                  << answer.actions.size() << " actions\n  reply: " << answer.reply << "\n";
        require(answer.ok() && !answer.actions.empty(), "live agent failed");
        ag::Context context;
        context.library = live->library;
        auto project2 = std::make_unique<ProjectState>(live->project);
        const auto report = ag::applyActions(*project2, answer.actions, context);
        for (const auto& line : report.done)
            std::cout << "    done: " << line << "\n";
        for (const auto& line : report.failed)
            std::cout << "    FAILED: " << line << "\n";
        int hookLoops = 0;
        for (const auto& track : project2->tracks)
            if (track.kind == TrackKind::Synth && track.trackName().containsIgnoreCase("hook"))
                for (const auto& loop : track.melodies)
                    hookLoops += loop.count > 0 ? 1 : 0;
        require(project2->valid() && hookLoops >= 3 && project2->song.sections >= 2,
                "live agent did not build what was asked");
    }
}

// ---- Instrument picker: search and favorites ------------------------------------
void testInstrumentSearch()
{
    using namespace sonora;
    const std::vector<juce::String> library { "Drums/Kick 01.wav", "Pad swell.flac", "Vocal chop.wav" };
    const auto entries = buildPickerEntries(library);
    require(entries.size() == instruments.size() + synthPatches().size() + library.size(), "picker entry count wrong");
    {
        std::set<juce::String> keys;
        for (const auto& entry : entries)
            require(keys.insert(entry.key).second, ("duplicate picker key: " + entry.key).toRawUTF8());
    }
    auto namesOf = [&](const std::vector<PickerRow>& rows) {
        juce::StringArray names;
        for (const auto& row : rows)
            if (!row.header)
                names.add(entries[static_cast<std::size_t>(row.entry)].name);
        return names;
    };
    Favorites none;

    // No query: every entry exactly once, grouped under family headings.
    {
        const auto rows = pickerRows(entries, "", none, false);
        int items = 0, headers = 0;
        for (const auto& row : rows)
            (row.header ? headers : items) += 1;
        require(items == static_cast<int>(entries.size()), "browse view dropped entries");
        require(rows.front().header && rows.front().title == "Synth", "first group is not the synth");
        require(headers >= 10, "family headings missing");
        require(pickerRows(entries, "   ", none, false).size() == rows.size(), "blank query not treated as browse");
    }
    // Ranking: names beat families, prefixes beat later words.
    require(namesOf(pickerRows(entries, "violin", none, false))[0] == "Violin", "violin not first");
    require(namesOf(pickerRows(entries, "grand piano", none, false))[0] == "Grand Piano", "grand piano not first");
    require(namesOf(pickerRows(entries, "alto sax", none, false))[0] == "Alto Sax", "alto sax not first");
    require(namesOf(pickerRows(entries, "SAX", none, false)).size() == 4, "search is not case-insensitive");
    {
        const auto bass = namesOf(pickerRows(entries, "synth bass", none, false));
        require(bass.contains("Synth Bass") && bass.contains("Synth Bass 2") && !bass.contains("Fingered Bass"),
                "every word must match");
        const auto pads = namesOf(pickerRows(entries, "pad", none, false));
        require(pads.contains("Warm Pad") && pads.contains("Halo Pad") && pads.contains("Dream Pad")
                    && pads.contains("Pad swell.flac"),
                "pad search misses patches, instruments, or samples");
        // A family heading alone is enough to find its members.
        require(namesOf(pickerRows(entries, "woodwind", none, false)).size() == 11, "family search wrong");
        require(namesOf(pickerRows(entries, "my samples", none, false)).size() == 3, "library not searchable by group");
    }
    require(pickerRows(entries, "zzzzqq", none, false).empty(), "nonsense query returned rows");
    {
        // Results come back in a "Results" group, never duplicated.
        const auto rows = pickerRows(entries, "sax", none, false);
        require(rows.front().header && rows.front().title == "Results", "results heading missing");
    }

    // Favorites: a section on top, no duplicates in results, favorites-only filter.
    Favorites stars;
    require(stars.toggle(instrumentKey(1)) && stars.toggle(patchKey("Super Saw")) && stars.size() == 2,
            "favorite toggle failed");
    require(stars.has(instrumentKey(1)) && !stars.has(instrumentKey(2)), "favorite lookup wrong");
    {
        const auto rows = pickerRows(entries, "", stars, false);
        require(rows.front().header && rows.front().title == "Favorites", "favorites section missing");
        require(namesOf({ rows.begin() + 1, rows.begin() + 3 }).size() == 2, "favorites rows wrong");
        const auto only = pickerRows(entries, "", stars, true);
        require(only.size() == 3, "favorites-only showed more than the favorites");
        const auto filtered = namesOf(pickerRows(entries, "saw", stars, true));
        require(filtered.size() == 1 && filtered[0] == "Super Saw", "favorites-only ignored the query");
        // Searching: the favorite is listed once, in the Favorites group.
        const auto searched = namesOf(pickerRows(entries, "saw", stars, false));
        int count = 0;
        for (const auto& name : searched)
            count += name == "Super Saw" ? 1 : 0;
        require(count == 1, "favorite duplicated in search results");
        require(pickerRows(entries, "", none, true).empty(), "favorites-only with none returned rows");
    }
    require(!stars.toggle(instrumentKey(1)) && !stars.has(instrumentKey(1)), "unstar failed");
    {
        Favorites capped;
        for (int i = 0; i < Favorites::maxEntries + 20; ++i)
            capped.toggle("sample:" + juce::String(i));
        require(capped.size() == Favorites::maxEntries, "favorites not capped");
        require(!capped.toggle(juce::String::repeatedString("x", Favorites::maxKeyLength + 1)), "overlong key accepted");
        require(!capped.toggle({}), "empty key accepted");
    }

    // Persistence: round-trip, missing file, damaged file, hostile entries.
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getNonexistentChildFile("sonora-favorites-test", "");
        require(dir.createDirectory().wasOk(), "favorites dir failed");
        const auto file = dir.getChildFile("config/favorites.json");
        Favorites saved;
        saved.toggle(instrumentKey(3));
        saved.toggle(sampleKey("Drums/Kick 01.wav"));
        require(saved.save(file).wasOk() && file.existsAsFile(), "favorites save failed");
        Favorites loaded;
        require(loaded.load(file).wasOk() && loaded == saved, "favorites did not round-trip");
        Favorites missing;
        missing.toggle("keep");
        require(missing.load(dir.getChildFile("nope.json")).wasOk() && missing.has("keep"),
                "missing favorites file disturbed state");
        file.replaceWithText("{ this is not json");
        Favorites damaged;
        damaged.toggle("keep");
        require(damaged.load(file).failed() && damaged.has("keep") && damaged.size() == 1,
                "damaged file half-applied");
        file.replaceWithText(R"({"version":1,"favorites":["ok", 5, null, {"a":1}, ""]})");
        Favorites mixed;
        require(mixed.load(file).wasOk() && mixed.size() == 1 && mixed.has("ok"), "non-string entries not ignored");
        file.replaceWithText(R"({"version":1})");
        require(Favorites().load(file).failed(), "file without a list accepted");
        require(dir.deleteRecursively(), "favorites cleanup failed");
    }

    // Display names come from string work (a relative path must never become a juce::File).
    require(sampleDisplayName("Drums/Kick 01.wav") == "Kick 01" && sampleDisplayName("a.b.flac") == "a.b"
                && sampleDisplayName("noext") == "noext" && sampleDisplayName("x/y/z") == "z"
                && sampleDisplayName("kick.WAV") == "kick" && sampleDisplayName("").isEmpty(),
            "sample display name wrong");

    // The button and the list agree on what a track is playing.
    {
        auto track = std::make_unique<Track>();
        track->kind = TrackKind::Synth;
        require(currentPickerKey(*track) == patchKey("Sine Keys") && describeTrackInstrument(*track) == "Synth: Sine Keys",
                "default synth not recognised as its patch");
        track->synth.cutoff = 900.0f;
        require(currentPickerKey(*track) == instrumentKey(0) && describeTrackInstrument(*track) == "Sonora Synth (custom)",
                "edited synth not reported as custom");
        track->synth = synthPatches()[10].params;
        require(currentPickerKey(*track) == patchKey(synthPatches()[10].name), "factory patch not recognised");
        track->instrumentPreset = 1;
        require(currentPickerKey(*track) == instrumentKey(1) && describeTrackInstrument(*track) == "Grand Piano",
                "bank instrument name wrong");
        track->instrumentPreset = samplerInstrument;
        require(describeTrackInstrument(*track) == "Sampler (no sound yet)"
                    && currentPickerKey(*track) == instrumentKey(samplerInstrument),
                "empty sampler described wrong");
        track->setSamplerFileName("Drums/Kick 01.wav");
        require(describeTrackInstrument(*track) == "Sampler: Kick 01"
                    && currentPickerKey(*track) == sampleKey("Drums/Kick 01.wav"),
                "loaded sampler described wrong");
    }
}

// ---- Sampler -------------------------------------------------------------
// ProjectState is ~300 KB: every state in these tests lives on the heap.
void writeToneWav(const juce::File& file, double hz, double seconds, double rate, int channels,
                  double decayPerSecond = 0.0, bool noise = false)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
        new juce::FileOutputStream(file), rate, static_cast<unsigned int>(channels), 24, {}, 0));
    require(writer != nullptr, "tone fixture writer failed");
    const int frames = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> buffer(channels, frames);
    std::uint32_t state = 12345u;
    for (int i = 0; i < frames; ++i)
    {
        const double time = i / rate;
        float value = 0.0f;
        if (noise)
        {
            state = state * 1664525u + 1013904223u;
            value = static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
        }
        else
            value = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * hz * time));
        value *= static_cast<float>(std::exp(-decayPerSecond * time));
        for (int c = 0; c < channels; ++c)
            buffer.setSample(c, i, c == 0 ? value : -value);
    }
    require(writer->writeFromAudioSampleBuffer(buffer, 0, frames), "tone fixture samples failed");
}

double crossingRate(const std::vector<float>& audio, int from, int to, double rate)
{
    int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if ((audio[static_cast<std::size_t>(i - 1)] < 0.0f) != (audio[static_cast<std::size_t>(i)] < 0.0f))
            ++crossings;
    return crossings / ((to - from) / rate) / 2.0; // Hz
}

double windowEnergy(const std::vector<float>& audio, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += static_cast<double>(audio[static_cast<std::size_t>(i)]) * audio[static_cast<std::size_t>(i)];
    return sum / std::max(1, to - from);
}

void testSampler()
{
    using sonora::ProjectState;
    using sonora::SamplerParams;

    // ---- params and names ----
    require(SamplerParams {}.valid(), "default sampler params rejected");
    {
        SamplerParams bad;
        bad.rootNote = 128;
        require(!bad.valid(), "root 128 validated");
        bad = {};
        bad.end = 0.0f;
        require(!bad.valid(), "empty region validated");
        bad = {};
        bad.loopEnd = bad.loopStart;
        require(!bad.valid(), "empty loop validated");
        bad = {};
        bad.tune = 250.0f;
        require(!bad.valid(), "wild tune validated");
        bad = {};
        bad.gain = std::nanf("");
        require(!bad.valid(), "NaN gain validated");
    }
    for (const char* good : { "kick.wav", "Drums/Kick 01.wav", "a..b.wav", "x/y/z.flac" })
        require(sonora::isSafeSampleName(good), "safe sample name rejected");
    for (const char* bad : { "", "/etc/passwd", "../x.wav", "a/../b.wav", "a/..", "..", "C:\\x.wav", "a\\b.wav" })
        require(!sonora::isSafeSampleName(bad), ("unsafe sample name accepted: " + juce::String(bad)).toRawUTF8());

    // ---- fixtures ----
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("sonora-sampler-test", "");
    require(dir.createDirectory().wasOk(), "sampler test dir failed");
    writeToneWav(dir.getChildFile("a440.wav"), 440.0, 1.0, 44100.0, 1);
    writeToneWav(dir.getChildFile("c4.wav"), 261.6256, 1.0, 48000.0, 1);
    writeToneWav(dir.getChildFile("short.wav"), 440.0, 0.2, 48000.0, 1);
    writeToneWav(dir.getChildFile("decay.wav"), 440.0, 1.0, 48000.0, 1, 6.0);
    writeToneWav(dir.getChildFile("stereo.wav"), 440.0, 0.5, 48000.0, 2);
    writeToneWav(dir.getChildFile("noise.wav"), 0.0, 1.0, 48000.0, 1, 0.0, true);

    // ---- loader ----
    const auto tone = sonora::loadSampleData(dir.getChildFile("a440.wav"));
    require(tone != nullptr && tone->valid() && tone->channels == 1 && tone->frames == 44100
                && std::abs(tone->rate - 44100.0) < 0.5,
            "tone did not load at its own rate");
    float peak = 0.0f;
    for (const auto v : tone->channel[0])
        peak = std::max(peak, std::abs(v));
    require(std::abs(peak - 0.9f) < 0.01f, "sample not normalized to 0.9");
    const auto stereo = sonora::loadSampleData(dir.getChildFile("stereo.wav"));
    require(stereo != nullptr && stereo->channels == 2 && !stereo->channel[1].empty()
                && stereo->channel[1][100] == -stereo->channel[0][100],
            "stereo sample lost its channels");
    require(sonora::loadSampleData(dir.getChildFile("missing.wav")) == nullptr, "missing file loaded");
    {
        std::atomic<bool> cancel { true };
        require(sonora::loadSampleData(dir.getChildFile("a440.wav"), &cancel) == nullptr,
                "cancelled load returned data");
        dir.getChildFile("junk.wav").replaceWithText("not audio");
        require(sonora::loadSampleData(dir.getChildFile("junk.wav")) == nullptr, "junk file loaded");
    }
    require(sonora::detectRootNote(*tone) == 69, "A440 root not detected as A4");
    require(sonora::detectRootNote(*sonora::loadSampleData(dir.getChildFile("c4.wav"))) == 60,
            "C4 root not detected");
    require(sonora::detectRootNote(*sonora::loadSampleData(dir.getChildFile("noise.wav"))) == -1,
            "noise given a root note");
    const auto peaks = sonora::samplePeaks(*tone, 64);
    require(peaks.size() == 64 && peaks[10] > 0.5f && peaks[10] <= 1.0f, "sample overview wrong");

    // ---- engine ----
    auto makeProject = [](int pitch, int ticks) {
        auto state = std::make_unique<ProjectState>(fixture());
        auto& track = state->tracks[0];
        track.instrumentPreset = sonora::samplerInstrument;
        track.setSamplerFileName("a440.wav");
        track.sampler.rootNote = 69;
        track.melodies[0] = {};
        track.melodies[0].count = 1;
        track.melodies[0].notes[0] = { 1, 0, ticks, pitch, 110 };
        state->tracks[1].drumPatterns[0] = {};
        state->songMode = false;
        return state;
    };
    auto render = [](const ProjectState& state, const sonora::SampleData* data, int blocks) {
        auto engine = std::make_unique<sonora::AudioEngine>();
        engine->prepare(48000.0);
        engine->retireSampleData(0, data);
        require(engine->submit(state), "sampler project rejected");
        engine->setPlaying(true);
        std::vector<float> out;
        juce::AudioBuffer<float> buffer(2, 512);
        for (int i = 0; i < blocks; ++i)
        {
            engine->process({ &buffer, 0, 512 });
            for (int s = 0; s < 512; ++s)
            {
                require(std::isfinite(buffer.getSample(0, s)), "sampler produced non-finite audio");
                out.push_back(buffer.getSample(0, s));
            }
        }
        engine->retireSampleData(0, nullptr);
        return out;
    };
    constexpr double rate = 48000.0;
    const int steady0 = 6000, steady1 = 24000; // inside the note, past the attack

    // No sample loaded: silent, no crash.
    {
        const auto silent = render(*makeProject(69, 3840), nullptr, 40);
        float level = 0.0f;
        for (const auto v : silent)
            level = std::max(level, std::abs(v));
        require(level < 1.0e-6f, "sampler without a file made sound");
    }
    // Root key plays at the file's own pitch even though 44.1k != 48k.
    const auto root = render(*makeProject(69, 3840), tone.get(), 80);
    require(windowEnergy(root, steady0, steady1) > 0.005, "sampler too quiet");
    require(std::abs(crossingRate(root, steady0, steady1, rate) - 440.0) < 12.0,
            "root key not at the file's pitch (rate conversion wrong)");
    // An octave up doubles the pitch; a fifth down is 2/3.
    const auto up = render(*makeProject(81, 3840), tone.get(), 80);
    require(std::abs(crossingRate(up, steady0, steady1, rate) - 880.0) < 25.0, "octave up not 880 Hz");
    const auto down = render(*makeProject(62, 3840), tone.get(), 80);
    require(std::abs(crossingRate(down, steady0, steady1, rate) - 440.0 * std::pow(2.0, -7.0 / 12.0)) < 12.0,
            "fifth down wrong");
    // Tune is in cents (+1200 would be an octave; +100 is one semitone).
    {
        auto tuned = makeProject(69, 3840);
        tuned->tracks[0].sampler.tune = 100.0f;
        const auto audio = render(*tuned, tone.get(), 80);
        require(std::abs(crossingRate(audio, steady0, steady1, rate) - 440.0 * std::pow(2.0, 1.0 / 12.0)) < 12.0,
                "tune cents wrong");
    }
    // Key tracking off: every key plays the file at its own pitch.
    {
        auto fixedPitch = makeProject(81, 3840);
        fixedPitch->tracks[0].sampler.keyTrack = false;
        const auto audio = render(*fixedPitch, tone.get(), 80);
        require(std::abs(crossingRate(audio, steady0, steady1, rate) - 440.0) < 12.0,
                "key tracking off still followed the key");
    }
    // A non-looping sample ends; looping sustains it for the held note.
    {
        const auto shortTone = sonora::loadSampleData(dir.getChildFile("short.wav"));
        auto plain = makeProject(69, 3840);
        plain->tracks[0].setSamplerFileName("short.wav");
        const auto ended = render(*plain, shortTone.get(), 60);
        require(windowEnergy(ended, 16000, 22000) < 1.0e-6, "finished sample kept sounding");
        auto looped = makeProject(69, 3840);
        looped->tracks[0].setSamplerFileName("short.wav");
        looped->tracks[0].sampler.loop = true;
        const auto held = render(*looped, shortTone.get(), 60);
        require(windowEnergy(held, 16000, 22000) > 0.005, "loop did not sustain the held note");
        require(std::abs(crossingRate(held, 16000, 22000, rate) - 440.0) < 15.0, "loop changed the pitch");
    }
    // Key release: a short note lets go, a one-shot plays through. 480 ticks is
    // ~0.24 s at the fixture's 123 BPM, plus the 0.12 s release, so look past
    // 0.5 s (the sample itself lasts a full second).
    {
        const auto releasing = render(*makeProject(69, 480), tone.get(), 60);
        require(windowEnergy(releasing, 3000, 9000) > 0.005, "short note silent while held");
        require(windowEnergy(releasing, 24000, 30000) < 1.0e-6, "released note kept sounding");
        auto oneShot = makeProject(69, 480);
        oneShot->tracks[0].sampler.oneShot = true;
        const auto played = render(*oneShot, tone.get(), 60);
        require(windowEnergy(played, 24000, 30000) > 0.005, "one-shot stopped on key release");
    }
    // Reverse and trim change what is heard: the decaying fixture is loud at
    // the start going forward and loud at the end in reverse.
    {
        const auto decay = sonora::loadSampleData(dir.getChildFile("decay.wav"));
        auto forward = makeProject(69, 3840);
        forward->tracks[0].setSamplerFileName("decay.wav");
        const auto fwd = render(*forward, decay.get(), 60);
        require(windowEnergy(fwd, 2000, 6000) > windowEnergy(fwd, 24000, 28000) * 4.0,
                "decaying sample not louder at the start");
        auto backwards = std::make_unique<ProjectState>(*forward);
        backwards->tracks[0].sampler.reverse = true;
        const auto rev = render(*backwards, decay.get(), 60);
        require(windowEnergy(rev, 2000, 6000) < windowEnergy(rev, 24000, 28000) * 0.5,
                "reverse did not play from the end");
        auto trimmed = std::make_unique<ProjectState>(*forward);
        trimmed->tracks[0].sampler.start = 0.5f;
        const auto cut = render(*trimmed, decay.get(), 60);
        require(windowEnergy(cut, 2000, 6000) < windowEnergy(fwd, 2000, 6000) * 0.2,
                "trim start did not skip the loud part");
        auto earlyEnd = std::make_unique<ProjectState>(*forward);
        earlyEnd->tracks[0].sampler.end = 0.1f;
        const auto clipped = render(*earlyEnd, decay.get(), 60);
        require(windowEnergy(clipped, 12000, 18000) < 1.0e-6, "trim end did not stop playback");
    }
    // Voice stealing: more notes than voices stays bounded and finite.
    {
        auto instrument = std::make_unique<sonora::SamplerInstrument>();
        instrument->prepare(rate);
        instrument->requestSample(tone.get());
        juce::MidiBuffer events;
        for (int note = 30; note < 80; ++note)
            events.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
        juce::AudioBuffer<float> block(2, 512);
        block.clear();
        instrument->render(block, 0, 512, events);
        require(instrument->activeVoices() == sonora::SamplerInstrument::numVoices, "voice cap wrong");
        for (int i = 0; i < 512; ++i)
            require(std::isfinite(block.getSample(0, i)), "voice stealing produced non-finite audio");
        instrument->stop();
        require(instrument->activeVoices() == 0, "stop left voices running");
        instrument->requestSample(nullptr);
    }
    // Stereo samples keep their image.
    {
        auto st = makeProject(69, 3840);
        st->tracks[0].setSamplerFileName("stereo.wav");
        auto engine = std::make_unique<sonora::AudioEngine>();
        engine->prepare(rate);
        engine->retireSampleData(0, stereo.get());
        require(engine->submit(*st), "stereo project rejected");
        engine->setPlaying(true);
        juce::AudioBuffer<float> buffer(2, 512);
        double dot = 0.0;
        for (int i = 0; i < 30; ++i)
        {
            engine->process({ &buffer, 0, 512 });
            if (i >= 12)
                for (int s = 0; s < 512; ++s)
                    dot += static_cast<double>(buffer.getSample(0, s)) * buffer.getSample(1, s);
        }
        engine->retireSampleData(0, nullptr);
        require(dot < 0.0, "stereo sample collapsed to mono");
    }

    // ---- library ----
    {
        const auto library = dir.getChildFile("library");
        const auto first = sonora::importSampleToLibrary(dir.getChildFile("a440.wav"), library);
        const auto second = sonora::importSampleToLibrary(dir.getChildFile("a440.wav"), library);
        require(first == "a440.wav" && second != first && second.isNotEmpty(), "library import naming wrong");
        require(sonora::importSampleToLibrary(dir.getChildFile("junk.txt"), library).isEmpty(),
                "non-audio imported");
        dir.getChildFile("notes.txt").replaceWithText("hello");
        require(sonora::importSampleToLibrary(dir.getChildFile("notes.txt"), library).isEmpty(),
                "text file imported");
        require(sonora::importSampleToLibrary(library.getChildFile(first), library) == first,
                "library file re-imported as a copy");
        library.getChildFile("Drums").createDirectory();
        dir.getChildFile("c4.wav").copyFileTo(library.getChildFile("Drums/Kick 01.wav"));
        const auto names = sonora::listSampleLibrary(library);
        require(names.size() == 3 && std::find(names.begin(), names.end(), juce::String("Drums/Kick 01.wav"))
                                         != names.end(),
                "library listing wrong");
        require(sonora::resolveSampleFile("Drums/Kick 01.wav", { library }).existsAsFile(), "library file not resolved");
        require(sonora::resolveSampleFile("a440.wav", { dir.getChildFile("nowhere"), library }).existsAsFile(),
                "later search dir skipped");
        require(sonora::resolveSampleFile("../a440.wav", { library }) == juce::File(), "parent hop resolved");
        require(sonora::resolveSampleFile(dir.getChildFile("a440.wav").getFullPathName(), { library }) == juce::File(),
                "absolute path resolved");
        require(sonora::resolveSampleFile("", { library }) == juce::File(), "empty name resolved");
    }

    // ---- persistence (v22) ----
    {
        auto saved = makeProject(60, 960);
        saved->tracks[0].setSamplerFileName("Drums/Kick 01.wav");
        auto& s = saved->tracks[0].sampler;
        s.rootNote = 48; s.tune = -12.0f; s.start = 0.1f; s.end = 0.9f; s.loop = true;
        s.loopStart = 0.2f; s.loopEnd = 0.8f; s.reverse = false; s.oneShot = false; s.keyTrack = true;
        s.attack = 0.05f; s.decay = 0.4f; s.sustain = 0.6f; s.release = 0.5f; s.gain = 1.25f;
        require(saved->valid(), "sampler fixture rejected");
        const auto json = sonora::ProjectIO::encode(*saved);
        auto loaded = std::make_unique<ProjectState>();
        require(sonora::ProjectIO::decode(json, *loaded).wasOk() && *loaded == *saved
                    && loaded->tracks[0].samplerFileName() == "Drums/Kick 01.wav"
                    && loaded->tracks[0].sampler.rootNote == 48 && loaded->tracks[0].sampler.loop,
                "sampler round-trip failed");
        auto legacy = juce::JSON::parse(json);
        legacy.getDynamicObject()->setProperty("version", 21);
        for (auto& track : *legacy.getDynamicObject()->getProperty("tracks").getArray())
            track.getDynamicObject()->removeProperty("sampler");
        require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), *loaded).wasOk()
                    && loaded->tracks[0].sampler == SamplerParams {} && loaded->tracks[0].samplerFile[0] == '\0',
                "v21 did not open with a default sampler");
        auto rejects = [&](const char* what, const std::function<void(juce::DynamicObject&)>& tamper) {
            auto document = juce::JSON::parse(json);
            auto* sampler = document.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
                                .getDynamicObject()->getProperty("sampler").getDynamicObject();
            tamper(*sampler);
            *loaded = *saved;
            require(sonora::ProjectIO::decode(juce::JSON::toString(document), *loaded).failed(), what);
            require(*loaded == *saved, "bad sampler destroyed current state");
        };
        rejects("parent-hop sample name accepted", [](juce::DynamicObject& o) { o.setProperty("file", "../../etc/passwd"); });
        rejects("absolute sample name accepted", [](juce::DynamicObject& o) { o.setProperty("file", "/etc/passwd"); });
        rejects("root 200 accepted", [](juce::DynamicObject& o) { o.setProperty("root", 200); });
        rejects("inverted trim accepted", [](juce::DynamicObject& o) { o.setProperty("end", 0.05); });
        rejects("wild gain accepted", [](juce::DynamicObject& o) { o.setProperty("gain", 9.0); });
        rejects("string loop flag accepted", [](juce::DynamicObject& o) { o.setProperty("loop", "yes"); });
        rejects("missing release accepted", [](juce::DynamicObject& o) { o.removeProperty("release"); });
        auto missing = juce::JSON::parse(json);
        missing.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
            .getDynamicObject()->removeProperty("sampler");
        *loaded = *saved;
        require(sonora::ProjectIO::decode(juce::JSON::toString(missing), *loaded).failed(),
                "v22 without a sampler section accepted");
        auto unsafe = std::make_unique<ProjectState>(*saved);
        unsafe->tracks[0].setSamplerFileName("../x.wav");
        require(!unsafe->valid(), "unsafe sampler name validated");
    }

    // ---- export ----
    {
        auto job = std::make_unique<sonora::ExportJob>();
        job->project = *makeProject(69, 3840);
        job->project.tracks[0].melodies[0].notes[0].pitch = 69;
        job->sampleDirs = { dir };
        job->songRange = false;
        const auto withSample = sonora::OfflineExport::render(*job);
        require(withSample.ok() && withSample.peak > 0.1f, "export dropped the sampler audio");
        job->sampleDirs = {};
        const auto without = sonora::OfflineExport::render(*job);
        require(without.ok() && without.peak < 1.0e-4f, "export invented sampler audio");
        // Drum-kit voicings and custom kits now survive export too.
        auto drums = std::make_unique<sonora::ExportJob>();
        drums->project = fixture();
        drums->project.tracks[0].melodies[0] = {};
        drums->project.songMode = false;
        const auto starter = sonora::OfflineExport::render(*drums);
        drums->project.tracks[1].kitVariant = 4; // Boom
        const auto boom = sonora::OfflineExport::render(*drums);
        require(starter.ok() && boom.ok(), "drum export failed");
        double difference = 0.0;
        for (int i = 0; i < starter.audio.getNumSamples(); ++i)
            difference += std::abs(starter.audio.getSample(0, i) - boom.audio.getSample(0, i));
        require(difference > 1.0, "export ignored the drum kit voicing");
    }
    require(dir.deleteRecursively(), "sampler test cleanup failed");
}

// Pulse and Noise oscillators. Kept out of testSynthEngine on purpose: a
// ProjectState is ~300 KB, and that function already holds a dozen of them on
// the stack, so every state here lives on the heap.
void testSynthOscillators()
{
    using sonora::ProjectState;
    auto make = [](int wave, int wave2 = sonora::WaveSine) {
        auto state = std::make_unique<ProjectState>(fixture());
        state->tracks[0].melodies[0] = {};
        state->tracks[0].melodies[0].count = 1;
        state->tracks[0].melodies[0].notes[0] = { 1, 0, 3840, 57, 110 };
        state->tracks[1].drumPatterns[0] = {};
        state->tracks[0].synth.wave = wave;
        state->tracks[0].synth.wave2 = wave2;
        return state;
    };
    auto render = [](const ProjectState& state, int blocks) {
        auto engine = std::make_unique<sonora::AudioEngine>();
        engine->prepare(48000.0);
        require(engine->submit(state), "oscillator project rejected");
        engine->setPlaying(true);
        std::vector<float> out;
        juce::AudioBuffer<float> buffer(2, 512);
        for (int i = 0; i < blocks; ++i)
        {
            engine->process({ &buffer, 0, 512 });
            for (int s = 0; s < 512; ++s)
            {
                require(std::isfinite(buffer.getSample(0, s)), "oscillator produced non-finite audio");
                out.push_back(buffer.getSample(0, s));
            }
        }
        return out;
    };
    auto peak = [](const std::vector<float>& a) {
        float value = 0.0f;
        for (const auto x : a)
            value = std::max(value, std::abs(x));
        return value;
    };
    auto brightness = [](const std::vector<float>& a) {
        double diff = 0.0, energy = 1.0e-12;
        for (std::size_t i = 1; i < a.size(); ++i)
        {
            diff += (a[i] - a[i - 1]) * (a[i] - a[i - 1]);
            energy += a[i] * a[i];
        }
        return diff / energy;
    };

    const auto sine = render(*make(sonora::WaveSine), 30);
    const auto pulse = render(*make(sonora::WavePulse), 30);
    require(peak(pulse) > 0.005f && peak(pulse) < 1.0f, "pulse level wrong");
    require(pulse != sine, "pulse rendered as sine");
    require(pulse != render(*make(sonora::WaveSquare), 30), "pulse rendered as square");
    require(brightness(pulse) > brightness(sine) * 1.5, "pulse not brighter than sine");

    const auto noise = render(*make(sonora::WaveNoise), 30);
    require(peak(noise) > 0.005f && peak(noise) < 1.0f, "noise level wrong");
    require(brightness(noise) > brightness(sine) * 5.0, "noise not broadband");
    require(noise == render(*make(sonora::WaveNoise), 30), "noise not reproducible");
    // Filtered noise darkens like any other source.
    auto soft = make(sonora::WaveNoise);
    soft->tracks[0].synth.cutoff = 300.0f;
    require(brightness(render(*soft, 30)) < brightness(noise) * 0.5, "filter did not shape noise");

    // Generated track names must be unambiguous: patch names are unique and
    // never collide with a sampled instrument name (they drive rename-following).
    // The editable synth (instrument 0) deliberately shares its saved name,
    // "Sine Keys", with patch 0 so existing tracks keep following renames.
    const auto& patches = sonora::synthPatches();
    require(juce::String(sonora::instruments[0].name) == patches[0].name, "synth/patch 0 names diverged");
    for (std::size_t i = 0; i < patches.size(); ++i)
    {
        for (std::size_t j = 0; j < i; ++j)
            require(juce::String(patches[i].name) != patches[j].name, "duplicate synth patch name");
        for (std::size_t k = 1; k < sonora::instruments.size(); ++k)
            require(juce::String(patches[i].name) != sonora::instruments[k].name,
                    "patch name collides with a sampled instrument");
        require(patches[i].params.valid(), "factory patch invalid");
    }

    // The new waveforms persist; an id past the table is rejected.
    auto persisted = make(sonora::WaveNoise, sonora::WavePulse);
    auto reloaded = std::make_unique<ProjectState>();
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(*persisted), *reloaded).wasOk()
                && reloaded->tracks[0].synth.wave == sonora::WaveNoise
                && reloaded->tracks[0].synth.wave2 == sonora::WavePulse,
            "new waveforms did not round-trip");
    persisted->tracks[0].synth.wave = sonora::numSynthWaves;
    require(!persisted->valid(), "unknown waveform validated");
    require(juce::String(sonora::synthWaveName(sonora::WaveNoise)) == "Noise"
                && juce::String(sonora::synthWaveName(sonora::WavePulse)) == "Pulse",
            "waveform names wrong");
}

void testSynthEngine()
{
    for (const auto& patch : sonora::synthPatches())
        require(patch.params.valid(), "factory synth patch out of range");
    require(sonora::synthPatches()[0].params == sonora::SynthParams {}, "patch 0 must be the default sine");

    auto project = fixture();
    project.tracks[0].melodies[0] = {};
    project.tracks[0].melodies[0].count = 1;
    project.tracks[0].melodies[0].notes[0] = { 1, 0, 3840, 57, 110 };
    project.tracks[1].drumPatterns[0] = {};
    struct Stereo { std::vector<float> left, right; };
    auto render = [](const sonora::ProjectState& state, int blocks) {
        auto engineStorage = std::make_unique<sonora::AudioEngine>();
        auto& engine = *engineStorage;
        engine.prepare(48000.0);
        require(engine.submit(state), "synth project rejected");
        engine.setPlaying(true);
        Stereo out;
        juce::AudioBuffer<float> buffer(2, 512);
        for (int i = 0; i < blocks; ++i)
        {
            engine.process({ &buffer, 0, 512 });
            for (int s = 0; s < 512; ++s)
            {
                require(std::isfinite(buffer.getSample(0, s)), "synth produced non-finite audio");
                out.left.push_back(buffer.getSample(0, s));
                out.right.push_back(buffer.getSample(1, s));
            }
        }
        return out;
    };
    auto peak = [](const std::vector<float>& a, std::size_t from = 0, std::size_t to = 0) {
        float value = 0.0f;
        for (std::size_t i = from; i < (to == 0 ? a.size() : to); ++i)
            value = std::max(value, std::abs(a[i]));
        return value;
    };
    // Brightness proxy: derivative energy over signal energy. Level-independent
    // and grows with harmonic content (a pure sine scores (2*pi*f/fs)^2).
    auto brightness = [](const std::vector<float>& a) {
        double diff = 0.0, energy = 1.0e-12;
        for (std::size_t i = 1; i < a.size(); ++i)
        {
            diff += (a[i] - a[i - 1]) * (a[i] - a[i - 1]);
            energy += a[i] * a[i];
        }
        return diff / energy;
    };
    const auto sine = render(project, 30);

    // Every factory patch sounds, stays in range, and differs from the sine.
    for (std::size_t i = 1; i < sonora::synthPatches().size(); ++i)
    {
        auto variant = project;
        variant.tracks[0].synth = sonora::synthPatches()[i].params;
        const auto audio = render(variant, 60);
        require(peak(audio.left) > 0.005f, "factory patch silent");
        require(peak(audio.left) < 1.0f, "factory patch clips");
        require(audio.left != sine.left, "factory patch rendered as sine");
    }

    // Saw is brighter than sine; closing the filter darkens it again.
    auto saw = project;
    saw.tracks[0].synth.wave = sonora::WaveSaw;
    const auto sawAudio = render(saw, 30);
    auto dark = saw;
    dark.tracks[0].synth.cutoff = 300.0f;
    const auto darkAudio = render(dark, 30);
    require(brightness(sawAudio.left) > brightness(sine.left) * 1.5, "saw not brighter than sine");
    require(brightness(darkAudio.left) < brightness(sawAudio.left) * 0.5, "low-pass filter did not darken");

    // A slow attack starts quiet; chorus widens the mono voice to stereo.
    auto slow = project;
    slow.tracks[0].synth.attack = 2.0f;
    const auto slowAudio = render(slow, 30);
    require(peak(slowAudio.left, 0, 2400) < peak(sine.left, 0, 2400) * 0.2f, "attack time ignored");
    require(sine.left == sine.right, "default synth should be mono");
    auto wide = project;
    wide.tracks[0].synth.chorus = 1.0f;
    const auto wideAudio = render(wide, 30);
    require(wideAudio.left != wideAudio.right, "chorus did not widen");

    // Sound edits apply live: a held note is neither cut nor re-triggered.
    // Edits that change stored settings but not the audible result must leave
    // the output sample-identical to an untouched engine.
    {
        sonora::AudioEngine reference, edited;
        for (auto* engine : { &reference, &edited })
        {
            engine->prepare(48000.0);
            require(engine->submit(project), "live-edit project rejected");
            engine->setPlaying(true);
        }
        juce::AudioBuffer<float> a(2, 512), b(2, 512);
        for (int i = 0; i < 10; ++i)
        {
            reference.process({ &a, 0, 512 });
            edited.process({ &b, 0, 512 });
        }
        auto tweaked = project;
        tweaked.tracks[0].synth.lfoRate = 9.0f;       // LFO depths are zero
        tweaked.tracks[0].fx.delay.timeMs = 500.0f;   // delay mix is zero
        tweaked.tracks[0].synth.filterDecay = 1.0f;   // filter is open
        require(edited.submit(tweaked), "tweaked project rejected");
        for (int i = 0; i < 3; ++i)
        {
            reference.process({ &a, 0, 512 });
            edited.process({ &b, 0, 512 });
            require(a.getMagnitude(0, 0, 512) > 0.01f, "reference note ended early");
            for (int s = 0; s < 512; ++s)
                require(a.getSample(0, s) == b.getSample(0, s), "sound edit cut or re-triggered the held note");
        }
        // A real timbre edit on the held note is heard immediately.
        auto brighter = tweaked;
        brighter.tracks[0].synth.wave = sonora::WaveSaw;
        require(edited.submit(brighter), "brighter project rejected");
        reference.process({ &a, 0, 512 });
        edited.process({ &b, 0, 512 });
        bool changed = false;
        for (int s = 0; s < 512 && !changed; ++s)
            changed = a.getSample(0, s) != b.getSample(0, s);
        require(changed && b.getMagnitude(0, 0, 512) > 0.01f, "live wave change not heard");
    }

    // v11 round-trip, v10 migration to the default sine, malformed synths.
    auto saved = project;
    saved.tracks[0].synth = sonora::synthPatches()[3].params;
    const auto json = sonora::ProjectIO::encode(saved);
    sonora::ProjectState loaded;
    require(json.contains("\"version\": 22"), "synth projects must save as v22");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == saved, "synth round-trip failed");
    auto legacy = juce::JSON::parse(json);
    legacy.getDynamicObject()->setProperty("version", 10);
    toPreV13Song(legacy);
    for (auto& track : *legacy.getDynamicObject()->getProperty("tracks").getArray())
        track.getDynamicObject()->removeProperty("synth");
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk()
            && loaded.tracks[0].synth == sonora::SynthParams {}, "v10 projects must open with the default sine");
    auto reject = [&](const char* field, juce::var value) {
        auto document = juce::JSON::parse(json);
        auto* synth = document.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
                          .getDynamicObject()->getProperty("synth").getDynamicObject();
        synth->setProperty(field, value);
        loaded = saved;
        require(sonora::ProjectIO::decode(juce::JSON::toString(document), loaded).failed(),
                ("bad synth field accepted: " + juce::String(field)).toRawUTF8());
        require(loaded == saved, "bad synth destroyed current state");
    };
    reject("wave", sonora::numSynthWaves); // one past the last waveform
    reject("wave2", -1);
    reject("cutoff", 10.0);
    reject("resonance", 1.5);
    reject("attack", 0.0);
    reject("level", "loud");
    auto missing = juce::JSON::parse(json);
    missing.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("synth");
    require(sonora::ProjectIO::decode(juce::JSON::toString(missing), loaded).failed(), "v11 without synth accepted");
}

void testKnobs()
{
    // Both MiniLab CC sets resolve to knobs 1-8; everything else is ignored.
    for (int knob = 0; knob < 8; ++knob)
    {
        require(sonora::knobIndexForController(sonora::miniLabKnobCcs[knob]) == knob, "user-program knob CC unmapped");
        require(sonora::knobIndexForController(sonora::miniLabDawKnobCcs[knob]) == knob, "DAW-mode knob CC unmapped");
    }
    for (int cc : { 0, 1, 7, 64, 102, 114, 127 })
        require(sonora::knobIndexForController(cc) < 0, "non-knob CC captured");

    // Every instrument (and drums) gets 8 distinct, in-range controls, and
    // extreme knob positions always leave a valid project.
    auto project = fixture();
    std::vector<sonora::Track> tracks;
    for (int preset = 0; preset < static_cast<int>(sonora::instruments.size()); ++preset)
    {
        auto track = project.tracks[0];
        track.instrumentPreset = preset;
        tracks.push_back(track);
    }
    tracks.push_back(project.tracks[1]);
    for (auto track : tracks)
    {
        const auto map = sonora::knobMapFor(track);
        for (int a = 0; a < 8; ++a)
            for (int b = 0; b < a; ++b)
                require(map[static_cast<std::size_t>(a)].target != map[static_cast<std::size_t>(b)].target,
                        "duplicate knob target");
        for (float v : { 0.0f, 0.37f, 1.0f })
            for (const auto& slot : map)
            {
                auto edited = project;
                edited.tracks[0] = track;
                sonora::applyKnob(edited.tracks[0], slot.target, v);
                require(edited.valid(), ("knob made project invalid: " + juce::String(slot.label)).toRawUTF8());
                require(sonora::knobValueText(edited.tracks[0], slot.target).isNotEmpty(), "empty knob readout");
            }
    }

    // Guitars put distortion on knob 1 and reverb on knob 8.
    auto guitar = project.tracks[0];
    guitar.instrumentPreset = 8; // Overdriven Guitar
    const auto guitarMap = sonora::knobMapFor(guitar);
    require(juce::String(guitarMap[0].label) == "Distortion" && guitarMap[0].target == sonora::KnobTarget::Drive,
            "guitar knob 1 should be distortion");
    require(guitarMap[7].target == sonora::KnobTarget::ReverbMix, "guitar knob 8 should be reverb");
    require(sonora::knobMapFor(project.tracks[0])[0].target == sonora::KnobTarget::SynthCutoff,
            "editable synth knob 1 should be cutoff");

    // Knob positions read back where they were set (log and linear curves).
    for (auto target : { sonora::KnobTarget::SynthCutoff, sonora::KnobTarget::SynthAttack, sonora::KnobTarget::Drive,
                         sonora::KnobTarget::DelayTime, sonora::KnobTarget::ReverbMix, sonora::KnobTarget::Volume })
    {
        auto track = project.tracks[0];
        sonora::applyKnob(track, target, 0.73f);
        require(std::abs(sonora::knobPosition(track, target) - 0.73f) < 0.01f, "knob position round-trip drifted");
    }
    // A knob wakes its effect from bypass; the EQ centre detent is exactly flat.
    auto bypassed = project.tracks[0];
    bypassed.fx.reverb.enabled = false;
    sonora::applyKnob(bypassed, sonora::KnobTarget::ReverbMix, 0.5f);
    require(bypassed.fx.reverb.enabled && bypassed.fx.reverb.mix > 0.49f, "knob did not enable bypassed reverb");
    sonora::applyKnob(bypassed, sonora::KnobTarget::EqHigh, 64.0f / 127.0f);
    require(bypassed.fx.eq.high == 0.0f, "EQ knob centre is not flat");

    // Drive adds harmonics to a sampled guitar; the tone control tames them;
    // track chorus makes a mono source stereo. Measured through the engine.
    auto melody = project;
    melody.tracks[0].melodies[0] = {};
    melody.tracks[0].melodies[0].count = 1;
    melody.tracks[0].melodies[0].notes[0] = { 1, 0, 3840, 52, 110 };
    melody.tracks[1].drumPatterns[0] = {};
    auto render = [](const sonora::ProjectState& state) {
        auto engineStorage = std::make_unique<sonora::AudioEngine>();
        auto& engine = *engineStorage;
        engine.prepare(48000.0);
        require(engine.submit(state), "knob project rejected");
        engine.setPlaying(true);
        std::vector<float> left, right;
        juce::AudioBuffer<float> buffer(2, 512);
        for (int i = 0; i < 40; ++i)
        {
            engine.process({ &buffer, 0, 512 });
            for (int s = 0; s < 512; ++s)
            {
                require(std::isfinite(buffer.getSample(0, s)), "drive/chorus produced non-finite audio");
                left.push_back(buffer.getSample(0, s));
                right.push_back(buffer.getSample(1, s));
            }
        }
        return std::pair { left, right };
    };
    auto brightness = [](const std::vector<float>& a) {
        double diff = 0.0, energy = 1.0e-12;
        for (std::size_t i = 1; i < a.size(); ++i)
        {
            diff += (a[i] - a[i - 1]) * (a[i] - a[i - 1]);
            energy += a[i] * a[i];
        }
        return diff / energy;
    };
    const auto clean = render(melody);
    auto driven = melody;
    sonora::applyKnob(driven.tracks[0], sonora::KnobTarget::Drive, 1.0f);
    const auto drivenAudio = render(driven);
    require(brightness(drivenAudio.first) > brightness(clean.first) * 1.5, "drive did not add harmonics");
    float drivenPeak = 0.0f;
    for (auto x : drivenAudio.first)
        drivenPeak = std::max(drivenPeak, std::abs(x));
    require(drivenPeak < 1.0f, "full drive clips the track");
    auto dark = driven;
    dark.tracks[0].fx.drive.tone = 800.0f;
    require(brightness(render(dark).first) < brightness(drivenAudio.first) * 0.5, "drive tone did not darken");
    require(clean.first == clean.second, "sine source should be mono");
    auto chorused = melody;
    sonora::applyKnob(chorused.tracks[0], sonora::KnobTarget::Chorus, 1.0f);
    const auto wide = render(chorused);
    require(wide.first != wide.second, "track chorus did not widen");

    // v12 persists drive and chorus; v11 files open with both bypassed.
    auto saved = project;
    saved.tracks[0].fx.drive = { 0.6f, 5000.0f, true };
    saved.tracks[0].fx.chorus = { 0.4f, 1.2f, 0.8f, false };
    const auto json = sonora::ProjectIO::encode(saved);
    sonora::ProjectState loaded;
    require(json.contains("\"version\": 22"), "fx projects must save as v22");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == saved, "drive/chorus round-trip failed");
    auto legacy = juce::JSON::parse(json);
    legacy.getDynamicObject()->setProperty("version", 11);
    toPreV13Song(legacy);
    for (auto& track : *legacy.getDynamicObject()->getProperty("tracks").getArray())
    {
        auto* fx = track.getDynamicObject()->getProperty("fx").getDynamicObject();
        fx->removeProperty("drive");
        fx->removeProperty("chorus");
    }
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk()
            && loaded.tracks[0].fx.drive == sonora::DriveParams {} && loaded.tracks[0].fx.chorus == sonora::ChorusParams {},
            "v11 projects must open with drive/chorus bypassed");
    auto reject = [&](const char* effect, const char* field, juce::var value) {
        auto document = juce::JSON::parse(json);
        auto* fx = document.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
                       .getDynamicObject()->getProperty("fx").getDynamicObject();
        if (value.isVoid())
            fx->removeProperty(effect);
        else
            fx->getProperty(effect).getDynamicObject()->setProperty(field, value);
        loaded = saved;
        require(sonora::ProjectIO::decode(juce::JSON::toString(document), loaded).failed(),
                ("bad " + juce::String(effect) + " accepted").toRawUTF8());
        require(loaded == saved, "bad fx destroyed current state");
    };
    reject("drive", "amount", 1.5);
    reject("drive", "tone", 100.0);
    reject("chorus", "rate", 9.0);
    reject("chorus", "enabled", "yes");
    reject("drive", "", juce::var());
}

void testAiMelody()
{
    namespace ai = sonora::ai;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonora-ai-test", "");
    require(dir.createDirectory(), "AI test dir failed");
    auto script = [&](const char* name, const juce::String& body) {
        auto file = dir.getChildFile(name);
        require(file.replaceWithText("#!/bin/sh\n" + body, false, false, "\n"), "fake script write failed");
        require(file.setExecutePermission(true), "fake script chmod failed");
        return file;
    };

    // Arguments reach the child verbatim: no shell, no splitting, no expansion.
    const auto echoArgs = script("args", "for a in \"$@\"; do printf '[%s]' \"$a\"; done\n");
    auto run = ai::runProcess(echoArgs, { "a b", "", "$HOME", "; rm -rf /", "--tools" }, {}, dir, {}, 5000, nullptr);
    require(run.ok() && run.output == "[a b][][$HOME][; rm -rf /][--tools]", "arguments were not passed verbatim");

    // Stdin, working directory, and environment are exactly what we supply.
    const auto probe = script("probe", "cat; echo; pwd; echo \"key=${ANTHROPIC_API_KEY:-none} keep=${KEEP_ME:-none}\"\n");
    const auto env = ai::sanitizedEnvironment({ "ANTHROPIC_API_KEY=sk-secret", "ANTHROPIC_AUTH_TOKEN=tok",
                                                "KEEP_ME=yes", "PATH=/usr/bin:/bin" });
    require(!env.joinIntoString("\n").contains("sk-secret") && !env.joinIntoString("\n").contains("tok")
            && env.contains("KEEP_ME=yes"), "API keys not stripped from child environment");
    const auto workDir = dir.getChildFile("work");
    require(workDir.createDirectory(), "work dir failed");
    run = ai::runProcess(probe, {}, "hello from stdin", workDir, env, 5000, nullptr);
    require(run.ok() && run.output.startsWith("hello from stdin"), "stdin not delivered");
    require(run.output.contains(workDir.getFullPathName()), "child did not run in the sandbox folder");
    require(run.output.contains("key=none keep=yes"), "child environment wrong");

    // Large stdin with simultaneous output cannot deadlock.
    const auto big = juce::String::repeatedString("0123456789", 20000);
    run = ai::runProcess(script("cat", "cat\n"), {}, big, dir, env, 10000, nullptr);
    require(run.ok() && run.output == big, "large stdin round-trip failed");

    // Timeouts and cancellation stop the process group promptly.
    const auto sleeper = script("sleep", "sleep 30 & sleep 30\n");
    auto started = juce::Time::getMillisecondCounterHiRes();
    run = ai::runProcess(sleeper, {}, {}, dir, env, 300, nullptr);
    require(run.timedOut && !run.ok() && juce::Time::getMillisecondCounterHiRes() - started < 5000, "timeout did not stop child");
    std::atomic<bool> cancel { true };
    started = juce::Time::getMillisecondCounterHiRes();
    run = ai::runProcess(sleeper, {}, {}, dir, env, 60000, &cancel);
    require(run.cancelled && juce::Time::getMillisecondCounterHiRes() - started < 5000, "cancel did not stop child");
    require(!ai::runProcess(dir.getChildFile("missing"), {}, {}, dir, env, 1000, nullptr).ok(), "missing binary ran");

    // CLI hardening flags are always present; nothing that widens access.
    const auto args = ai::claudeArguments();
    const auto toolsAt = args.indexOf("--tools");
    require(toolsAt >= 0 && args[toolsAt + 1].isEmpty(), "tools not disabled");
    require(args.contains("--strict-mcp-config") && args.contains("--no-session-persistence")
            && args.contains("--json-schema") && args.contains("-p"), "hardening flag missing");
    const auto sources = args.indexOf("--setting-sources");
    require(sources >= 0 && args[sources + 1].isEmpty(), "user settings/hooks not disabled");
    for (const auto& arg : args)
        require(!arg.contains("dangerously") && !arg.contains("bypassPermissions") && arg != "--add-dir",
                "argument widens CLI access");
    require(juce::JSON::parse(ai::responseSchema()).getDynamicObject() != nullptr, "response schema is not JSON");

    // Context: tempo, other tracks (notes named, drum steps), target excluded,
    // user text cleaned and bounded.
    auto project = fixture();
    project.tracks[0].setTrackName("Lead \x01Line");
    project.tracks[2].id = 3;
    project.tracks[2].kind = sonora::TrackKind::Synth;
    project.tracks[2].instrumentPreset = 10;
    project.tracks[2].setTrackName("Bass");
    project.tracks[2].melodies[0].count = 1;
    project.tracks[2].melodies[0].notes[0] = { 1, 0, 960, 48, 100 };
    ai::MelodyRequest request;
    request.project = project;
    request.track = 0;
    request.prompt = "A bouncy hook\x07" + juce::String::repeatedString("x", 5000);
    const auto message = ai::buildUserMessage(request);
    require(message.contains("123 BPM") && message.contains("\"Bass\" (Fingered Bass)")
            && message.contains("pitch=48 (C3)"), "context missing other tracks");
    require(message.contains("Kick on sixteenth steps: 0,16,32,48"), "context missing drum steps");
    require(message.contains("\"Lead Line\"") && !message.contains("\x01") && !message.contains("\x07"),
            "control characters leaked into the model message");
    require(message.contains("A bouncy hook") && !message.contains(juce::String::repeatedString("x", 2001)),
            "user prompt not bounded");

    // Parsing: structured output, fenced text fallback, errors, sanitizing.
    auto envelope = [](const juce::String& notes, bool structured = true) {
        const auto body = "{\"title\":\"Hook\",\"explanation\":\"Answers the lead.\",\"notes\":[" + notes + "]}";
        return structured ? "{\"is_error\":false,\"result\":\"\",\"structured_output\":" + body + "}"
                          : "{\"is_error\":false,\"result\":" + juce::JSON::toString(juce::var("```json\n" + body + "\n```")) + "}";
    };
    auto parsed = ai::parseMelodyResponse(envelope("{\"start\":0,\"duration\":480,\"pitch\":60,\"velocity\":100},"
                                                   "{\"start\":480,\"duration\":480,\"pitch\":64,\"velocity\":90}"));
    require(parsed.ok() && parsed.pattern.count == 2 && parsed.pattern.valid() && parsed.title == "Hook",
            "structured melody not parsed");
    parsed = ai::parseMelodyResponse(envelope("{\"start\":0,\"duration\":480,\"pitch\":67,\"velocity\":100}", false));
    require(parsed.ok() && parsed.pattern.count == 1, "fenced text melody not parsed");
    parsed = ai::parseMelodyResponse("{\"is_error\":true,\"result\":\"Not logged in. Please run /login\"}");
    require(!parsed.ok() && parsed.error.contains("/login"), "CLI error not surfaced");
    require(!ai::parseMelodyResponse("garbage").ok(), "garbage accepted");
    require(!ai::parseMelodyResponse(envelope("")).ok(), "empty melody accepted");
    parsed = ai::parseMelodyResponse(envelope(
        "{\"start\":-50,\"duration\":99999,\"pitch\":84,\"velocity\":400},"   // clamped; 84 kept (full range)
        "{\"start\":960,\"duration\":480,\"pitch\":60,\"velocity\":80},"
        "{\"start\":960,\"duration\":240,\"pitch\":60,\"velocity\":70},"      // exact duplicate start
        "{\"start\":2000,\"duration\":100,\"pitch\":30,\"velocity\":0},"      // 30 kept, vel -> 1
        "{\"start\":\"x\",\"duration\":1,\"pitch\":60,\"velocity\":1}"));    // non-numeric dropped
    require(parsed.ok() && parsed.pattern.valid() && parsed.pattern.count == 3, "untrusted notes not sanitized");
    require(parsed.pattern.notes[0].pitch == 84 && parsed.pattern.notes[0].start == 0
            && parsed.pattern.notes[0].duration == sonora::patternTicks && parsed.pattern.notes[0].velocity == 127,
            "overlap trim/clamp wrong");
    require(parsed.pattern.notes[2].pitch == 30 && parsed.pattern.notes[2].velocity == 1, "pitch clamp wrong");
    juce::String many;
    for (int i = 0; i < 400; ++i)
        many << (i ? "," : "") << "{\"start\":" << (i * 30) % 15000 << ",\"duration\":30,\"pitch\":" << 48 + i % 24
             << ",\"velocity\":100}";
    parsed = ai::parseMelodyResponse(envelope(many));
    require(parsed.ok() && parsed.pattern.count <= sonora::Pattern::capacity && parsed.pattern.valid(), "capacity not capped");

    // End to end with a fake `claude` that checks it was sandboxed.
    const auto fake = script("claude",
        "case \"$*\" in *--strict-mcp-config*) ;; *) echo '{\"is_error\":true,\"result\":\"unsandboxed\"}'; exit 0;; esac\n"
        "[ -z \"$ANTHROPIC_API_KEY\" ] || { echo '{\"is_error\":true,\"result\":\"key leaked\"}'; exit 0; }\n"
        "[ -z \"$(ls -A .)\" ] || { echo '{\"is_error\":true,\"result\":\"cwd not empty\"}'; exit 0; }\n"
        "grep -q 'bouncy' || { echo '{\"is_error\":true,\"result\":\"no prompt\"}'; exit 0; }\n"
        "echo '" + envelope("{\"start\":0,\"duration\":960,\"pitch\":62,\"velocity\":100}").replace("'", "") + "'\n");
    setenv("SONORA_CLAUDE_PATH", fake.getFullPathName().toRawUTF8(), 1);
    setenv("ANTHROPIC_API_KEY", "sk-should-not-leak", 1);
    require(ai::findClaudeExecutable() == fake, "SONORA_CLAUDE_PATH ignored");
    const auto generated = ai::generateMelody(request, nullptr, 10000);
    require(generated.ok() && generated.pattern.count == 1 && generated.pattern.notes[0].pitch == 62,
            ("fake end-to-end generation failed: " + generated.error).toRawUTF8());
    setenv("SONORA_CLAUDE_PATH", "relative/claude", 1);
    require(!ai::findClaudeExecutable().getFullPathName().endsWith("relative/claude"), "relative CLI path trusted");
    unsetenv("SONORA_CLAUDE_PATH");
    unsetenv("ANTHROPIC_API_KEY");
    require(dir.deleteRecursively(), "AI test cleanup failed");

    // Opt-in: one real generation through the installed, signed-in Claude
    // Code CLI (uses the subscription). Never runs in normal test passes.
    if (juce::SystemStats::getEnvironmentVariable("SONORA_LIVE_CLAUDE", {}) == "1")
    {
        request.prompt = "A catchy, syncopated counter-melody that answers the other parts, mostly eighth notes.";
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto live = ai::generateMelody(request, nullptr);
        std::cout << "LIVE claude: " << (live.ok() ? "ok" : live.error) << " in "
                  << juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) << " s, "
                  << live.pattern.count << " notes, title=\"" << live.title << "\"\n  " << live.explanation << "\n";
        for (int i = 0; i < std::min(live.pattern.count, 12); ++i)
        {
            const auto& n = live.pattern.notes[static_cast<std::size_t>(i)];
            std::cout << "    start=" << n.start << " dur=" << n.duration << " pitch=" << n.pitch << " vel=" << n.velocity << "\n";
        }
        require(live.ok() && live.pattern.valid() && live.pattern.count >= 4, "live Claude generation failed");
    }
}

void testBackgroundLoading()
{
    // The hardware sender never blocks the poster: malformed frames are
    // dropped, rapid posts coalesce, and shutdown flushes the latest.
    {
        juce::CriticalSection lock;
        std::vector<sonora::minilab::Bytes> received;
        sonora::MiniLabSender sender([&](const sonora::minilab::Bytes& bytes) {
            const juce::ScopedLock guard(lock);
            received.push_back(bytes);
        });
        sender.post({ 0x01, 0x02 }); // no SysEx framing: dropped
        sender.post(sonora::minilab::deviceInquiry());
        for (int i = 0; i < 20; ++i)
            sender.post(sonora::minilab::screenMessage("K" + juce::String(i), "v"));
        sender.shutdown();
        const juce::ScopedLock guard(lock);
        require(!received.empty(), "sender delivered nothing");
        for (const auto& bytes : received)
            require(bytes.front() == 0xF0 && bytes.back() == 0xF7, "sender passed a malformed frame");
        require(received.back() == sonora::minilab::screenMessage("K19", "v"), "shutdown lost the latest frame");
        require(received.size() <= 22, "sender did not coalesce");
    }
    // Loaders honor cancellation so superseded jobs die promptly instead of
    // piling up behind a slow resample.
    {
        std::array<sonora::AudioTakeMeta, sonora::maxTakes> takes {};
        takes[0].id = 1;
        takes[0].setFileName("missing.wav");
        takes[0].frames = 48000;
        const auto cancelled = sonora::loadTakes(takes, 1, juce::File(), 48000.0,
                                                 [](double) { return false; });
        require(cancelled == nullptr, "cancelled take load returned a set");
        const auto bank = sonora::loadSampleBank({}, juce::File(), 48000.0, 0,
                                                 [](double) { return false; });
        require(bank == nullptr, "cancelled bank load returned a bank");
    }
}

void testMiniLabDisplay()
{
    namespace ml = sonora::minilab;
    using B = ml::Bytes;
    // Byte-exact against the hardware-verified reference profile.
    require(ml::deviceInquiry() == B { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }, "device inquiry bytes");
    require(ml::connectDaw() == B { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x02, 0x40, 0x6A, 0x21, 0xF7 }, "DAW connect bytes");
    require(ml::disconnectDaw() == B { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x02, 0x40, 0x6A, 0x20, 0xF7 }, "DAW disconnect bytes");
    require(ml::requestMode() == B { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x01, 0x00, 0x40, 0x01, 0xF7 }, "mode request bytes");
    require(ml::requestPadBank() == B { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x01, 0x00, 0x40, 0x03, 0xF7 }, "pad bank request bytes");
    B screen { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x04, 0x02, 0x60, 0x1F, 0x07, 0x01, 0x00, 0x00, 0x01, 0x00,
               0x01, 'C', 'O', 'D', 'E', 'X', 0x00, 0x02 };
    for (char c : juce::String("Connected").toStdString())
        screen.push_back(static_cast<std::uint8_t>(c));
    screen.insert(screen.end(), { 0x00, 0xF7 });
    require(ml::screenMessage("CODEX", "Connected") == screen, "screen message bytes");

    // Lines are truncated to 10/18 chars and non-ASCII becomes '?'; every
    // data byte stays 7-bit so the SysEx is always well-formed.
    const auto long1 = ml::screenMessage("DISTORTION AND MORE", juce::String::fromUTF8("42% caf\xc3\xa9 overdriven guitar"));
    require(ml::asciiLine("DISTORTION AND MORE", ml::line1Chars).size() == 10, "line 1 not truncated");
    require(ml::asciiLine(juce::String::fromUTF8("caf\xc3\xa9"), 18) == B { 'c', 'a', 'f', '?' }, "non-ASCII not replaced");
    for (std::size_t i = 1; i + 1 < long1.size(); ++i)
        require(long1[i] < 0x80, "screen SysEx has a non-7-bit byte");
    require(long1.front() == 0xF0 && long1.back() == 0xF7, "screen SysEx framing");

    // Pads: 8 x 7-bit RGB, scaled by brightness, for the requested bank.
    const auto pads = ml::padBankMessage(ml::padBankB, 0xFF8000u, 0.5f);
    require(pads.size() == 6 + 4 + 24 + 1 && pads[9] == 0x40, "pad bank message shape");
    require(pads[10] == 64 && pads[11] == 32 && pads[12] == 0 && pads[31] == 64, "pad colour scaling");
    for (std::size_t i = 1; i + 1 < pads.size(); ++i)
        require(pads[i] < 0x80, "pad SysEx has a non-7-bit byte");

    // Replies from the device.
    auto kind = [](const B& bytes) { return ml::classify(bytes.data(), static_cast<int>(bytes.size())); };
    require(kind({ 0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x00, 0x20, 0x6B, 0x02, 0x00, 0x04, 0x02, 0x01, 0x00, 0x00, 0x00, 0xF7 })
            == ml::Reply::DeviceIdentity, "device identity not recognised");
    require(kind(ml::arturia({ 0x02, 0x00, 0x40, 0x01, 0x01 })) == ml::Reply::DawMode, "DAW mode reply");
    require(kind(ml::arturia({ 0x02, 0x00, 0x40, 0x01, 0x00 })) == ml::Reply::ArturiaMode, "Arturia mode reply");
    require(kind({ 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x00, 0x40, 0x62, 0x02, 0xF7 }) == ml::Reply::DawModeChanged,
            "DAW mode change announcement");
    require(kind(ml::arturia({ 0x02, 0x00, 0x40, 0x62, 0x01 })) == ml::Reply::ArturiaModeChanged, "Arturia mode change");
    require(kind(ml::arturia({ 0x02, 0x00, 0x40, 0x63, 0x01 })) == ml::Reply::PadBankB, "pad bank B reply");
    // Replies captured from the connected MiniLab 3 (amidi, Arturia program).
    require(kind({ 0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x00, 0x20, 0x6B, 0x02, 0x00, 0x04, 0x04, 0x49, 0x06, 0x00, 0x01, 0xF7 })
            == ml::Reply::DeviceIdentity, "captured identity reply");
    require(kind({ 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x00, 0x40, 0x01, 0x00, 0xF7 }) == ml::Reply::ArturiaMode,
            "captured mode reply");
    require(kind({ 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x00, 0x40, 0x03, 0x00, 0xF7 }) == ml::Reply::PadBankA,
            "captured pad bank reply");
    require(kind({ 0xF0, 0x43, 0x10, 0xF7 }) == ml::Reply::None && kind({}) == ml::Reply::None, "foreign SysEx misread");

    // What the screen says: the track at rest, the knob while turning.
    auto project = fixture();
    auto guitar = project.tracks[0];
    guitar.instrumentPreset = 8;
    guitar.setTrackName("Overdriven Guitar");
    sonora::applyKnob(guitar, sonora::KnobTarget::Drive, 0.42f);
    const auto turning = ml::knobScreen(guitar, 0);
    require(turning.first == "DISTORTION" && turning.second.startsWith("42%  Overdriven"), "knob screen text");
    require(turning.second.length() <= ml::line2Chars, "knob screen line 2 too long");
    require(ml::knobScreen(guitar, 7).first == "REVERB", "reverb knob screen");
    const auto rest = ml::trackScreen(guitar);
    require(rest.first == "OVERDRIVEN GUITAR" && rest.second == "Overdriven Guitar", "track screen text");
    require(ml::trackScreen(project.tracks[1]).second == "Drum kit", "drum track screen");
    require(ml::trackScreen(project.tracks[0]).second == "Sonora Synth", "synth track screen");
}

void testSongComposition()
{
    using sonora::SongPart;
    // Section editing moves whole columns (part, slots, on/off) together.
    sonora::Arrangement song;
    song.sections = 3;
    for (int s = 0; s < 3; ++s)
    {
        song.parts[static_cast<std::size_t>(s)] = static_cast<SongPart>(s + 1); // Intro, Verse, Pre-Chorus
        song.slots[static_cast<std::size_t>(s)][0] = static_cast<std::uint8_t>(s);
        song.trackOn[static_cast<std::size_t>(s)][1] = s != 1;
    }
    auto check = [&](int s, SongPart part, int slot, bool drumsOn, const char* what) {
        require(song.parts[static_cast<std::size_t>(s)] == part && song.slots[static_cast<std::size_t>(s)][0] == slot
                && song.trackOn[static_cast<std::size_t>(s)][1] == drumsOn, what);
    };
    require(song.duplicateSection(1) && song.sections == 4, "duplicate failed");
    check(1, SongPart::Verse, 1, false, "duplicate source moved");
    check(2, SongPart::Verse, 1, false, "duplicate is not a copy");
    check(3, SongPart::PreChorus, 2, true, "later section not shifted");
    require(song.moveSection(3, 0), "move failed");
    check(0, SongPart::PreChorus, 2, true, "moved section wrong");
    check(1, SongPart::Intro, 0, true, "move did not shift others");
    require(song.removeSection(0) && song.sections == 3, "remove failed");
    check(0, SongPart::Intro, 0, true, "remove did not close the gap");
    require(song.insertSection(3) && song.sections == 4 && song.parts[3] == SongPart::Section
            && song.trackOn[3][0] && song.trackOn[3][1], "empty insert not a fresh, playing section");
    require(!song.moveSection(0, 9) && !song.removeSection(7) && !song.insertSection(9), "out-of-range edit accepted");
    sonora::Arrangement single;
    single.sections = 1;
    require(!single.removeSection(0), "last section removed");
    sonora::Arrangement full;
    full.sections = sonora::maxSections;
    require(!full.duplicateSection(0) && full.sections == sonora::maxSections, "section overflow");
    require(song.valid() && full.valid(), "edited arrangement invalid");

    // Templates: named parts, drums out of intros/outros, and slot fallback
    // so a project with only pattern A still plays everywhere.
    auto project = fixture();
    for (int t = 0; t < static_cast<int>(sonora::SongTemplate::numTemplates); ++t)
    {
        const auto tpl = static_cast<sonora::SongTemplate>(t);
        const auto built = sonora::buildSongFromTemplate(project, tpl);
        const auto parts = sonora::songTemplateParts(tpl);
        require(built.valid() && built.sections == static_cast<int>(parts.size()), "template section count");
        for (int s = 0; s < built.sections; ++s)
        {
            const auto part = built.parts[static_cast<std::size_t>(s)];
            require(part == parts[static_cast<std::size_t>(s)], "template part order");
            require(built.slots[static_cast<std::size_t>(s)][0] == 0 && built.slots[static_cast<std::size_t>(s)][1] == 0,
                    "template picked an empty pattern");
            const bool drumless = part == SongPart::Intro || part == SongPart::Outro || part == SongPart::Break;
            require(built.trackOn[static_cast<std::size_t>(s)][1] == !drumless && built.trackOn[static_cast<std::size_t>(s)][0],
                    "template track gating");
        }
    }
    auto withChorus = project;
    withChorus.tracks[0].melodies[1] = withChorus.tracks[0].melodies[0];
    const auto pop = sonora::buildSongFromTemplate(withChorus, sonora::SongTemplate::Pop);
    require(pop.parts[3] == SongPart::Chorus && pop.slots[3][0] == 1 && pop.slots[1][0] == 0,
            "choruses should use pattern B when it exists");

    // Loop roles describe character; slot suggestions follow the music.
    require(sonora::describeLoopRole(project.tracks[1], 0).contains("four-on-the-floor kick"), "kick role missed");
    require(sonora::describeLoopRole(project.tracks[1], 0).contains("driving hats"), "hat role missed");
    require(sonora::describeLoopRole(project.tracks[1], 1) == "empty", "empty loop mislabeled");
    require(sonora::describeLoopRole(project.tracks[0], 0).contains("notes"), "melody role missed");
    auto varied = project;
    varied.tracks[0].melodies[1].count = 16; // dense chorus candidate
    for (int n = 0; n < 16; ++n)
        varied.tracks[0].melodies[1].notes[static_cast<std::size_t>(n)] = {
            static_cast<std::uint32_t>(n + 1), n * 960, 240, 60 + (n % 12), 110 };
    varied.tracks[0].melodies[2].count = 1; // sparse bridge candidate
    varied.tracks[0].melodies[2].notes[0] = { 30, 0, 3840, 48, 80 };
    using sonora::SongPart;
    require(sonora::suggestSlotForPart(varied.tracks[0], SongPart::Chorus) == 1, "chorus wants the densest loop");
    require(sonora::suggestSlotForPart(varied.tracks[0], SongPart::Drop) == 1, "drop wants the densest loop");
    require(sonora::suggestSlotForPart(varied.tracks[0], SongPart::Verse) == 2, "verse wants the sparsest loop");
    require(sonora::suggestSlotForPart(varied.tracks[0], SongPart::Intro) == 2, "intro wants the sparsest loop");
    require(sonora::suggestSlotForPart(varied.tracks[0], SongPart::Build) == 0, "build wants the runner-up loop");
    require(sonora::suggestSlotForPart(varied.tracks[0], SongPart::Bridge) == 0, "bridge wants the runner-up loop");
    sonora::Track empty;
    empty.kind = sonora::TrackKind::Synth;
    require(sonora::suggestSlotForPart(empty, SongPart::Chorus) == 1, "empty track lost the old default");
    require(sonora::suggestSlotForPart(empty, SongPart::Verse) == 0, "empty track lost the old default");
    const auto smart = sonora::buildSongFromTemplate(varied, sonora::SongTemplate::Pop);
    require(smart.slots[3][0] == 1 && smart.slots[1][0] == 2, "template ignored loop character");

    // v13 saves 16 parts with names; v12 files (8 rows) still open.
    auto saved = project;
    saved.song = pop;
    saved.song.insertSection(pop.sections, 0);
    const auto json = sonora::ProjectIO::encode(saved);
    sonora::ProjectState loaded;
    require(json.contains("\"version\": 22"), "song projects must save as v22");
    require(sonora::ProjectIO::decode(json, loaded).wasOk() && loaded == saved, "song parts round-trip failed");
    auto big = project;
    big.song.sections = sonora::maxSections;
    require(sonora::ProjectIO::decode(sonora::ProjectIO::encode(big), loaded).wasOk() && loaded.song.sections == 16,
            "16-part song round-trip failed");
    auto legacy = juce::JSON::parse(sonora::ProjectIO::encode(project));
    legacy.getDynamicObject()->setProperty("version", 12);
    toPreV13Song(legacy);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacy), loaded).wasOk()
            && loaded.song.parts[0] == SongPart::Section, "v12 song did not open");
    auto badPart = juce::JSON::parse(json);
    badPart.getDynamicObject()->getProperty("song").getDynamicObject()->getProperty("parts").getArray()->set(0, 42);
    require(sonora::ProjectIO::decode(juce::JSON::toString(badPart), loaded).failed(), "unknown part accepted");
    auto shortRows = juce::JSON::parse(json);
    toPreV13Song(shortRows);
    require(sonora::ProjectIO::decode(juce::JSON::toString(shortRows), loaded).failed(), "v15 with 8 rows accepted");

    // v14 persists per-track swing; v13 files open straight.
    auto swung = project;
    swung.tracks[0].swing = 0.25f;
    swung.tracks[1].swing = 0.5f;
    const auto swingJson = sonora::ProjectIO::encode(swung);
    require(sonora::ProjectIO::decode(swingJson, loaded).wasOk() && loaded == swung
            && loaded.tracks[0].swing == 0.25f, "swing round-trip failed");
    auto legacySwing = juce::JSON::parse(swingJson);
    legacySwing.getDynamicObject()->setProperty("version", 13);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacySwing), loaded).wasOk()
            && loaded.tracks[0].swing == 0.0f, "v13 did not open with straight swing");
    auto badSwing = juce::JSON::parse(swingJson);
    badSwing.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->setProperty("swing", 2.0);
    loaded = swung;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badSwing), loaded).failed(), "over-range swing accepted");
    require(loaded == swung, "bad swing destroyed current state");
    auto missingSwing = juce::JSON::parse(swingJson);
    missingSwing.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("swing");
    loaded = swung;
    require(sonora::ProjectIO::decode(juce::JSON::toString(missingSwing), loaded).failed(), "v22 without swing accepted");
    auto invalidSwing = swung;
    invalidSwing.tracks[0].swing = -0.5f;
    require(!invalidSwing.valid(), "negative swing validated");

    // v15 persists the song key; older files open in C major.
    auto keyed = project;
    keyed.musicKey = 9;
    keyed.musicScale = sonora::MusicScale::Dorian;
    const auto keyJson = sonora::ProjectIO::encode(keyed);
    require(sonora::ProjectIO::decode(keyJson, loaded).wasOk() && loaded == keyed
            && loaded.musicKey == 9
            && juce::String(sonora::scaleName(loaded.musicScale)) == "Dorian", "key round-trip failed");
    auto legacyKey = juce::JSON::parse(keyJson);
    legacyKey.getDynamicObject()->setProperty("version", 14);
    legacyKey.getDynamicObject()->removeProperty("musicKey");
    legacyKey.getDynamicObject()->removeProperty("musicScale");
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacyKey), loaded).wasOk()
            && loaded.musicKey == 0 && loaded.musicScale == sonora::MusicScale::Major,
            "v14 did not open in C major");
    auto badKey = juce::JSON::parse(keyJson);
    badKey.getDynamicObject()->setProperty("musicKey", 12);
    loaded = keyed;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badKey), loaded).failed(), "out-of-range key accepted");
    require(loaded == keyed, "bad key destroyed current state");
    auto badScale = juce::JSON::parse(keyJson);
    badScale.getDynamicObject()->setProperty("musicScale", 99);
    loaded = keyed;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badScale), loaded).failed(), "unknown scale accepted");
    auto missingKey = juce::JSON::parse(keyJson);
    missingKey.getDynamicObject()->removeProperty("musicKey");
    loaded = keyed;
    require(sonora::ProjectIO::decode(juce::JSON::toString(missingKey), loaded).failed(), "v22 without key accepted");
    auto invalidKey = keyed;
    invalidKey.musicKey = -1;
    require(!invalidKey.valid(), "negative key validated");

    // v16 persists live FX; older files open with FX off.
    auto lively = project;
    lively.tracks[0].liveFx.arp = sonora::ArpMode::Up;
    lively.tracks[0].liveFx.rate = sonora::ArpRate::Eighth;
    lively.tracks[0].liveFx.octaves = 2;
    lively.tracks[0].liveFx.latch = true;
    lively.tracks[0].liveFx.chordOn = true;
    lively.tracks[0].liveFx.chord = sonora::ChordType::Min7;
    const auto liveJson = sonora::ProjectIO::encode(lively);
    require(sonora::ProjectIO::decode(liveJson, loaded).wasOk() && loaded == lively
            && loaded.tracks[0].liveFx.arp == sonora::ArpMode::Up
            && loaded.tracks[0].liveFx.octaves == 2 && loaded.tracks[0].liveFx.latch
            && loaded.tracks[0].liveFx.chordOn
            && loaded.tracks[0].liveFx.chord == sonora::ChordType::Min7,
            "live FX round-trip failed");
    auto legacyLive = juce::JSON::parse(liveJson);
    legacyLive.getDynamicObject()->setProperty("version", 15);
    for (const char* key : { "liveArp", "liveArpRate", "liveArpOctaves", "liveArpLatch", "liveChordOn",
                             "liveChord" })
        legacyLive.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
            .getDynamicObject()->removeProperty(key);
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacyLive), loaded).wasOk()
            && loaded.tracks[0].liveFx == sonora::LiveFx {},
            "v15 did not open with live FX off");
    auto badArp = juce::JSON::parse(liveJson);
    badArp.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->setProperty("liveArp", 99);
    loaded = lively;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badArp), loaded).failed(),
            "unknown arp mode accepted");
    require(loaded == lively, "bad arp destroyed current state");
    auto missingArp = juce::JSON::parse(liveJson);
    missingArp.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("liveArpRate");
    loaded = lively;
    require(sonora::ProjectIO::decode(juce::JSON::toString(missingArp), loaded).failed(),
            "v22 without arp rate accepted");
    auto invalidLive = lively;
    invalidLive.tracks[0].liveFx.octaves = 4;
    require(!invalidLive.valid(), "arp octave validated");

    // v18 persists the chord track; older files open chordless.
    auto chorded = project;
    chorded.musicKey = 0;
    chorded.song.sections = 4;
    chorded.song.chords[0] = { 9, sonora::ChordType::Minor };
    chorded.song.chords[3] = { 5, sonora::ChordType::Major };
    require(chorded.valid(), "chord fixture rejected");
    const auto chordJson = sonora::ProjectIO::encode(chorded);
    require(sonora::ProjectIO::decode(chordJson, loaded).wasOk() && loaded == chorded
            && juce::String(sonora::chordLabel(loaded.song.chords[0])) == "Am"
            && juce::String(sonora::chordLabel(loaded.song.chords[3])) == "F"
            && sonora::chordTranspose(loaded.song.chords[3], 0) == 5,
            "chord track round-trip failed");
    auto legacyChords = juce::JSON::parse(chordJson);
    legacyChords.getDynamicObject()->setProperty("version", 17);
    legacyChords.getDynamicObject()->getProperty("song").getDynamicObject()->removeProperty("chords");
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacyChords), loaded).wasOk()
            && !loaded.song.chords[0].set() && !loaded.song.chords[3].set(),
            "v17 did not open chordless");
    auto badRoot = juce::JSON::parse(chordJson);
    badRoot.getDynamicObject()->getProperty("song").getDynamicObject()->getProperty("chords")
        .getArray()->getReference(0).getDynamicObject()->setProperty("root", 12);
    loaded = chorded;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badRoot), loaded).failed(),
            "out-of-range chord root accepted");
    require(loaded == chorded, "bad chord destroyed current state");
    auto missingChords = juce::JSON::parse(chordJson);
    missingChords.getDynamicObject()->getProperty("song").getDynamicObject()->removeProperty("chords");
    loaded = chorded;
    require(sonora::ProjectIO::decode(juce::JSON::toString(missingChords), loaded).failed(),
            "v22 without chords accepted");

    // v19 persists pan/sends and the return buses; older files mix dry.
    auto mixed = project;
    mixed.tracks[0].mix.pan = -0.5f;
    mixed.tracks[0].mix.sendDelay = 0.7f;
    mixed.tracks[1].mix.sendReverb = 0.4f;
    mixed.sends.delay.timeMs = 250.0f;
    mixed.sends.delayReturn = 1.2f;
    mixed.sends.reverb.size = 0.9f;
    require(mixed.valid(), "mix fixture rejected");
    const auto mixJson = sonora::ProjectIO::encode(mixed);
    require(sonora::ProjectIO::decode(mixJson, loaded).wasOk() && loaded == mixed
            && loaded.tracks[0].mix.pan == -0.5f && loaded.tracks[0].mix.sendDelay == 0.7f
            && loaded.sends.delay.timeMs == 250.0f && loaded.sends.delayReturn == 1.2f
            && loaded.sends.reverb.size == 0.9f,
            "mixer round-trip failed");
    auto legacyMix = juce::JSON::parse(mixJson);
    legacyMix.getDynamicObject()->setProperty("version", 18);
    legacyMix.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("pan");
    legacyMix.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("sendDelay");
    legacyMix.getDynamicObject()->removeProperty("sends");
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacyMix), loaded).wasOk()
            && loaded.tracks[0].mix == sonora::TrackMix {}
            && loaded.sends == sonora::ProjectSends {},
            "v18 did not open with a dry mix");
    auto badPan = juce::JSON::parse(mixJson);
    badPan.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->setProperty("pan", 2.0);
    loaded = mixed;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badPan), loaded).failed(),
            "wide pan accepted");
    require(loaded == mixed, "bad pan destroyed current state");
    auto badReturn = juce::JSON::parse(mixJson);
    badReturn.getDynamicObject()->getProperty("sends").getDynamicObject()->setProperty("delayReturn", 9.0);
    loaded = mixed;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badReturn), loaded).failed(),
            "clipping return accepted");
    auto missingPan = juce::JSON::parse(mixJson);
    missingPan.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("sendReverb");
    loaded = mixed;
    require(sonora::ProjectIO::decode(juce::JSON::toString(missingPan), loaded).failed(),
            "v22 without sends accepted");

    // v20 persists automation lanes; older files play the knob values.
    auto curved = project;
    auto& swell = curved.tracks[0]
                      .automation[0][static_cast<std::size_t>(sonora::AutomationTarget::Volume)];
    swell.count = 2;
    swell.points[0] = { 0, 0.8f };
    swell.points[1] = { 1000, 0.2f };
    auto& sweep = curved.tracks[0]
                      .automation[1][static_cast<std::size_t>(sonora::AutomationTarget::Pan)];
    sweep.count = 1;
    sweep.points[0] = { 0, -1.0f };
    require(curved.valid(), "automation fixture rejected");
    const auto curveJson = sonora::ProjectIO::encode(curved);
    require(sonora::ProjectIO::decode(curveJson, loaded).wasOk() && loaded == curved
            && std::abs(loaded.tracks[0].automation[0][static_cast<std::size_t>(sonora::AutomationTarget::Volume)]
                            .eval(500, 9.0f)
                        - 0.5f)
                   < 1.0e-6f,
            "automation round-trip failed");
    auto legacyCurve = juce::JSON::parse(curveJson);
    legacyCurve.getDynamicObject()->setProperty("version", 19);
    legacyCurve.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("automation");
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacyCurve), loaded).wasOk()
            && loaded.tracks[0].automation[0][static_cast<std::size_t>(sonora::AutomationTarget::Volume)]
                       .count == 0,
            "v19 did not open with empty lanes");
    auto badTickArray = juce::JSON::parse(curveJson);
    badTickArray.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->getProperty("automation").getArray()->getReference(0)
        .getDynamicObject()->getProperty("points").getArray()->getReference(0)
        = juce::var(juce::Array<juce::var> { 99999999, 0.5 });
    loaded = curved;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badTickArray), loaded).failed(),
            "out-of-range automation tick accepted");
    require(loaded == curved, "bad tick destroyed current state");
    auto badValue = juce::JSON::parse(curveJson);
    badValue.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->getProperty("automation").getArray()->getReference(0)
        .getDynamicObject()->getProperty("points").getArray()->getReference(0)
        = juce::var(juce::Array<juce::var> { 0, 5.0 });
    loaded = curved;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badValue), loaded).failed(),
            "out-of-range automation value accepted");
    auto missingAutomation = juce::JSON::parse(curveJson);
    missingAutomation.getDynamicObject()->getProperty("tracks").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("automation");
    loaded = curved;
    require(sonora::ProjectIO::decode(juce::JSON::toString(missingAutomation), loaded).failed(),
            "v22 without automation accepted");

    // v21 persists take stretch; older files play at speed.
    auto speedy = project;
    speedy.takeCount = 1;
    speedy.takes[0].id = 4;
    speedy.takes[0].setFileName("take4.wav");
    speedy.takes[0].frames = 4800;
    speedy.takes[0].stretch = 1.5f;
    require(speedy.valid(), "stretch fixture rejected");
    const auto speedyJson = sonora::ProjectIO::encode(speedy);
    require(sonora::ProjectIO::decode(speedyJson, loaded).wasOk() && loaded == speedy
            && loaded.takes[0].stretch == 1.5f,
            "take stretch round-trip failed");
    auto legacySpeed = juce::JSON::parse(speedyJson);
    legacySpeed.getDynamicObject()->setProperty("version", 20);
    legacySpeed.getDynamicObject()->getProperty("takes").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("stretch");
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacySpeed), loaded).wasOk()
            && loaded.takes[0].stretch == 1.0f,
            "v20 did not open at speed");
    auto badStretch = juce::JSON::parse(speedyJson);
    badStretch.getDynamicObject()->getProperty("takes").getArray()->getReference(0)
        .getDynamicObject()->setProperty("stretch", 0.1);
    loaded = speedy;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badStretch), loaded).failed(),
            "silly stretch accepted");
    require(loaded == speedy, "bad stretch destroyed current state");
    auto missingStretch = juce::JSON::parse(speedyJson);
    missingStretch.getDynamicObject()->getProperty("takes").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("stretch");
    loaded = speedy;
    require(sonora::ProjectIO::decode(juce::JSON::toString(missingStretch), loaded).failed(),
            "v22 without stretch accepted");
    auto invalidStretch = speedy;
    invalidStretch.takes[0].stretch = 3.0f;
    require(!invalidStretch.valid(), "triple-speed take validated");
    auto invalidLane = curved;
    invalidLane.tracks[0].automation[0][static_cast<std::size_t>(sonora::AutomationTarget::Volume)].count = 99;
    require(!invalidLane.valid(), "overfull lane validated");

    // v17 persists take solos; older files open with solos off.
    auto comped = project;
    comped.takeCount = 2;
    comped.takes[0].id = 7;
    comped.takes[0].setFileName("take7.wav");
    comped.takes[0].frames = 4800;
    comped.takes[0].solo = true;
    comped.takes[1].id = 9;
    comped.takes[1].setFileName("take9.wav");
    comped.takes[1].frames = 4800;
    require(comped.valid(), "solo fixture rejected");
    const auto compJson = sonora::ProjectIO::encode(comped);
    require(sonora::ProjectIO::decode(compJson, loaded).wasOk() && loaded == comped
            && loaded.takes[0].solo && !loaded.takes[1].solo,
            "take solo round-trip failed");
    auto legacyComp = juce::JSON::parse(compJson);
    legacyComp.getDynamicObject()->setProperty("version", 16);
    legacyComp.getDynamicObject()->getProperty("takes").getArray()->getReference(0)
        .getDynamicObject()->removeProperty("solo");
    require(sonora::ProjectIO::decode(juce::JSON::toString(legacyComp), loaded).wasOk()
            && !loaded.takes[0].solo && !loaded.takes[1].solo,
            "v16 did not open with solos off");
    auto badSolo = juce::JSON::parse(compJson);
    badSolo.getDynamicObject()->getProperty("takes").getArray()->getReference(0)
        .getDynamicObject()->setProperty("solo", "yes");
    loaded = comped;
    require(sonora::ProjectIO::decode(juce::JSON::toString(badSolo), loaded).failed(),
            "non-bool take solo accepted");
    require(loaded == comped, "bad solo destroyed current state");

    // Play from a part: only part 3 has the melody switched on.
    auto songProject = project;
    songProject.tracks[1].drumPatterns[0] = {};
    songProject.songMode = true;
    songProject.song.sections = 3;
    for (int s = 0; s < 3; ++s)
        songProject.song.trackOn[static_cast<std::size_t>(s)][0] = s == 2;
    auto firstBlocks = [&](int startSection, unsigned mask, const sonora::ProjectState& state) {
        auto engineStorage = std::make_unique<sonora::AudioEngine>();
        auto& engine = *engineStorage;
        engine.prepare(48000.0);
        engine.setSongStartSection(startSection);
        engine.setLoopTrackMask(mask);
        require(engine.submit(state), "song project rejected");
        engine.setPlaying(true);
        juce::AudioBuffer<float> buffer(2, 512);
        float peak = 0.0f;
        for (int i = 0; i < 20; ++i)
        {
            engine.process({ &buffer, 0, 512 });
            peak = std::max(peak, buffer.getMagnitude(0, 512));
        }
        return peak;
    };
    require(firstBlocks(0, ~0u, songProject) < 1.0e-6f, "song started in the wrong place");
    require(firstBlocks(2, ~0u, songProject) > 0.01f, "play-from-part did not start at the part");
    // Loop preview of a part silences tracks switched off in it.
    auto loopProject = songProject;
    loopProject.songMode = false;
    require(firstBlocks(0, ~0u, loopProject) > 0.01f, "loop preview silent");
    require(firstBlocks(0, ~0u & ~1u, loopProject) < 1.0e-6f, "gated track still sounds in part preview");
}

void testAiAssistant()
{
    namespace ai = sonora::ai;
    auto project = fixture();
    project.tracks[2].id = 3;
    project.tracks[2].kind = sonora::TrackKind::Synth;
    project.tracks[2].instrumentPreset = 10;
    project.tracks[2].setTrackName("Bass");
    project.tracks[2].melodies[1].count = 1;
    project.tracks[2].melodies[1].notes[0] = { 1, 0, 960, 50, 100 };
    project.song = sonora::buildSongFromTemplate(project, sonora::SongTemplate::Simple);
    project.song.slots[2][1] = 1;       // chorus drums use loop B
    project.song.slots[2][2] = 1;       // chorus bass uses loop B
    project.song.trackOn[2][0] = false; // lead is silent in the chorus
    project.tracks[1].drumPatterns[1].steps[1][4] = 90;

    // Working on the chorus, targeting the drums.
    ai::AssistantRequest request;
    request.project = project;
    request.track = 1;
    request.part = 2;
    request.message = "Add a snare fill in bar 4";
    for (int i = 0; i < 20; ++i)
        request.history.push_back({ i % 2 == 0, "turn " + juce::String(i) });
    require(ai::assistantTargetSlot(request) == 1, "target slot should follow the part");
    auto message = ai::buildAssistantMessage(request);
    require(message.contains("Working on part 3, Chorus (bars 9-12)") && message.contains("3 Chorus (current)"),
            "part context missing");
    require(message.contains("\"Sine Keys\"") && message.contains("silent in this part"), "silent track not marked");
    require(message.contains("pitch=50 (D3)"), "other track should use the part's loop");
    require(message.contains("replacing loop B") && message.contains("Loop B (target)")
            && message.contains("Snare on sixteenth steps: 4"),
            "target's current beat missing (needed for edits)");
    require(message.contains("Producer: turn 8") && !message.contains("Producer: turn 6"),
            "history not limited to the most recent turns");
    require(message.contains("Add a snare fill in bar 4"), "user message missing");
    // A free loop uses the preview slots instead.
    request.part = -1;
    request.drumSlots[1] = 3;
    require(ai::assistantTargetSlot(request) == 3 && ai::buildAssistantMessage(request).contains("free 4-bar loop"),
            "free-loop context wrong");

    // Hardened CLI arguments and per-kind schemas.
    for (bool drums : { false, true })
    {
        const auto args = ai::assistantArguments(drums);
        const auto tools = args.indexOf("--tools");
        require(tools >= 0 && args[tools + 1].isEmpty() && args.contains("--strict-mcp-config")
                && args.contains("--no-session-persistence"), "assistant not sandboxed");
        const auto schema = juce::JSON::parse(ai::assistantSchema(drums));
        require(schema.getDynamicObject() != nullptr, "assistant schema not JSON");
        require(ai::assistantSchema(drums).contains(drums ? "\"hits\"" : "\"notes\""), "schema kind wrong");
    }
    require(ai::assistantSystemPrompt(true).contains("0 = Kick") && ai::assistantSystemPrompt(true).contains("7 = Shaker"),
            "drum prompt missing pad map");

    // Parsing: reply-only, melody edit, drum edit with untrusted hits, clear.
    auto wrap = [](const juce::String& body) {
        return "{\"is_error\":false,\"result\":\"\",\"structured_output\":" + body + "}";
    };
    auto talk = ai::parseAssistantResponse(wrap("{\"reply\":\"Sounds good!\",\"change\":false,\"notes\":[]}"), false);
    require(talk.ok() && !talk.changed && talk.reply == "Sounds good!", "reply-only turn wrong");
    auto melody = ai::parseAssistantResponse(wrap("{\"reply\":\"Simpler now.\",\"change\":true,\"notes\":["
                                                  "{\"start\":0,\"duration\":960,\"pitch\":84,\"velocity\":100}]}"), false);
    require(melody.ok() && melody.changed && melody.pattern.count == 1 && melody.pattern.notes[0].pitch == 84,
            "melody edit not sanitized");
    auto beat = ai::parseAssistantResponse(wrap("{\"reply\":\"Fill added.\",\"change\":true,\"hits\":["
                                                "{\"pad\":0,\"step\":0,\"velocity\":120},"
                                                "{\"pad\":0,\"step\":0,\"velocity\":80},"   // duplicate: keep loudest
                                                "{\"pad\":1,\"step\":60,\"velocity\":300}," // velocity clamped
                                                "{\"pad\":9,\"step\":4,\"velocity\":100},"  // unknown pad dropped
                                                "{\"pad\":2,\"step\":64,\"velocity\":100}," // past the loop dropped
                                                "{\"pad\":\"x\",\"step\":1,\"velocity\":1}]}"), true);
    require(beat.ok() && beat.changed && beat.drums && beat.drumPattern.hitCount() == 2
            && beat.drumPattern.steps[0][0] == 120 && beat.drumPattern.steps[1][60] == 127 && beat.drumPattern.valid(),
            "drum hits not sanitized");
    auto cleared = ai::parseAssistantResponse(wrap("{\"reply\":\"Cleared.\",\"change\":true,\"hits\":[]}"), true);
    require(cleared.ok() && cleared.changed && cleared.count() == 0, "clearing a beat rejected");
    require(!ai::parseAssistantResponse(wrap("{\"reply\":\"x\",\"change\":true,\"hits\":[{\"pad\":42,\"step\":0,\"velocity\":1}]}"), true).ok(),
            "all-invalid hits accepted");
    require(!ai::parseAssistantResponse(wrap("{\"reply\":\"x\",\"change\":true}"), false).ok(), "change without notes accepted");
    require(!ai::parseAssistantResponse("{\"is_error\":true,\"result\":\"Not logged in\"}", true).ok(), "CLI error hidden");

    // End to end with a fake CLI that checks the sandbox and the drum schema.
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonora-assist", "");
    require(dir.createDirectory(), "assistant test dir failed");
    const auto fake = dir.getChildFile("claude");
    const auto answer = wrap("{\"reply\":\"Here is a beat.\",\"change\":true,\"hits\":[{\"pad\":0,\"step\":0,\"velocity\":110},"
                             "{\"pad\":1,\"step\":4,\"velocity\":100}]}");
    require(fake.replaceWithText("#!/bin/sh\n"
        "case \"$*\" in *--strict-mcp-config*hits*) ;; *) echo '{\"is_error\":true,\"result\":\"bad args\"}'; exit 0;; esac\n"
        "[ -z \"$ANTHROPIC_API_KEY\" ] || { echo '{\"is_error\":true,\"result\":\"key leaked\"}'; exit 0; }\n"
        "grep -q 'snare fill' || { echo '{\"is_error\":true,\"result\":\"no message\"}'; exit 0; }\n"
        "echo '" + answer + "'\n", false, false, "\n") && fake.setExecutePermission(true), "fake CLI write failed");
    setenv("SONORA_CLAUDE_PATH", fake.getFullPathName().toRawUTF8(), 1);
    setenv("ANTHROPIC_API_KEY", "sk-should-not-leak", 1);
    request.part = 2;
    request.message = "Give it a snare fill";
    const auto result = ai::runAssistant(request, nullptr, 10000);
    unsetenv("SONORA_CLAUDE_PATH");
    unsetenv("ANTHROPIC_API_KEY");
    require(result.ok() && result.changed && result.drums && result.drumPattern.hitCount() == 2,
            ("assistant end-to-end failed: " + result.error).toRawUTF8());
    require(dir.deleteRecursively(), "assistant test cleanup failed");

    // Opt-in live check through the real, signed-in CLI.
    if (juce::SystemStats::getEnvironmentVariable("SONORA_LIVE_CLAUDE", {}) == "1")
    {
        request.history.clear();
        request.message = "Write a punchy beat for this chorus that locks with the bass.";
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto live = ai::runAssistant(request, nullptr);
        std::cout << "LIVE assistant (drums): " << (live.ok() ? "ok" : live.error) << " in "
                  << juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) << " s, changed="
                  << live.changed << ", " << live.count() << " hits\n  reply: " << live.reply << "\n";
        require(live.ok() && live.changed && live.count() >= 4, "live assistant beat failed");
    }
}

void testSongComposer()
{
    namespace ai = sonora::ai;
    auto project = fixture(); // track 0 synth (loop A), track 1 drums (loop A)
    project.tracks[2].id = 3;
    project.tracks[2].kind = sonora::TrackKind::Synth;
    project.tracks[2].setTrackName("Bass");
    project.tracks[2].melodies[0].count = 1;
    project.tracks[2].melodies[0].notes[0] = { 1, 0, 960, 48, 100 };
    project.song = sonora::buildSongFromTemplate(project, sonora::SongTemplate::Simple);

    ai::SongRequest request;
    request.project = project;
    request.message = "Compose the full song with variation";
    request.history = { { true, "earlier idea" }, { false, "earlier reply" } };
    const auto message = ai::buildSongMessage(request);
    require(message.contains("Track 0 \"Sine Keys\"") && message.contains("Track 2 \"Bass\"")
            && message.contains("Track 1 \"Starter Drums\""), "tracks missing from song context");
    require(message.contains("Loop B [empty]: (empty, free for a new loop)") && message.contains("pitch=48 (C3)")
            && message.contains("Kick on sixteenth steps: 0,16,32,48"), "loop contents missing");
    require(message.contains("Current arrangement (6 sections)") && message.contains("1. Intro: track 0 loop A, track 2 loop A")
            && message.contains("2. Verse: track 0 loop A, track 1 loop A, track 2 loop A"), "current arrangement missing");
    require(message.contains("Producer: earlier idea") && message.contains("Compose the full song"), "song chat missing");
    const auto args = ai::songArguments();
    require(args.indexOf("--tools") >= 0 && args[args.indexOf("--tools") + 1].isEmpty()
            && args.contains("--strict-mcp-config") && args.contains("--no-session-persistence"), "composer not sandboxed");
    require(juce::JSON::parse(ai::songSchema()).getDynamicObject() != nullptr, "song schema is not JSON");
    require(ai::songSystemPrompt().contains("EMPTY loop slots only"), "empty-slot rule missing from prompt");

    auto wrap = [](const juce::String& body) {
        return "{\"is_error\":false,\"result\":\"\",\"structured_output\":" + body + "}";
    };
    const auto answer = wrap(R"({"reply":"Built a full song.","change":true,
        "sections":[
          {"part":"Intro","tracks":[{"track":0,"loop":"A"}]},
          {"part":"verse","tracks":[{"track":0,"loop":"A"},{"track":1,"loop":"A"},{"track":2,"loop":"A"}]},
          {"part":"Build","tracks":[{"track":1,"loop":"B"},{"track":2,"loop":"A"}]},
          {"part":"Chorus","tracks":[{"track":0,"loop":"B"},{"track":1,"loop":"A"},{"track":2,"loop":"C"},{"track":5,"loop":"A"},{"track":0,"loop":"Z"}]},
          {"part":"Space Jam","tracks":[]}],
        "newLoops":[
          {"track":1,"loop":"B","hits":[{"pad":1,"step":60,"velocity":110},{"pad":1,"step":62,"velocity":90}]},
          {"track":0,"loop":"B","notes":[{"start":0,"duration":960,"pitch":72,"velocity":100}]},
          {"track":0,"loop":"A","notes":[{"start":0,"duration":960,"pitch":64,"velocity":100}]},
          {"track":2,"loop":"D","notes":[]},
          {"track":7,"loop":"B","notes":[{"start":0,"duration":960,"pitch":60,"velocity":100}]}]})");
    const auto result = ai::parseSongResponse(answer, project);
    require(result.ok() && result.changed && result.song.sections == 5, "song arrangement not parsed");
    using P = sonora::SongPart;
    require(result.song.parts[0] == P::Intro && result.song.parts[1] == P::Verse && result.song.parts[2] == P::Build
            && result.song.parts[3] == P::Chorus && result.song.parts[4] == P::Section, "part names not mapped");
    require(result.writes.size() == 2, "wrong number of new loops (existing/empty/unknown targets must be refused)");
    require(result.skipped.size() == 1 && result.skipped[0].contains("Sine Keys loop A"), "overwrite refusal not reported");
    require(result.song.trackOn[0][0] && !result.song.trackOn[0][1] && !result.song.trackOn[0][2], "intro gating wrong");
    require(result.song.trackOn[2][1] && result.song.slots[2][1] == 1, "build should use the new drum loop B");
    require(result.song.trackOn[3][0] && result.song.slots[3][0] == 1, "chorus should use the new lead loop B");
    require(!result.song.trackOn[3][2], "cell pointing at an empty loop should be off");
    require(result.song.valid(), "composed arrangement invalid");

    auto applied = project;
    require(ai::applySongResult(applied, result), "apply failed");
    require(applied.tracks[1].drumPatterns[1].steps[1][60] == 110 && applied.tracks[0].melodies[1].count == 1
            && applied.tracks[0].melodies[1].notes[0].pitch == 72, "new loops not written");
    require(applied.tracks[0].melodies[0] == project.tracks[0].melodies[0], "existing loop A was overwritten");
    require(applied.song == result.song && applied.valid(), "arrangement not applied");
    auto busy = project;
    busy.tracks[1].drumPatterns[1].steps[0][0] = 100; // producer filled the slot meanwhile
    ai::applySongResult(busy, result);
    require(busy.tracks[1].drumPatterns[1].hitCount() == 1, "apply overwrote a slot filled meanwhile");

    auto question = ai::parseSongResponse(wrap(R"({"reply":"It's 6 sections now.","change":false,"sections":[],"newLoops":[]})"), project);
    require(question.ok() && !question.changed, "question turn wrong");
    auto untouched = project;
    require(!ai::applySongResult(untouched, question) && untouched == project, "no-change result altered project");
    require(!ai::parseSongResponse(wrap(R"({"reply":"x","change":true,"sections":[],"newLoops":[]})"), project).ok(),
            "empty arrangement accepted");

    // End to end with a fake CLI.
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonora-song", "");
    require(dir.createDirectory(), "song test dir failed");
    const auto fake = dir.getChildFile("claude");
    require(fake.replaceWithText("#!/bin/sh\n"
        "case \"$*\" in *--strict-mcp-config*newLoops*) ;; *) echo '{\"is_error\":true,\"result\":\"bad args\"}'; exit 0;; esac\n"
        "grep -q 'Current arrangement' || { echo '{\"is_error\":true,\"result\":\"no context\"}'; exit 0; }\n"
        "echo '" + answer.replace("\n", " ") + "'\n", false, false, "\n") && fake.setExecutePermission(true),
            "fake CLI write failed");
    setenv("SONORA_CLAUDE_PATH", fake.getFullPathName().toRawUTF8(), 1);
    const auto run = ai::runSongComposer(request, nullptr, 10000);
    unsetenv("SONORA_CLAUDE_PATH");
    require(run.ok() && run.song.sections == 5 && run.writes.size() == 2, ("composer end-to-end failed: " + run.error).toRawUTF8());
    require(dir.deleteRecursively(), "song test cleanup failed");

    if (juce::SystemStats::getEnvironmentVariable("SONORA_LIVE_CLAUDE", {}) == "1")
    {
        request.history.clear();
        request.message = "Compose the full song with variation: write a drum fill loop and a lifted final-chorus melody.";
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto live = ai::runSongComposer(request, nullptr);
        std::cout << "LIVE composer: " << (live.ok() ? "ok" : live.error) << " in "
                  << juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) << " s, "
                  << live.song.sections << " sections, " << live.writes.size() << " new loops\n  reply: " << live.reply << "\n  ";
        for (int s = 0; s < live.song.sections; ++s)
            std::cout << sonora::songPartName(live.song.parts[static_cast<std::size_t>(s)]) << (s + 1 < live.song.sections ? " > " : "\n");
        for (const auto& write : live.writes)
            std::cout << "  new: track " << write.track << " loop " << static_cast<char>('A' + write.slot) << " ("
                      << (write.drums ? write.drumPattern.hitCount() : write.pattern.count) << (write.drums ? " hits)\n" : " notes)\n");
        auto check = project;
        require(live.ok() && live.changed && ai::applySongResult(check, live) && check.valid(), "live composition failed");
    }
}

void testIdeaCapture()
{
    using sonora::IdeaEvent;
    // Leading silence is removed, rhythm is quantized, and held keys acquire
    // actual note lengths instead of being copied as raw transport audio.
    const std::vector<IdeaEvent> keys {
        { 1.0, 60, 108, 1, true }, { 1.5, 60, 0, 1, false },
        { 1.5, 64, 90, 1, true }, { 2.0, 64, 0, 1, false },
        { 2.0, 67, 95, 1, true }, { 2.5, 67, 0, 1, false }
    };
    const auto melody = sonora::interpretIdea(keys, sonora::TrackKind::Synth, 120.0, 3.0);
    require(melody.melody.valid() && melody.melody.count == 3, "idea lost played keys");
    require(melody.melody.notes[0].start == 0 && melody.melody.notes[0].duration == 960
            && melody.melody.notes[1].start == 960 && melody.melody.notes[2].start == 1920,
            "idea timing/leading-silence wrong");
    const auto octave = sonora::interpretIdea({ { 0, 84, 100, 1, true }, { 0.5, 84, 0, 1, false } },
                                              sonora::TrackKind::Synth, 120, 1);
    require(octave.melody.count == 1 && octave.melody.notes[0].pitch == 84, "idea pitch not preserved");
    std::vector<IdeaEvent> longIdea { { 0, 60, 100, 1, true }, { 0.5, 60, 0, 1, false },
                                      { 18, 67, 100, 1, true }, { 18.5, 67, 0, 1, false } };
    const auto fitted = sonora::interpretIdea(longIdea, sonora::TrackKind::Synth, 120, 20);
    require(fitted.melody.valid() && fitted.melody.count == 2
            && fitted.melody.notes[1].start < sonora::patternTicks,
            "long idea did not fit into four bars");
    const auto drums = sonora::interpretIdea({ { 0, 36, 110, 10, true }, { 0.25, 37, 100, 10, true },
                                               { 0.25, 37, 70, 10, true }, { 0.5, 46, 90, 10, true },
                                               { 0.75, 88, 90, 10, true } },
                                             sonora::TrackKind::Drums, 120, 1.5);
    require(drums.drums.valid() && drums.drums.hitCount() == 3
            && drums.drums.steps[0][0] == 110 && drums.drums.steps[1][2] == 100,
            "pad idea lost hits or duplicate velocity");
    require(sonora::interpretIdea({}, sonora::TrackKind::Synth, 120, 1).melody.count == 0,
            "empty idea manufactured notes");
    // Local mic fallback: a steady sung A4 becomes a note without needing a
    // network call, or creating a recorded take in the project.
    std::vector<float> hum(48000);
    for (int i = 0; i < 48000; ++i)
        hum[static_cast<std::size_t>(i)] = 0.4f * std::sin(juce::MathConstants<double>::twoPi * 440 * i / 48000.0);
    const auto vocal = sonora::interpretAudioIdea(hum.data(), static_cast<int>(hum.size()), 48000, 120);
    require(vocal.fromAudio && vocal.melody.valid() && vocal.melody.count > 0
            && vocal.melody.notes[0].pitch == 69, "hummed idea not detected");
    std::fill(hum.begin(), hum.end(), 0.0f);
    require(sonora::interpretAudioIdea(hum.data(), static_cast<int>(hum.size()), 48000, 120).melody.count == 0,
            "silence became an idea");
}

void testAiDictation()
{
    // Voxtype prints progress lines, then the final transcript alone.
    require(sonora::ai::parseVoxtypeOutput("Loading audio file: \"/tmp/x.wav\"\nAudio format: 16000 Hz\nProcessing 16000 samples (1.00s)...\n\nAdd a snare fill\n")
                == "Add a snare fill", "dictation transcript not extracted");
    require(sonora::ai::parseVoxtypeOutput("Loading audio file: \"/tmp/x.wav\"\nAudio format: 16000 Hz\n").isEmpty(),
            "progress-only output accepted as dictation");
    require(sonora::ai::parseVoxtypeOutput("").isEmpty(), "empty dictation accepted");
    // A missing WAV can never become text.
    const auto missing = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonora-missing", ".wav");
    std::atomic<bool> cancel { false };
    const auto result = sonora::ai::transcribeDictation(missing, &cancel);
    require(!result.ok() && result.error.isNotEmpty(), "missing dictation WAV accepted");
}

int main()
{
    try
    {
        testTiming(); std::cout << "PASS sample-accurate looping, boundaries, chase, tempo\n";
        testGroove(); std::cout << "PASS swing timing, quantize, humanize, engine groove\n";
        testKeyTools(); std::cout << "PASS scales, snap, chords, velocity ramp\n";
        testLiveFx(); std::cout << "PASS live arp and chord FX\n";
                testFx(); std::cout << "PASS fx transparency, EQ/comp/delay/verb/limiter behavior, validation\n";
        testFxPersistence(); std::cout << "PASS v4 fx round-trip, v3 migration, malformed fx, effected export\n";
        testTakePersistence(); std::cout << "PASS v5 take round-trip, v4 migration, malformed takes, latency math\n";
        testRecorder(); std::cout << "PASS FIFO recorder write/read integrity, overruns, validation\n";
        testTakePlayback(); std::cout << "PASS song-mode take offset/level, mute, loop-mode silence\n";
        testTakeSolo(); std::cout << "PASS take solo comping\n";
        testMixer(); std::cout << "PASS mixer pan, sends, returns\n";
        testAutomation(); std::cout << "PASS automation curves and engine lanes\n";
        testTimeStretch(); std::cout << "PASS rubber-band time stretch\n";
        testChordTrack(); std::cout << "PASS chord track, follow transpose, AI chords\n";
        testVariations(); std::cout << "PASS pattern library round-trip, v5 migration, malformed slots\n";
        testInstances(); std::cout << "PASS make-unique detach, full-library refusal, sharing queries\n";
        testOmarchyTheme(); std::cout << "PASS theme parse, palette map, fallbacks, live load\n";
        testAcceptance(); std::cout << "PASS reference song save/load, loop+s song bounce, vocal in mix\n";
        testKitPanelLogic(); std::cout << "PASS kit panel helpers\n";
        testKitSamples(); std::cout << "PASS sample import, resample, normalize, fallback, engine bank swap\n";
        testKitPersistence(); std::cout << "PASS v7 kit round-trip, v6 migration, malformed sample lists\n";
        testKitVariants(); std::cout << "PASS factory kit variants, persistence, validation\n";
        testKitPresets(); std::cout << "PASS preset save/load/delete/browse, sanitize, corrupt rejection\n";
        testPitch(); std::cout << "PASS YIN accuracy, scale snap, correction, identity, vibrato, validation\n";
        testMidiHardware(); std::cout << "PASS Arturia pad map, MCU transport, auto-connect, status text\n";
        testBackgroundLoading(); std::cout << "PASS async MiniLab sender, take/bank load cancel\n";
        testExpression(); std::cout << "PASS pitch bend, mod vibrato, sustain hold/release\n";
        testPersistence(); std::cout << "PASS project round-trip, validation, atomic replacement\n";
        testDrumPersistence(); std::cout << "PASS version-1 migration, drum/mixer persistence, malformed tracks\n";
        testArrangement(); std::cout << "PASS section gating, song end, v2/v3 persistence, engine stop\n";
        testQueue(); std::cout << "PASS bounded snapshot queue and concurrent transfer\n";
        testAudio(); std::cout << "PASS audible offline render, callback invariance, stop cleanup\n";
        testDrumAudio(); std::cout << "PASS sampled drums, hat choke, resampling, mute/solo, pad audition\n";
        testExport(); std::cout << "PASS loop/song bounce, live parity, normalize, cancel, WAV round-trip\n";
        testInstruments(); std::cout << "PASS sampled instruments, panic, FX isolation, export, v10 presets\n";
        testSynthEngine(); std::cout << "PASS synth patches, waves, filter, envelopes, chorus, live edits, v11\n";
        testSynthOscillators(); std::cout << "PASS pulse and noise oscillators\n";
        testAgentActions(); std::cout << "PASS agent: parameters, parsing, tracks, patterns, song, automation, effects\n";
        testAgentPipeline(); std::cout << "PASS agent: schema/prompt/executor agree, context, parsing, end-to-end\n";
        testInstrumentSearch(); std::cout << "PASS instrument picker: search ranking, favorites, persistence\n";
        testSampler(); std::cout << "PASS sampler: loader, pitch, loop, one-shot, trim, library, v22, export\n";
        testKnobs(); std::cout << "PASS MiniLab knob maps, CC sets, drive/chorus DSP, v12 fx\n";
        testAiMelody(); std::cout << "PASS AI melody sandbox, context, parsing, sanitizing, fake CLI\n";
        testMiniLabDisplay(); std::cout << "PASS MiniLab 3 screen/pad SysEx, replies, screen text\n";
        testSongComposition(); std::cout << "PASS song parts, section edits, templates, v13, play-from-part, part preview\n";
        testAiAssistant(); std::cout << "PASS AI assistant context, history, drum/melody edits, sandbox, fake CLI\n";
        testSongComposer(); std::cout << "PASS song composer context, empty-slot rule, arrangement, apply, fake CLI\n";
        testIdeaCapture(); std::cout << "PASS idea capture MIDI pads/keys, four-bar timing, humming, silence\n";
        testAiDictation(); std::cout << "PASS local dictation transcript parsing, missing WAV rejection\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
