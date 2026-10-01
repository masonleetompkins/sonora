#pragma once
#include "Instruments.h"
#include "SamplerParams.h"
#include "SynthParams.h"
#include "AudioTakes.h"
#include "Fx.h"
#include "Timing.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace sonora
{
inline constexpr int lowestPitch = 0;
inline constexpr int highestPitch = 127;
inline constexpr int drumPads = 8;
inline constexpr int drumBaseNote = 36;
inline constexpr std::uint32_t melodyTrackId = 1, drumTrackId = 2;
inline constexpr std::array<const char*, drumPads> drumNames {
    "Kick", "Snare", "Closed hat", "Open hat", "Clap", "Low tom", "Rim", "Shaker"
};
inline constexpr std::array<int, drumPads> drumMidiNotes { 36, 38, 42, 46, 39, 45, 37, 82 };

struct Note
{
    std::uint32_t id = 0;
    int start = 0;
    int duration = stepTicks;
    int pitch = 60;
    int velocity = 100;
    bool operator==(const Note&) const = default;
};

struct Pattern
{
    static constexpr int capacity = 256;
    std::array<Note, capacity> notes {};
    int count = 0;

    bool valid() const
    {
        if (count < 0 || count > capacity)
            return false;
        for (int i = 0; i < count; ++i)
        {
            const auto& n = notes[static_cast<std::size_t>(i)];
            if (n.id == 0 || n.id > 2147483647u || n.start < 0 || n.start >= patternTicks || n.duration <= 0
                || n.duration > patternTicks - n.start || n.pitch < lowestPitch
                || n.pitch > highestPitch || n.velocity < 1 || n.velocity > 127)
                return false;
            for (int j = 0; j < i; ++j)
            {
                const auto& other = notes[static_cast<std::size_t>(j)];
                if (n.id == other.id || (n.pitch == other.pitch && n.start < other.start + other.duration
                                         && other.start < n.start + n.duration))
                    return false;
            }
        }
        return true;
    }

    int noteAt(int pitch, int tick) const
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& n = notes[static_cast<std::size_t>(i)];
            if (n.pitch == pitch && tick >= n.start && tick < n.start + n.duration)
                return i;
        }
        return -1;
    }

    void erase(int index)
    {
        if (index < 0 || index >= count)
            return;
        for (int i = index; i < count - 1; ++i)
            notes[static_cast<std::size_t>(i)] = notes[static_cast<std::size_t>(i + 1)];
        notes[static_cast<std::size_t>(--count)] = {};
    }

    bool operator==(const Pattern& other) const
    {
        return count == other.count && std::equal(notes.begin(), notes.begin() + count, other.notes.begin());
    }
};

struct DrumPattern
{
    // Zero is an empty step; 1-127 is its velocity.
    std::array<std::array<std::uint8_t, gridSteps>, drumPads> steps {};
    bool valid() const
    {
        for (const auto& row : steps)
            for (const auto velocity : row)
                if (velocity > 127)
                    return false;
        return true;
    }
    int hitCount() const
    {
        int count = 0;
        for (const auto& row : steps)
            for (const auto velocity : row)
                count += velocity > 0 ? 1 : 0;
        return count;
    }
    bool operator==(const DrumPattern&) const = default;
};

// Groove toolbox. Swing is non-destructive (a per-track playback offset
// applied at schedule time); quantize and humanize are destructive edits.
// Swing delays odd 16th steps by a fraction of one step; 0.1-0.3 is the
// classic pocket, 0 is straight.
inline constexpr float maxSwing = 0.75f;

inline int swingTicks(int tick, float swing)
{
    if (swing <= 0.0f)
        return tick;
    if ((tick / stepTicks) % 2 == 1)
        return tick + static_cast<int>(std::round(swing * stepTicks));
    return tick;
}

// Sort, trim same-pitch overlaps, cap, and reassign ids so a transformed
// pattern always stays valid (undo restores the original anyway).
inline void packNotes(Pattern& pattern, std::vector<Note> notes)
{
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return std::tie(a.start, a.pitch) < std::tie(b.start, b.pitch);
    });
    Pattern packed;
    for (auto note : notes)
    {
        note.duration = std::clamp(note.duration, 1, patternTicks - note.start);
        bool duplicate = false;
        for (int i = 0; i < packed.count; ++i)
        {
            auto& previous = packed.notes[static_cast<std::size_t>(i)];
            if (previous.pitch != note.pitch || previous.start + previous.duration <= note.start)
                continue;
            if (previous.start == note.start)
                duplicate = true;
            else
                previous.duration = note.start - previous.start;
        }
        if (duplicate || packed.count >= Pattern::capacity)
            continue;
        note.id = static_cast<std::uint32_t>(packed.count + 1);
        packed.notes[static_cast<std::size_t>(packed.count++)] = note;
    }
    pattern = packed;
}

// Snap note starts toward the 16th grid. Strength 1 is fully quantized.
inline void quantizePattern(Pattern& pattern, float strength)
{
    strength = std::clamp(strength, 0.0f, 1.0f);
    if (strength <= 0.0f || pattern.count == 0)
        return;
    std::vector<Note> notes(pattern.notes.begin(), pattern.notes.begin() + pattern.count);
    for (auto& note : notes)
    {
        const int snapped = static_cast<int>(std::round(static_cast<double>(note.start) / stepTicks)) * stepTicks;
        note.start = std::clamp(static_cast<int>(std::round(note.start + (snapped - note.start) * strength)),
                                0, patternTicks - 1);
    }
    packNotes(pattern, std::move(notes));
}

