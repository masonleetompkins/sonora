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
    std::array<Pattern, numPatterns> melodies {};
    std::array<DrumPattern, numPatterns> drumPatterns {};
    std::array<std::array<char, sampleFileCapacity>, drumPads> padSamples {};
    int kitVariant = 0;
    TrackMix mix;
    TrackFx fx;
    bool valid() const
    {
        // Empty slots carry no identity and their content is ignored.
        if (kind == TrackKind::None)
            return true;
        if (id == 0 || id > 2147483647u || name[0] == '\0')
            return false;
        if (kind != TrackKind::Synth && kind != TrackKind::Drums)
            return false;
        if (kitVariant < 0 || kitVariant >= numKitVariants)
            return false;
        for (const auto& pattern : melodies)
            if (!pattern.valid())
                return false;
        for (const auto& drums : drumPatterns)
            if (!drums.valid())
                return false;
        return true;
    }
    bool operator==(const Track&) const = default;
    juce::String trackName() const { return juce::String(name); }
    void setTrackName(const juce::String& value)
    {
        const auto bytes = value.trim().toRawUTF8();
        std::strncpy(name, bytes, trackNameCapacity - 1);
        name[trackNameCapacity - 1] = '\0';
    }
};

struct Arrangement
{
    int sections = 2;
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
        for (const auto& row : slots)
            for (const auto slot : row)
                if (slot >= numPatterns)
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
    Arrangement song;
    bool songMode = false;
    std::array<AudioTakeMeta, maxTakes> takes {};
    int takeCount = 0;
    double bpm = 120.0;
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
            && master.valid() && song.valid()))
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
}
