#include "AudioEngine.h"
#include "AudioTakes.h"
#include "Export.h"
#include "Fx.h"
#include "KitSamples.h"
#include "KnobMaps.h"
#include "LiveFx.h"
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
            sonora::AudioEngine engine;
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
    require(sonora::ProjectIO::decode(json.replace("\"version\": 17", "\"version\": 18"), loaded).failed(),
            "unknown version accepted");
    require(sonora::ProjectIO::decode(json.replace("\"version\": 17", "\"version\": 4294967297"), loaded).failed(),
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
        sonora::AudioEngine engine;
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

    sonora::AudioEngine engine;
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
    sonora::AudioEngine engine;
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
    require(json.contains("\"version\": 17"), "projects must save as v17");
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
    require(json.contains("\"version\": 17"), "take projects must save as v17");
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
    sonora::AudioEngine engine;
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
        sonora::AudioEngine engine;
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
        sonora::AudioEngine engine;
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
        sonora::AudioEngine engine;
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
    require(json.contains("\"version\": 17"), "variation projects must save as v17");
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
    sonora::AudioEngine engine;
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
    require(json.contains("\"version\": 17"), "kit projects must save as v17");
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
    sonora::AudioEngine probe;
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
        sonora::AudioEngine engine;
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
        auto variant = project;
        variant.tracks[0].instrumentPreset = preset;
        const auto audio = render(variant, 40);
        const auto level = peak(audio);
        require(level < 1.0f, "sampled instrument clips");
        if (level > 0.01f)
            ++audibleCount;
        require(audio != sine, "sampled preset rendered as the sine voice");
    }
    require(audibleCount == static_cast<int>(sonora::instruments.size()) - 1, "a sampled preset is silent");

    // Every sampled preset plays the full piano roll: SoundFont key ranges
    // are octave-transposed into range instead of going silent.
    for (int preset = 1; preset < static_cast<int>(sonora::instruments.size()); ++preset)
        for (int pitch : { 0, 12, 21, 36, 60, 84, 96, 108, 120, 127 })
        {
            auto variant = project;
            variant.tracks[0].instrumentPreset = preset;
            variant.tracks[0].melodies[0].notes[0].pitch = pitch;
            const auto level = peak(render(variant, 40));
            // No digital silence anywhere on the roll. (Extremes on some
            // instruments are quiet by nature of the samples, like the
            // real thing — e.g. a fingered bass at the top MIDI octave.)
            require(level > 0.001f, ("sampled preset silent out of range: preset "
                + juce::String(preset) + " pitch " + juce::String(pitch)).toRawUTF8());
        }

    // Panic cuts sampled voices at once; no release tail in the next block.
    {
        auto piano = project;
        piano.tracks[0].instrumentPreset = 1;
        sonora::AudioEngine engine;
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
    require(json.contains("\"version\": 17"), "instrument projects must save as v17");
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
    invalid.tracks[0].instrumentPreset = 99;
    require(!invalid.valid(), "out-of-range preset validated");
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
        sonora::AudioEngine engine;
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
    require(json.contains("\"version\": 17"), "synth projects must save as v17");
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
    reject("wave", 4);
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
        sonora::AudioEngine engine;
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
    require(json.contains("\"version\": 17"), "fx projects must save as v17");
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
    require(json.contains("\"version\": 17"), "song projects must save as v17");
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
    require(sonora::ProjectIO::decode(juce::JSON::toString(missingSwing), loaded).failed(), "v17 without swing accepted");
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
    require(sonora::ProjectIO::decode(juce::JSON::toString(missingKey), loaded).failed(), "v17 without key accepted");
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
            "v17 without arp rate accepted");
    auto invalidLive = lively;
    invalidLive.tracks[0].liveFx.octaves = 4;
    require(!invalidLive.valid(), "arp octave validated");

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
        sonora::AudioEngine engine;
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