// Loosen timing (±timing * 60 ticks) and velocity (±velocity * 20).
// Seeded so results are reproducible (the UI picks a fresh seed per take).
inline void humanizePattern(Pattern& pattern, float timing, float velocity, std::uint32_t seed)
{
    timing = std::clamp(timing, 0.0f, 1.0f);
    velocity = std::clamp(velocity, 0.0f, 1.0f);
    if ((timing <= 0.0f && velocity <= 0.0f) || pattern.count == 0)
        return;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    std::vector<Note> notes(pattern.notes.begin(), pattern.notes.begin() + pattern.count);
    for (auto& note : notes)
    {
        note.start = std::clamp(static_cast<int>(std::round(note.start + jitter(rng) * timing * 60.0f)),
                                0, patternTicks - 1);
        note.velocity = std::clamp(static_cast<int>(std::round(note.velocity + jitter(rng) * velocity * 20.0f)),
                                   1, 127);
    }
    packNotes(pattern, std::move(notes));
}

// Drums live on the grid, so only velocities wander.
inline void humanizeDrums(DrumPattern& pattern, float velocity, std::uint32_t seed)
{
    velocity = std::clamp(velocity, 0.0f, 1.0f);
    if (velocity <= 0.0f)
        return;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    for (auto& row : pattern.steps)
        for (auto& cell : row)
            if (cell > 0)
                cell = static_cast<std::uint8_t>(std::clamp(
                    static_cast<int>(std::round(cell + jitter(rng) * velocity * 20.0f)), 1, 127));
}

// Musical key tools: per-project key/scale, scale membership, snapping,
// chord voicings, and velocity ramps. Pitch classes are 0-11 (C=0).
enum class MusicScale : std::uint8_t
{
    Major = 0, NaturalMinor, HarmonicMinor, Dorian, MajorPentatonic, MinorPentatonic, Blues, numScales
};

inline const char* scaleName(MusicScale scale)
{
    switch (scale)
    {
        case MusicScale::Major: return "Major";
        case MusicScale::NaturalMinor: return "Natural minor";
        case MusicScale::HarmonicMinor: return "Harmonic minor";
        case MusicScale::Dorian: return "Dorian";
        case MusicScale::MajorPentatonic: return "Major pentatonic";
        case MusicScale::MinorPentatonic: return "Minor pentatonic";
        case MusicScale::Blues: return "Blues";
        case MusicScale::numScales: break;
    }
    return "Major";
}

inline const char* keyName(int key)
{
    static constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[std::clamp(key, 0, 11)];
}

inline std::array<bool, 12> scalePitchClasses(MusicScale scale)
{
    switch (scale)
    {
        case MusicScale::Major: return { { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 } };
        case MusicScale::NaturalMinor: return { { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 } };
        case MusicScale::HarmonicMinor: return { { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1 } };
        case MusicScale::Dorian: return { { 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0 } };
        case MusicScale::MajorPentatonic: return { { 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0 } };
        case MusicScale::MinorPentatonic: return { { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0 } };
        case MusicScale::Blues: return { { 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0 } };
        case MusicScale::numScales: break;
    }
    return { { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 } };
}

inline bool pitchInScale(int pitch, int key, MusicScale scale)
{
    const int pc = ((pitch - key) % 12 + 12) % 12;
    return scalePitchClasses(scale)[static_cast<std::size_t>(pc)];
}

// Nearest in-scale pitch (ties resolve upward), clamped to MIDI range.
inline int snapPitchToScale(int pitch, int key, MusicScale scale)
{
    pitch = std::clamp(pitch, lowestPitch, highestPitch);
    for (int radius = 0; radius <= 6; ++radius)
    {
        if (pitch + radius <= highestPitch && pitchInScale(pitch + radius, key, scale))
            return pitch + radius;
        if (pitch - radius >= lowestPitch && pitchInScale(pitch - radius, key, scale))
            return pitch - radius;
    }
    return pitch;
}

enum class ChordType : std::uint8_t
{
    Major = 0, Minor, Dom7, Maj7, Min7, Sus4, Dim, Aug, numChords
};

inline const char* chordName(ChordType chord)
{
    switch (chord)
    {
        case ChordType::Major: return "Major";
        case ChordType::Minor: return "Minor";
        case ChordType::Dom7: return "7";
        case ChordType::Maj7: return "Maj7";
        case ChordType::Min7: return "Min7";
        case ChordType::Sus4: return "Sus4";
        case ChordType::Dim: return "Dim";
        case ChordType::Aug: return "Aug";
        case ChordType::numChords: break;
    }
    return "Major";
}

// Semitone offsets from the root for one-finger chord stamps.
inline std::vector<int> chordPitches(int root, ChordType chord)
{
    const int* offsets = nullptr;
    int count = 0;
    static constexpr int major[] { 0, 4, 7 }, minor[] { 0, 3, 7 }, dom7[] { 0, 4, 7, 10 },
        maj7[] { 0, 4, 7, 11 }, min7[] { 0, 3, 7, 10 }, sus4[] { 0, 5, 7 }, dim[] { 0, 3, 6 }, aug[] { 0, 4, 8 };
    switch (chord)
    {
        case ChordType::Major: offsets = major; count = 3; break;
        case ChordType::Minor: offsets = minor; count = 3; break;
        case ChordType::Dom7: offsets = dom7; count = 4; break;
        case ChordType::Maj7: offsets = maj7; count = 4; break;
        case ChordType::Min7: offsets = min7; count = 4; break;
        case ChordType::Sus4: offsets = sus4; count = 3; break;
        case ChordType::Dim: offsets = dim; count = 3; break;
        case ChordType::Aug: offsets = aug; count = 3; break;
        case ChordType::numChords: break;
    }
    std::vector<int> pitches;
    for (int i = 0; i < count; ++i)
        if (root + offsets[i] >= lowestPitch && root + offsets[i] <= highestPitch)
            pitches.push_back(root + offsets[i]);
    return pitches;
}

