#pragma once
#include "Pattern.h"

namespace sonora
{
// Audio-thread owned. Tick events map to rounded sample positions, independent
// of callback size. Events at a block's end belong to the following block.
//
// loopFrames always spans exactly one 4-bar pattern (patternTicks); song mode
// tiles sections as multiples of it and tracks the song end separately. Never
// repurpose loopFrames as a song length, or sections past the first go silent.
class LoopScheduler
{
public:
    void configure(double sampleRate, double bpm)
    {
        const auto fraction = static_cast<double>(position) / static_cast<double>(loopFrames);
        framesPerTick = sampleRate * 60.0 / (bpm * ticksPerQuarter);
        loopFrames = std::max<std::int64_t>(1, std::llround(patternTicks * framesPerTick));
        position = std::min(loopFrames - 1, static_cast<std::int64_t>(fraction * static_cast<double>(loopFrames)));
    }
    void rewind() { position = 0; }
    double tickPosition() const { return static_cast<double>(position) / framesPerTick; }
    std::int64_t lengthInSamples() const { return loopFrames; }
    std::int64_t samplePosition() const { return position; }
    std::int64_t framesForTick(int tick) const { return frameAt(tick); }

    // Section gating for song mode: each active section plays its track's own
    // library pattern, sounding only where the section cell is on. Song
    // position advances linearly and reports completion instead of wrapping.
    template <typename Emit>
    void scheduleDrumsSong(const std::array<DrumPattern, numPatterns>& patterns, const Arrangement& song,
                           int track, int samples, Emit&& emit) const
    {
        const auto songFrames = std::llround(static_cast<double>(song.songTicks()) * framesPerTick);
        for (int s = 0; s < song.sections; ++s)
        {
            if (!song.trackOn[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)])
                continue;
            const auto& pattern = patterns[song.slots[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)]];
            for (int pad = 0; pad < drumPads; ++pad)
                for (int step = 0; step < gridSteps; ++step)
                {
                    const auto velocity = pattern.steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(step)];
                    if (velocity == 0)
                        continue;
                    const auto frame = static_cast<std::int64_t>(s) * loopFrames + frameAt(step * stepTicks);
                    if (frame < position || frame >= position + samples || frame >= songFrames)
                        continue;
                    emit(pad, velocity, static_cast<int>(frame - position));
                }
        }
    }

    // Uses the same transport position as notes, without advancing it. Call
    // before process() so both instruments share exact loop and tempo boundaries.
    template <typename Emit>
    void scheduleDrums(const DrumPattern& pattern, int samples, Emit&& emit) const
    {
        scheduleDrumsAt(pattern, samples, position, std::forward<Emit>(emit));
    }

    // Block-relative drum scheduling: every track in a multi-track loop block
    // schedules against the same block start so one track's cursor never
    // starves the others. The caller advances once via advance().
    template <typename Emit>
    void scheduleDrumsAt(const DrumPattern& pattern, int samples, std::int64_t blockPosition,
                         Emit&& emit) const
    {
        for (int pad = 0; pad < drumPads; ++pad)
            for (int step = 0; step < gridSteps; ++step)
            {
                const auto velocity = pattern.steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(step)];
                if (velocity == 0)
                    continue;
                const auto frame = frameAt(step * stepTicks) % loopFrames;
                auto offset = (frame - blockPosition + loopFrames) % loopFrames;
                for (; offset < samples; offset += loopFrames)
                    emit(pad, velocity, static_cast<int>(offset));
            }
    }

    template <typename Emit>
    void processLoop(const Pattern& pattern, int samples, bool chase, Emit&& emit)
    {
        processLoopAt(pattern, samples, chase, position, std::forward<Emit>(emit));
        advance(samples);
    }

    // Block-relative melody scheduling without consuming the shared cursor.
    template <typename Emit>
    void processLoopAt(const Pattern& pattern, int samples, bool chase, std::int64_t blockPosition,
                       Emit&& emit) const
    {
        if (samples <= 0)
            return;
        // Reconstruct sustained notes after a pattern or tempo change.
        if (chase)
            for (int i = 0; i < pattern.count; ++i)
            {
                const auto& n = pattern.notes[static_cast<std::size_t>(i)];
                if (frameAt(n.start) < blockPosition && frameAt(n.start + n.duration) > blockPosition)
                    emit(n, true, 0);
            }
        // Offs precede ons at shared boundaries, including the loop seam.
        for (bool on : { false, true })
            for (int i = 0; i < pattern.count; ++i)
            {
                const auto& n = pattern.notes[static_cast<std::size_t>(i)];
                const auto frame = frameAt(on ? n.start : n.start + n.duration) % loopFrames;
                auto offset = (frame - blockPosition + loopFrames) % loopFrames;
                for (; offset < samples; offset += loopFrames)
                    emit(n, on, static_cast<int>(offset));
            }
    }

    void advance(int samples)
    {
        if (samples > 0)
            position = (position + samples) % loopFrames;
    }

    template <typename Emit>
    void process(const Pattern& pattern, int samples, bool chase, Emit&& emit)
    {
        processLoop(pattern, samples, chase, std::forward<Emit>(emit));
    }

    // Song mode tiles the per-section library patterns with per-section
    // gating and linear transport that returns true when the song end is
    // reached during this block. Loop mode is handled by processLoop().
    // The emit callback gains the track index: emit(track, note, on, offset).
    template <typename Emit>
    bool processSong(const std::array<Track, maxTracks>& tracks, const Arrangement& song,
                     int samples, bool chase, Emit&& emit)
    {
        if (samples <= 0)
            return false;
        const auto songFrames = std::llround(static_cast<double>(song.songTicks()) * framesPerTick);
        const auto currentSection = std::clamp(static_cast<int>(tickPosition()) / patternTicks,
                                               0, song.sections - 1);
        if (chase)
            for (int track = 0; track < maxTracks; ++track)
            {
                if (tracks[static_cast<std::size_t>(track)].kind != TrackKind::Synth)
                    continue;
                if (!song.trackOn[static_cast<std::size_t>(currentSection)][static_cast<std::size_t>(track)])
                    continue;
                const auto& pattern = tracks[static_cast<std::size_t>(track)]
                                          .melodies[song.slots[static_cast<std::size_t>(currentSection)]
                                                                 [static_cast<std::size_t>(track)]];
                for (int i = 0; i < pattern.count; ++i)
                {
                    const auto& n = pattern.notes[static_cast<std::size_t>(i)];
                    if (frameAt(n.start) % loopFrames < position % loopFrames
                        && frameAt(n.start + n.duration) % loopFrames > position % loopFrames)
                        emit(track, n, true, 0);
                }
            }
        // Offs precede ons at shared boundaries.
        for (bool on : { false, true })
            for (int s = 0; s < song.sections; ++s)
                for (int track = 0; track < maxTracks; ++track)
                {
                    if (tracks[static_cast<std::size_t>(track)].kind != TrackKind::Synth)
                        continue;
                    if (!song.trackOn[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)])
                        continue;
                    const auto& pattern = tracks[static_cast<std::size_t>(track)]
                                              .melodies[song.slots[static_cast<std::size_t>(s)]
                                                                     [static_cast<std::size_t>(track)]];
                    for (int i = 0; i < pattern.count; ++i)
                    {
                        const auto& n = pattern.notes[static_cast<std::size_t>(i)];
                        const auto frame = static_cast<std::int64_t>(s) * loopFrames
                            + frameAt(on ? n.start : n.start + n.duration);
                        if (frame < position || frame >= position + samples || frame >= songFrames)
                            continue;
                        emit(track, n, on, static_cast<int>(frame - position));
                    }
                }
        position += samples;
        if (position >= songFrames)
        {
            position = songFrames;
            return true;
        }
        return false;
    }

private:
    std::int64_t frameAt(int tick) const { return std::llround(tick * framesPerTick); }
    double framesPerTick = 25.0;
    std::int64_t loopFrames = 384000;
    std::int64_t position = 0;
};
}
