#pragma once
#include "AudioTakes.h"
#include "Fx.h"
#include "Timing.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <vector>

namespace sonora
{
inline constexpr int lowestPitch = 48;
inline constexpr int highestPitch = 71;
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

struct TrackMix
{
    float volume = 0.8f;
    bool mute = false, solo = false;
    bool valid() const { return std::isfinite(volume) && volume >= 0.0f && volume <= 1.5f; }
    bool audible(const TrackMix& other) const { return !mute && (solo || !other.solo); }
    bool operator==(const TrackMix&) const = default;
};

inline constexpr int maxSections = 8;
inline constexpr int numPatterns = 4;
inline constexpr int sampleFileCapacity = 260;
inline constexpr int numKitVariants = 3;

struct Arrangement
{
    int sections = 2;
    std::array<bool, maxSections> melodyOn {}, drumsOn {};
    // Which library pattern each section plays (0-3). Sections tile the
    // 4-bar patterns; loop mode instead previews the editor-selected pattern.
    std::array<std::uint8_t, maxSections> melodyPattern {}, drumPattern {};
    Arrangement() { melodyOn.fill(true); drumsOn.fill(true); }
    bool valid() const
    {
        if (sections < 1 || sections > maxSections)
            return false;
        for (int i = 0; i < maxSections; ++i)
            if (melodyPattern[static_cast<std::size_t>(i)] >= numPatterns
                || drumPattern[static_cast<std::size_t>(i)] >= numPatterns)
                return false;
        return true;
    }
    int songTicks() const { return sections * patternTicks; }
    bool operator==(const Arrangement&) const = default;
};

// Sections are instances: several sections may share one library slot, so one
// edit changes them all. makeSectionUnique detaches a section by copying its
// effective pattern into a free slot and repointing it. Returns false (no-op)
// when the section already has its slot to itself, or when every slot is in
// use elsewhere. Callers wrap the mutation in undo.
inline bool makeSectionUnique(Arrangement& song, std::array<Pattern, numPatterns>& library, int section)
{
    if (section < 0 || section >= song.sections)
        return false;
    const auto slot = song.melodyPattern[static_cast<std::size_t>(section)];
    bool shared = false;
    int freeSlot = -1;
    for (int s = 0; s < song.sections; ++s)
    {
        if (s != section && song.melodyPattern[static_cast<std::size_t>(s)] == slot)
            shared = true;
    }
    if (!shared)
        return false;
    for (int i = 0; i < numPatterns; ++i)
    {
        bool used = false;
        for (int s = 0; s < song.sections; ++s)
            if (song.melodyPattern[static_cast<std::size_t>(s)] == i)
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
    song.melodyPattern[static_cast<std::size_t>(section)] = static_cast<std::uint8_t>(freeSlot);
    return true;
}

inline bool makeDrumSectionUnique(Arrangement& song, std::array<DrumPattern, numPatterns>& library, int section)
{
    if (section < 0 || section >= song.sections)
        return false;
    const auto slot = song.drumPattern[static_cast<std::size_t>(section)];
    bool shared = false;
    for (int s = 0; s < song.sections; ++s)
    {
        if (s != section && song.drumPattern[static_cast<std::size_t>(s)] == slot)
            shared = true;
    }
    if (!shared)
        return false;
    int freeSlot = -1;
    for (int i = 0; i < numPatterns; ++i)
    {
        bool used = false;
        for (int s = 0; s < song.sections; ++s)
            if (song.drumPattern[static_cast<std::size_t>(s)] == i)
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
    song.drumPattern[static_cast<std::size_t>(section)] = static_cast<std::uint8_t>(freeSlot);
    return true;
}

// Sections sharing a slot, for "shared by N" indicators.
inline std::vector<int> sectionsSharingSlot(const Arrangement& song, bool drums, int slot)
{
    std::vector<int> result;
    for (int s = 0; s < song.sections; ++s)
    {
        const auto current = drums ? song.drumPattern[static_cast<std::size_t>(s)]
                                   : song.melodyPattern[static_cast<std::size_t>(s)];
        if (current == slot)
            result.push_back(s + 1);
    }
    return result;
}

struct ProjectState
{
    std::array<Pattern, numPatterns> melodies {};
    std::array<DrumPattern, numPatterns> drumPatterns {};
    // Per-pad custom sample filenames (empty = built-in starter). Resolved
    // against the media folder, like takes.
    std::array<std::array<char, sampleFileCapacity>, drumPads> padSamples {};
    // Factory kit variant voicing the built-in pads (0=Starter, 1=Deep, 2=Crisp).
    int kitVariant = 0;
    TrackMix melodyMix, drumMix;
    TrackFx melodyFx, drumFx;
    LimiterParams master;
    Arrangement song;
    bool songMode = false;
    std::array<AudioTakeMeta, maxTakes> takes {};
    int takeCount = 0;
    double bpm = 120.0;
    bool valid() const
    {
        if (!(std::isfinite(bpm) && bpm >= 40.0 && bpm <= 240.0
            && melodyMix.valid() && drumMix.valid() && song.valid()
            && melodyFx.valid() && drumFx.valid() && master.valid()))
            return false;
        if (kitVariant < 0 || kitVariant >= numKitVariants)
            return false;
        for (const auto& pattern : melodies)
            if (!pattern.valid())
                return false;
        for (const auto& drums : drumPatterns)
            if (!drums.valid())
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
}