// Linear velocity ramp across notes ordered by start (first gets startVel,
// last gets endVel). Note order in the pattern is preserved.
inline void applyVelocityRamp(Pattern& pattern, int startVel, int endVel)
{
    if (pattern.count == 0)
        return;
    startVel = std::clamp(startVel, 1, 127);
    endVel = std::clamp(endVel, 1, 127);
    std::vector<int> order(static_cast<std::size_t>(pattern.count));
    for (int i = 0; i < pattern.count; ++i)
        order[static_cast<std::size_t>(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return pattern.notes[static_cast<std::size_t>(a)].start < pattern.notes[static_cast<std::size_t>(b)].start;
    });
    for (std::size_t k = 0; k < order.size(); ++k)
    {
        const double t = order.size() == 1 ? 0.0 : static_cast<double>(k) / (order.size() - 1);
        pattern.notes[static_cast<std::size_t>(order[k])].velocity =
            std::clamp(static_cast<int>(std::round(startVel + (endVel - startVel) * t)), 1, 127);
    }
}

// Live MIDI FX for MiniLab playing: arpeggiator + one-finger chords on the
// synth target's live notes. Processed engine-side; see LiveFx.h.
enum class ArpMode : std::uint8_t
{
    Off = 0, Up, Down, UpDown, Random, numModes
};

inline const char* arpModeName(ArpMode mode)
{
    switch (mode)
    {
        case ArpMode::Off: return "Off";
        case ArpMode::Up: return "Up";
        case ArpMode::Down: return "Down";
        case ArpMode::UpDown: return "Up-down";
        case ArpMode::Random: return "Random";
        case ArpMode::numModes: break;
    }
    return "Off";
}

enum class ArpRate : std::uint8_t
{
    Eighth = 0, Sixteenth, EighthTriplet, SixteenthTriplet, numRates
};

inline const char* arpRateName(ArpRate rate)
{
    switch (rate)
    {
        case ArpRate::Eighth: return "1/8";
        case ArpRate::Sixteenth: return "1/16";
        case ArpRate::EighthTriplet: return "1/8T";
        case ArpRate::SixteenthTriplet: return "1/16T";
        case ArpRate::numRates: break;
    }
    return "1/16";
}

inline double arpStepBeats(ArpRate rate)
{
    switch (rate)
    {
        case ArpRate::Eighth: return 0.5;
        case ArpRate::Sixteenth: return 0.25;
        case ArpRate::EighthTriplet: return 1.0 / 3.0;
        case ArpRate::SixteenthTriplet: return 1.0 / 6.0;
        case ArpRate::numRates: break;
    }
    return 0.25;
}

struct LiveFx
{
    ArpMode arp = ArpMode::Off;
    ArpRate rate = ArpRate::Sixteenth;
    int octaves = 1; // 1-3
    bool latch = false;
    bool chordOn = false;
    ChordType chord = ChordType::Major;
    bool active() const { return arp != ArpMode::Off || chordOn; }
    bool valid() const
    {
        return arp < ArpMode::numModes && rate < ArpRate::numRates && octaves >= 1 && octaves <= 3
            && chord < ChordType::numChords;
    }
    bool operator==(const LiveFx&) const = default;
};

// Automation lanes: per-track, per-loop-slot point curves for mix and key FX
// parameters, evaluated at the loop tick in both loop and song playback.
// Empty lanes defer to the stored mix/FX values. Fixed capacity keeps the
// project trivially copyable for the engine snapshot queue.
enum class AutomationTarget : std::uint8_t
{
    Volume = 0, Pan, SendDelay, SendReverb, Drive, DelayMix, numTargets
};

inline const char* automationTargetName(AutomationTarget target)
{
    switch (target)
    {
        case AutomationTarget::Volume: return "Volume";
        case AutomationTarget::Pan: return "Pan";
        case AutomationTarget::SendDelay: return "Send delay";
        case AutomationTarget::SendReverb: return "Send reverb";
        case AutomationTarget::Drive: return "Drive";
        case AutomationTarget::DelayMix: return "Delay mix";
        case AutomationTarget::numTargets: break;
    }
    return "Volume";
}

inline std::pair<float, float> automationRange(AutomationTarget target)
{
    switch (target)
    {
        case AutomationTarget::Volume: return { 0.0f, 1.5f };
        case AutomationTarget::Pan: return { -1.0f, 1.0f };
        case AutomationTarget::SendDelay:
        case AutomationTarget::SendReverb:
        case AutomationTarget::Drive:
        case AutomationTarget::DelayMix:
        case AutomationTarget::numTargets: break;
    }
    return { 0.0f, 1.0f };
}

inline constexpr int maxAutomationPoints = 32;

struct AutomationPoint
{
    int tick = 0;
    float value = 0.0f;
    bool operator==(const AutomationPoint&) const = default;
};

struct AutomationLane
{
    std::array<AutomationPoint, maxAutomationPoints> points {};
    int count = 0;
    bool valid() const
    {
        if (count < 0 || count > maxAutomationPoints)
            return false;
        for (int i = 0; i < count; ++i)
        {
            const auto& point = points[static_cast<std::size_t>(i)];
            if (point.tick < 0 || point.tick > patternTicks || !std::isfinite(point.value))
                return false;
            if (i > 0 && point.tick < points[static_cast<std::size_t>(i - 1)].tick)
                return false;
        }
        return true;
    }
    // Piecewise-linear at tick, ends held; empty lanes return the base value.
    float eval(int tick, float base) const
    {
        if (count <= 0)
            return base;
        const auto& first = points[0];
        if (tick <= first.tick)
            return first.value;
        for (int i = 1; i < count; ++i)
        {
            const auto& point = points[static_cast<std::size_t>(i)];
            if (tick <= point.tick)
            {
                const auto& previous = points[static_cast<std::size_t>(i - 1)];
                const int span = point.tick - previous.tick;
                if (span <= 0)
                    return point.value;
                const float t = static_cast<float>(tick - previous.tick) / static_cast<float>(span);
                return previous.value + (point.value - previous.value) * t;
            }
        }
        return points[static_cast<std::size_t>(count - 1)].value;
    }
    bool operator==(const AutomationLane&) const = default;
};

struct TrackMix
{
    float volume = 0.8f;
    float pan = 0.0f; // -1 left .. +1 right (balance; center passes through)
    float sendDelay = 0.0f, sendReverb = 0.0f; // 0..1 post-fader sends
    bool mute = false, solo = false;
    bool valid() const
    {
        return std::isfinite(volume) && volume >= 0.0f && volume <= 1.5f
            && std::isfinite(pan) && pan >= -1.0f && pan <= 1.0f
            && std::isfinite(sendDelay) && sendDelay >= 0.0f && sendDelay <= 1.0f
            && std::isfinite(sendReverb) && sendReverb >= 0.0f && sendReverb <= 1.0f;
    }
    bool audible(const TrackMix& other) const { return !mute && (solo || !other.solo); }
    bool operator==(const TrackMix&) const = default;
};

// Shared send/return buses: delay (bus 0) and reverb (bus 1). Buses run fully
// wet; the return levels are the mix control.
struct ProjectSends
{
    DelayParams delay;
    float delayReturn = 0.8f;
    ReverbParams reverb;
    float reverbReturn = 0.8f;
    bool valid() const
    {
        return delay.valid() && reverb.valid() && std::isfinite(delayReturn)
            && delayReturn >= 0.0f && delayReturn <= 1.5f && std::isfinite(reverbReturn)
            && reverbReturn >= 0.0f && reverbReturn <= 1.5f;
    }
    bool operator==(const ProjectSends&) const = default;
};

inline constexpr int maxSections = 16;      // parts per song (4 bars each)
inline constexpr int legacyMaxSections = 8; // files before v13 store 8 rows
inline constexpr int numPatterns = 4;
inline constexpr int sampleFileCapacity = 260;
inline constexpr int numKitVariants = 6;
inline constexpr int maxTracks = 8;
inline constexpr int trackNameCapacity = 64;

enum class TrackKind : std::uint8_t { Synth, Drums, None };

// An instrument track: named, icon-coded by kind, and fully self-contained
// (pattern library, kit, mixer, and FX travel with it). Empty slots carry
// kind None and are skipped by the engine, the arrangement, and the UI.
struct Track
{
    std::uint32_t id = 0;
    char name[trackNameCapacity] {};
    // Icon follows kind for now (0=keys, 1=drums); stored separately so
    // custom icons never need a format change.
    std::uint8_t icon = 0;
    TrackKind kind = TrackKind::None;
    int instrumentPreset = 0;
    SynthParams synth;
    std::array<Pattern, numPatterns> melodies {};
    std::array<DrumPattern, numPatterns> drumPatterns {};
    std::array<std::array<char, sampleFileCapacity>, drumPads> padSamples {};
    int kitVariant = 0;
    TrackMix mix;
    TrackFx fx;
    float swing = 0.0f;
    LiveFx liveFx;
    // Sampler instrument (Instruments.h: samplerInstrument): settings, plus
    // the audio file's name; empty means no sample loaded.
    SamplerParams sampler;
    std::array<char, samplerFileCapacity> samplerFile {};
    // Automation lanes per loop slot, then target.
    std::array<std::array<AutomationLane, static_cast<std::size_t>(AutomationTarget::numTargets)>, numPatterns> automation {};
    bool valid() const
    {
        // Empty slots carry no identity and their content is ignored.
        if (!validInstrument(instrumentPreset) || !synth.valid())
            return false;
        if (kind == TrackKind::None)
            return true;
        if (id == 0 || id > 2147483647u || name[0] == '\0')
            return false;
        if (kind != TrackKind::Synth && kind != TrackKind::Drums)
            return false;
        if (kitVariant < 0 || kitVariant >= numKitVariants)
            return false;
        if (!std::isfinite(swing) || swing < 0.0f || swing > maxSwing)
            return false;
        if (!liveFx.valid() || !sampler.valid())
            return false;
        if (samplerFile[0] != '\0'
            && (samplerFile[samplerFileCapacity - 1] != '\0' || !isSafeSampleName(samplerFile.data())))
            return false;
        for (int slot = 0; slot < numPatterns; ++slot)
            for (int t = 0; t < static_cast<int>(AutomationTarget::numTargets); ++t)
            {
                const auto target = static_cast<AutomationTarget>(t);
                const auto& lane = automation[static_cast<std::size_t>(slot)][static_cast<std::size_t>(t)];
                if (!lane.valid())
                    return false;
                const auto [lo, hi] = automationRange(target);
                for (int i = 0; i < lane.count; ++i)
                {
                    const auto v = lane.points[static_cast<std::size_t>(i)].value;
                    if (v < lo || v > hi)
                        return false;
                }
            }
        for (const auto& pattern : melodies)
            if (!pattern.valid())
                return false;
        for (const auto& drums : drumPatterns)
            if (!drums.valid())
                return false;
        return true;
    }
    bool operator==(const Track&) const = default;
    juce::String samplerFileName() const { return juce::String(samplerFile.data()); }
    void setSamplerFileName(const juce::String& value)
    {
        samplerFile.fill('\0');
        const auto bytes = value.toRawUTF8();
        std::strncpy(samplerFile.data(), bytes, samplerFileCapacity - 1);
    }
    juce::String trackName() const { return juce::String(name); }
    void setTrackName(const juce::String& value)
    {
        const auto trimmed = value.trim();
        const auto bytes = trimmed.toRawUTF8();
        std::strncpy(name, bytes, trackNameCapacity - 1);
        name[trackNameCapacity - 1] = '\0';
    }
};

// What a section is for; drives its label and colour in the song view and
// the pattern/track choices of song templates. Stable persisted ids.
enum class SongPart : std::uint8_t { Section = 0, Intro, Verse, PreChorus, Chorus, Bridge, Break, Build, Drop, Outro, numParts };

inline const char* songPartName(SongPart part)
{
    static constexpr const char* names[] { "Part", "Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge",
                                           "Break", "Build", "Drop", "Outro" };
    const auto index = static_cast<int>(part);
    return index >= 0 && index < static_cast<int>(SongPart::numParts) ? names[index] : "Part";
}

// Global chord track: one chord per song section. Song-mode playback
// transposes synth loops by (root - song key) in chorded sections, and the
// AI composer writes new loops to fit. root -1 means no chord (as written).
struct SectionChord
{
    int root = -1; // -1 = none, else pitch class C=0..B=11
    ChordType type = ChordType::Major;
    bool set() const { return root >= 0; }
    bool valid() const
    {
        return (root == -1 || (root >= 0 && root <= 11)) && type < ChordType::numChords;
    }
    bool operator==(const SectionChord&) const = default;
};

inline juce::String chordLabel(const SectionChord& chord)
{
    if (!chord.set())
        return "-";
    juce::String label(keyName(chord.root));
    switch (chord.type)
    {
        case ChordType::Major: break;
        case ChordType::Minor: label += "m"; break;
        case ChordType::Dom7: label += "7"; break;
        case ChordType::Maj7: label += "maj7"; break;
        case ChordType::Min7: label += "m7"; break;
        case ChordType::Sus4: label += "sus4"; break;
        case ChordType::Dim: label += "dim"; break;
        case ChordType::Aug: label += "aug"; break;
        case ChordType::numChords: break;
    }
    return label;
}

// Semitone shift so the song key center follows the section chord.
inline int chordTranspose(const SectionChord& chord, int songKey)
{
    if (!chord.set())
        return 0;
    return chord.root - std::clamp(songKey, 0, 11);
}

struct Arrangement
{
    int sections = 2;
    std::array<SongPart, maxSections> parts {};
    std::array<SectionChord, maxSections> chords {};
    // Per section, per track: pattern slot + audible flag. Tracks are
    // addressed by index and never shift (deleting a track clears its cells),
    // so sections survive track edits without remapping.
    std::array<std::array<std::uint8_t, maxTracks>, maxSections> slots {};
    std::array<std::array<bool, maxTracks>, maxSections> trackOn {};
    Arrangement()
    {
        for (auto& row : trackOn)
            row.fill(true);
    }
    bool valid() const
    {
        if (sections < 1 || sections > maxSections)
            return false;
        for (const auto part : parts)
            if (part >= SongPart::numParts)
                return false;
        for (const auto& chord : chords)
            if (!chord.valid())
                return false;
        for (const auto& row : slots)
            for (const auto slot : row)
                if (slot >= numPatterns)
                    return false;
        return true;
    }
    int songTicks() const { return sections * patternTicks; }
    bool operator==(const Arrangement&) const = default;

    // Section editing. Each section is a column (part type, pattern slot per
    // track, on/off per track) and moves as a unit. All return false and
    // leave the arrangement untouched when the request is out of range.
    bool insertSection(int at, int copyFrom = -1)
    {
        if (sections >= maxSections || at < 0 || at > sections)
            return false;
        Column column = copyFrom >= 0 && copyFrom < sections ? columnAt(copyFrom) : Column {};
        if (copyFrom < 0 || copyFrom >= sections)
            column.on.fill(true);
        const SectionChord carried = copyFrom >= 0 && copyFrom < sections
            ? chords[static_cast<std::size_t>(copyFrom)] : SectionChord {};
        for (int s = sections; s > at; --s)
        {
            setColumn(s, columnAt(s - 1));
            chords[static_cast<std::size_t>(s)] = chords[static_cast<std::size_t>(s - 1)];
        }
        setColumn(at, column);
        chords[static_cast<std::size_t>(at)] = carried;
        ++sections;
        return true;
    }
    bool duplicateSection(int at) { return at >= 0 && at < sections && insertSection(at + 1, at); }
    bool removeSection(int at)
    {
        if (sections <= 1 || at < 0 || at >= sections)
            return false;
        for (int s = at; s < sections - 1; ++s)
        {
            setColumn(s, columnAt(s + 1));
            chords[static_cast<std::size_t>(s)] = chords[static_cast<std::size_t>(s + 1)];
        }
        Column cleared {};
        cleared.on.fill(true);
        setColumn(sections - 1, cleared);
        chords[static_cast<std::size_t>(sections - 1)] = SectionChord {};
        --sections;
        return true;
    }
    bool moveSection(int from, int to)
    {
        if (from < 0 || from >= sections || to < 0 || to >= sections || from == to)
            return false;
        const auto moving = columnAt(from);
        const auto movingChord = chords[static_cast<std::size_t>(from)];
        const int step = to > from ? 1 : -1;
        for (int s = from; s != to; s += step)
        {
            setColumn(s, columnAt(s + step));
            chords[static_cast<std::size_t>(s)] = chords[static_cast<std::size_t>(s + step)];
        }
        setColumn(to, moving);
        chords[static_cast<std::size_t>(to)] = movingChord;
        return true;
    }

private:
    struct Column
    {
        SongPart part = SongPart::Section;
        std::array<std::uint8_t, maxTracks> slot {};
        std::array<bool, maxTracks> on {};
    };
    Column columnAt(int s) const
    {
        const auto i = static_cast<std::size_t>(s);
        return { parts[i], slots[i], trackOn[i] };
    }
    void setColumn(int s, const Column& column)
    {
        const auto i = static_cast<std::size_t>(s);
        parts[i] = column.part;
        slots[i] = column.slot;
        trackOn[i] = column.on;
    }
};

// Repeated clicks on a song cell: empty -> A -> B -> C -> D -> empty.
// An off cell always starts on A, regardless of its remembered slot.
inline std::pair<std::uint8_t, bool> nextArrangementCell(std::uint8_t slot, bool on)
{
    if (!on)
        return { 0, true };
    if (slot + 1 < numPatterns)
        return { static_cast<std::uint8_t>(slot + 1), true };
    return { 0, false };
}

// Sections are instances: several sections may share one library slot, so one
// edit changes them all. makeSectionUnique detaches a section by copying its
// effective pattern into a free slot and repointing it. Returns false (no-op)
// when the section already has its slot to itself, or when every slot is in
// use elsewhere. Callers wrap the mutation in undo. Tracks are addressed by
// index; library is that track's own pattern store.
inline bool makeSectionUnique(Arrangement& song, std::array<Pattern, numPatterns>& library,
                              int section, int track)
{
    if (section < 0 || section >= song.sections || track < 0 || track >= maxTracks)
        return false;
    const auto slot = song.slots[static_cast<std::size_t>(section)][static_cast<std::size_t>(track)];
    bool shared = false;
    for (int s = 0; s < song.sections; ++s)
    {
        if (s != section && song.slots[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)] == slot)
            shared = true;
    }
    if (!shared)
        return false;
    int freeSlot = -1;
    for (int i = 0; i < numPatterns; ++i)
    {
        bool used = false;
        for (int s = 0; s < song.sections; ++s)
            if (song.slots[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)] == i)
                used = true;
        if (!used)
        {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot < 0)
        return false;
    library[static_cast<std::size_t>(freeSlot)] = library[slot];
    song.slots[static_cast<std::size_t>(section)][static_cast<std::size_t>(track)]
        = static_cast<std::uint8_t>(freeSlot);
    return true;
}

inline bool makeDrumSectionUnique(Arrangement& song, std::array<DrumPattern, numPatterns>& library,
                                  int section, int track)
{
    if (section < 0 || section >= song.sections || track < 0 || track >= maxTracks)
        return false;
    const auto slot = song.slots[static_cast<std::size_t>(section)][static_cast<std::size_t>(track)];
    bool shared = false;
    for (int s = 0; s < song.sections; ++s)
    {
        if (s != section && song.slots[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)] == slot)
            shared = true;
    }
    if (!shared)
        return false;
    int freeSlot = -1;
    for (int i = 0; i < numPatterns; ++i)
    {
        bool used = false;
        for (int s = 0; s < song.sections; ++s)
            if (song.slots[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)] == i)
                used = true;
        if (!used)
        {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot < 0)
        return false;
    library[static_cast<std::size_t>(freeSlot)] = library[slot];
    song.slots[static_cast<std::size_t>(section)][static_cast<std::size_t>(track)]
        = static_cast<std::uint8_t>(freeSlot);
    return true;
}

// Sections sharing a slot on one track, for "shared by N" indicators.
inline std::vector<int> sectionsSharingSlot(const Arrangement& song, int track, int slot)
{
    std::vector<int> result;
    if (track < 0 || track >= maxTracks)
        return result;
    for (int s = 0; s < song.sections; ++s)
    {
        if (song.slots[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)] == slot)
            result.push_back(s + 1);
    }
    return result;
}

struct ProjectState
{
    std::array<Track, maxTracks> tracks {};
    LimiterParams master;
    ProjectSends sends;
    Arrangement song;
    bool songMode = false;
    std::array<AudioTakeMeta, maxTakes> takes {};
    int takeCount = 0;
    double bpm = 120.0;
    int musicKey = 0; // C=0..B=11
    MusicScale musicScale = MusicScale::Major;
    int activeTrackCount() const
    {
        int count = 0;
        for (const auto& track : tracks)
            count += track.kind == TrackKind::None ? 0 : 1;
        return count;
    }
    bool valid() const
    {
        if (!(std::isfinite(bpm) && bpm >= 40.0 && bpm <= 240.0
            && master.valid() && song.valid() && sends.valid()))
            return false;
        if (musicKey < 0 || musicKey > 11 || musicScale >= MusicScale::numScales)
            return false;
        for (const auto& track : tracks)
            if (!track.valid())
                return false;
        if (takeCount < 0 || takeCount > maxTakes)
            return false;
        for (int i = 0; i < takeCount; ++i)
        {
            const auto& take = takes[static_cast<std::size_t>(i)];
            if (!take.valid())
                return false;
            for (int j = 0; j < i; ++j)
                if (takes[static_cast<std::size_t>(j)].id == take.id)
                    return false;
        }
        return true;
    }
    bool operator==(const ProjectState&) const = default;
};

// A fresh project: one synth and one drum track, everything else empty.
inline ProjectState defaultProject()
{
    ProjectState project;
    project.tracks[0].id = 1;
    project.tracks[0].setTrackName("Sine Keys");
    project.tracks[0].kind = TrackKind::Synth;
    project.tracks[0].icon = 0;
    project.tracks[1].id = 2;
    project.tracks[1].setTrackName("Starter Drums");
    project.tracks[1].kind = TrackKind::Drums;
    project.tracks[1].icon = 1;
    return project;
}

inline std::uint32_t nextTrackId(const ProjectState& project)
{
    std::uint32_t id = 1;
    for (const auto& track : project.tracks)
        id = std::max(id, track.id + 1);
    return id;
}

// Reorder two usable tracks, carrying each track's arrangement column
// (slots and gates) with it so the song keeps playing the same music.
// Returns false when either endpoint is out of range or empty.
inline bool moveTrackState(ProjectState& project, int from, int to)
{
    from = std::clamp(from, 0, maxTracks - 1);
    to = std::clamp(to, 0, maxTracks - 1);
    if (from == to
        || project.tracks[static_cast<std::size_t>(from)].kind == TrackKind::None
        || project.tracks[static_cast<std::size_t>(to)].kind == TrackKind::None)
        return false;
    auto swapAdjacent = [&](int a, int b) {
        const auto ai = static_cast<std::size_t>(a), bi = static_cast<std::size_t>(b);
        std::swap(project.tracks[ai], project.tracks[bi]);
        for (int s = 0; s < maxSections; ++s)
        {
            const auto si = static_cast<std::size_t>(s);
            std::swap(project.song.slots[si][ai], project.song.slots[si][bi]);
            std::swap(project.song.trackOn[si][ai], project.song.trackOn[si][bi]);
        }
    };
    if (from < to)
        for (int i = from; i < to; ++i)
            swapAdjacent(i, i + 1);
    else
        for (int i = from; i > to; --i)
            swapAdjacent(i, i - 1);
    return true;
}

// One-click song structures. Each part type prefers a pattern slot
// (verses A, choruses/drops B, bridges/builds/pre-choruses C, breaks D) and
// intros/outros/breaks drop the drums. A preferred slot that is empty on a
// track falls back to slot A, then to that track's first non-empty slot, so a
// project with only pattern A still yields a complete, audible song.
enum class SongTemplate { Simple = 0, Pop, Edm, HipHop, numTemplates };

inline const char* songTemplateName(SongTemplate tpl)
{
    switch (tpl)
    {
        case SongTemplate::Simple: return "Simple  (Intro Verse Chorus Verse Chorus Outro)";
        case SongTemplate::Pop: return "Pop  (with pre-choruses and a bridge)";
        case SongTemplate::Edm: return "EDM  (Intro Build Drop Break Build Drop Outro)";
        case SongTemplate::HipHop: return "Hip-hop  (double verses and hooks)";
        case SongTemplate::numTemplates: break;
    }
    return "Song";
}

inline std::vector<SongPart> songTemplateParts(SongTemplate tpl)
{
    using P = SongPart;
    switch (tpl)
    {
        case SongTemplate::Simple: return { P::Intro, P::Verse, P::Chorus, P::Verse, P::Chorus, P::Outro };
        case SongTemplate::Pop: return { P::Intro, P::Verse, P::PreChorus, P::Chorus, P::Verse, P::PreChorus,
                                         P::Chorus, P::Bridge, P::Chorus, P::Outro };
        case SongTemplate::Edm: return { P::Intro, P::Build, P::Drop, P::Break, P::Build, P::Drop, P::Outro };
        case SongTemplate::HipHop: return { P::Intro, P::Verse, P::Verse, P::Chorus, P::Verse, P::Verse,
                                            P::Chorus, P::Outro };
        case SongTemplate::numTemplates: break;
    }
    return { P::Verse };
}

inline bool trackSlotHasContent(const Track& track, int slot)
{
    const auto i = static_cast<std::size_t>(std::clamp(slot, 0, numPatterns - 1));
    return track.kind == TrackKind::Drums ? track.drumPatterns[i].hitCount() > 0 : track.melodies[i].count > 0;
}

// How busy/loud one loop is (0..~1): onset density blended with velocity.
// Deterministic ranking fuel for orchestration, not an audible property.
inline float loopEnergy(const Track& track, int slot)
{
    const auto i = static_cast<std::size_t>(std::clamp(slot, 0, numPatterns - 1));
    if (track.kind == TrackKind::Drums)
    {
        const auto& grid = track.drumPatterns[i];
        int hits = 0, vel = 0;
        for (const auto& row : grid.steps)
            for (const auto velocity : row)
                if (velocity > 0)
                {
                    ++hits;
                    vel += velocity;
                }
        if (hits == 0)
            return 0.0f;
        return std::min(1.0f, static_cast<float>(hits) / 64.0f) * 0.7f
            + (static_cast<float>(vel) / static_cast<float>(hits) / 127.0f) * 0.3f;
    }
    const auto& pattern = track.melodies[i];
    if (pattern.count == 0)
        return 0.0f;
    bool steps[gridSteps] = {};
    int vel = 0;
    for (int n = 0; n < pattern.count; ++n)
    {
        const auto& note = pattern.notes[static_cast<std::size_t>(n)];
        vel += note.velocity;
        for (int step = note.start / stepTicks; step <= (note.start + note.duration - 1) / stepTicks && step < gridSteps; ++step)
            if (step >= 0)
                steps[step] = true;
    }
    int active = 0;
    for (bool on : steps)
        active += on ? 1 : 0;
    // Onsets dominate: sixteen staccato 16ths outrank a few long notes.
    return std::min(1.0f, static_cast<float>(pattern.count) / 32.0f) * 0.5f
        + static_cast<float>(active) / static_cast<float>(gridSteps) * 0.3f
        + (static_cast<float>(vel) / static_cast<float>(pattern.count) / 127.0f) * 0.2f;
}

// One-line character sketch of a loop, e.g. "driving 16ths, 24 notes" or
// "four-on-the-floor, backbeat, 40 hits". Empty loops report "empty".
// Shown in the song UI and read by the AI so it can tell verse material
// from chorus material without renaming the A-D loops.
inline juce::String describeLoopRole(const Track& track, int slot)
{
    const auto i = static_cast<std::size_t>(std::clamp(slot, 0, numPatterns - 1));
    if (!trackSlotHasContent(track, slot))
        return "empty";
    if (track.kind == TrackKind::Drums)
    {
        const auto& grid = track.drumPatterns[i];
        int hits = 0;
        for (const auto& row : grid.steps)
            for (const auto velocity : row)
                hits += velocity > 0 ? 1 : 0;
        juce::StringArray tags;
        const auto& kick = grid.steps[0];
        if (kick[0] > 0 && kick[16] > 0 && kick[32] > 0 && kick[48] > 0)
            tags.add("four-on-the-floor kick");
        const auto& snare = grid.steps[1];
        if (snare[16] > 0 && snare[48] > 0)
            tags.add("backbeat snare");
        int hats = 0;
        for (int step = 0; step < gridSteps; ++step)
            hats += grid.steps[2][static_cast<std::size_t>(step)] > 0 ? 1 : 0;
        if (hats >= 32)
            tags.add("driving hats");
        else if (hats >= 12)
            tags.add("steady hats");
        int lastBar = 0, earlier = 0;
        for (int pad = 0; pad < drumPads; ++pad)
            for (int step = 0; step < gridSteps; ++step)
                if (grid.steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(step)] > 0)
                    (step >= 48 ? lastBar : earlier)++;
        if (lastBar > earlier / 3 + 2)
            tags.add("fill into bar 4");
        if (tags.isEmpty())
            tags.add(hits < 12 ? "sparse" : "busy");
        return juce::String(hits) + " hits, " + tags.joinIntoString(", ");
    }
    const auto& pattern = track.melodies[i];
    bool steps[gridSteps] = {};
    int lo = 127, hi = 0, dur = 0;
    for (int n = 0; n < pattern.count; ++n)
    {
        const auto& note = pattern.notes[static_cast<std::size_t>(n)];
        lo = std::min(lo, note.pitch);
        hi = std::max(hi, note.pitch);
        dur += note.duration;
        for (int step = note.start / stepTicks; step <= (note.start + note.duration - 1) / stepTicks && step < gridSteps; ++step)
            if (step >= 0)
                steps[step] = true;
    }
    int active = 0;
    for (bool on : steps)
        active += on ? 1 : 0;
    juce::String feel;
    if (dur / pattern.count >= 1440)
        feel = "sustained chords";
    else if (active >= 48)
        feel = "driving 16ths";
    else if (active >= 24)
        feel = "steady groove";
    else if (pattern.count <= 6)
        feel = "sparse motif";
    else
        feel = "laid-back groove";
    const int octaves = (hi - lo + 12) / 12;
    return juce::String(pattern.count) + " notes, " + feel + ", "
        + juce::String(octaves) + "-octave range";
}

// Which loop of a track fits a song part, judged by measured character:
// choruses/drops take the biggest loop, verses/intros/outros the smallest,
// builds and bridges the runner-up, so templates follow the actual music
// instead of fixed letters. Ties prefer higher letters for big parts and
// lower letters for small parts (so a copied B still beats A in a chorus).
// A track with nothing written keeps the old fixed default for the part.
inline int suggestSlotForPart(const Track& track, SongPart part)
{
    int filled[numPatterns], count = 0;
    for (int slot = 0; slot < numPatterns; ++slot)
        if (trackSlotHasContent(track, slot))
            filled[count++] = slot;
    if (count == 0)
    {
        switch (part)
        {
            case SongPart::Chorus: case SongPart::Drop: return 1;
            case SongPart::PreChorus: case SongPart::Bridge: case SongPart::Build: return 2;
            case SongPart::Break: return 3;
            case SongPart::Section: case SongPart::Intro: case SongPart::Verse: case SongPart::Outro:
            case SongPart::numParts: break;
        }
        return 0;
    }
    const bool big = part == SongPart::Chorus || part == SongPart::Drop;
    const bool middle = part == SongPart::PreChorus || part == SongPart::Bridge || part == SongPart::Build;
    int best = filled[0];
    if (middle && count > 1)
    {
        // Runner-up by energy (ties: higher letter), so builds rise toward
        // the peak instead of jumping straight to it.
        int order[numPatterns];
        for (int k = 0; k < count; ++k)
            order[k] = filled[k];
        for (int a = 0; a < count; ++a)
            for (int b = a + 1; b < count; ++b)
            {
                const float ea = loopEnergy(track, order[a]), eb = loopEnergy(track, order[b]);
                if (eb > ea + 1.0e-6f || (std::abs(eb - ea) < 1.0e-6f && order[b] > order[a]))
                    std::swap(order[a], order[b]);
            }
        best = order[1];
    }
    else
    {
        for (int k = 1; k < count; ++k)
        {
            const int slot = filled[k];
            const float energy = loopEnergy(track, slot), bestEnergy = loopEnergy(track, best);
            const bool tied = std::abs(energy - bestEnergy) < 1.0e-6f;
            if (big ? (energy > bestEnergy + 1.0e-6f || (tied && slot > best))
                    : (energy < bestEnergy - 1.0e-6f || (tied && slot < best)))
                best = slot;
        }
    }
    return best;
}

inline Arrangement buildSongFromTemplate(const ProjectState& project, SongTemplate tpl)
{
    Arrangement song;
    const auto parts = songTemplateParts(tpl);
    song.sections = std::clamp(static_cast<int>(parts.size()), 1, maxSections);
    for (int s = 0; s < song.sections; ++s)
    {
        const auto part = parts[static_cast<std::size_t>(s)];
        song.parts[static_cast<std::size_t>(s)] = part;
        const bool drumless = part == SongPart::Intro || part == SongPart::Outro || part == SongPart::Break;
        for (int t = 0; t < maxTracks; ++t)
        {
            const auto& track = project.tracks[static_cast<std::size_t>(t)];
            const int slot = suggestSlotForPart(track, part);
            song.slots[static_cast<std::size_t>(s)][static_cast<std::size_t>(t)] = static_cast<std::uint8_t>(slot);
            song.trackOn[static_cast<std::size_t>(s)][static_cast<std::size_t>(t)]
                = !(drumless && track.kind == TrackKind::Drums);
        }
    }
    return song;
}
}
