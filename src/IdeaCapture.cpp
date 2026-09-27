#include "IdeaCapture.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sonora
{
namespace
{
int snap(double ticks)
{
    return static_cast<int>(std::lround(ticks / 120.0)) * 120;
}

int fitPitch(int pitch)
{
    return std::clamp(pitch, lowestPitch, highestPitch);
}
}

IdeaResult interpretIdea(const std::vector<IdeaEvent>& events, TrackKind kind, double bpm, double captureSeconds)
{
    IdeaResult result;
    if (events.empty() || !std::isfinite(bpm) || bpm <= 0 || !std::isfinite(captureSeconds))
        return result;
    std::vector<IdeaEvent> sorted(events);
    std::stable_sort(sorted.begin(), sorted.end(), [](auto a, auto b) { return a.seconds < b.seconds; });
    double first = captureSeconds;
    for (const auto& event : sorted)
        if (event.on && event.seconds >= 0)
            first = std::min(first, event.seconds);
    if (first >= captureSeconds)
        return result;
    const double secondsPerTick = 60.0 / (bpm * ticksPerQuarter);
    // A long sketch is compressed proportionally; short sketches keep their
    // actual rhythm and begin on beat one after removing leading silence.
    const double span = std::max(0.1, captureSeconds - first);
    const double scale = std::min(1.0, (patternTicks - stepTicks) * secondsPerTick / span);
    auto position = [&](double seconds) {
        return std::clamp(snap((seconds - first) * scale / secondsPerTick), 0, patternTicks - 120);
    };
    if (kind == TrackKind::Drums)
    {
        for (const auto& event : sorted)
        {
            if (!event.on || event.seconds < first || event.seconds >= captureSeconds)
                continue;
            int pad = midi::arturiaPadForNote(event.pitch);
            if (pad < 0)
                for (int i = 0; i < drumPads; ++i)
                    if (drumMidiNotes[static_cast<std::size_t>(i)] == event.pitch)
                        pad = i;
            if (pad < 0)
                continue;
            const int step = std::clamp(static_cast<int>(std::lround(position(event.seconds) / static_cast<double>(stepTicks))),
                                        0, gridSteps - 1);
            auto& cell = result.drums.steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(step)];
            cell = static_cast<std::uint8_t>(std::max<int>(cell, std::clamp(event.velocity, 1, 127)));
        }
        return result;
    }
    if (kind != TrackKind::Synth)
        return result;
    std::array<std::array<int, 128>, 16> held;
    for (auto& row : held) row.fill(-1);
    std::vector<Note> notes;
    for (const auto& event : sorted)
    {
        if (event.seconds < first || event.seconds > captureSeconds || event.pitch < 0 || event.pitch > 127)
            continue;
        const int channel = std::clamp(event.channel - 1, 0, 15);
        int& index = held[static_cast<std::size_t>(channel)][static_cast<std::size_t>(event.pitch)];
        const int tick = position(event.seconds);
        if (event.on && event.velocity > 0)
        {
            if (index >= 0)
                notes[static_cast<std::size_t>(index)].duration = std::max(120, tick - notes[static_cast<std::size_t>(index)].start);
            index = static_cast<int>(notes.size());
            notes.push_back({ 0, tick, stepTicks, fitPitch(event.pitch), std::clamp(event.velocity, 1, 127) });
        }
        else if (index >= 0)
        {
            auto& note = notes[static_cast<std::size_t>(index)];
            note.duration = std::max(120, tick - note.start);
            index = -1;
        }
    }
    for (const auto& row : held)
        for (int index : row)
            if (index >= 0)
            {
                auto& note = notes[static_cast<std::size_t>(index)];
                note.duration = std::max(120, position(captureSeconds) - note.start);
            }
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return std::tie(a.start, a.pitch) < std::tie(b.start, b.pitch);
    });
    for (auto note : notes)
    {
        if (result.melody.count >= Pattern::capacity)
            break;
        note.duration = std::clamp(note.duration, 120, patternTicks - note.start);
        // Do not create overlapping notes on the same pitch (even when two
        // MIDI channels fold to the same octave in the two-octave editor).
        bool duplicate = false;
        for (int i = 0; i < result.melody.count; ++i)
        {
            auto& previous = result.melody.notes[static_cast<std::size_t>(i)];
            if (previous.pitch != note.pitch || previous.start + previous.duration <= note.start)
                continue;
            if (previous.start == note.start) duplicate = true;
            else previous.duration = note.start - previous.start;
        }
        if (!duplicate)
        {
            note.id = static_cast<std::uint32_t>(result.melody.count + 1);
            result.melody.notes[static_cast<std::size_t>(result.melody.count++)] = note;
        }
    }
    return result;
}

IdeaResult interpretAudioIdea(const float* samples, int frames, double rate, double bpm)
{
    IdeaResult result;
    if (samples == nullptr || frames < 4096 || rate < 8000)
        return result;
    YinDetector detector(rate);
    const auto contour = detector.analyze(samples, frames);
    std::vector<IdeaEvent> events;
    int active = -1, start = 0;
    // Require three consecutive confident frames before emitting a new note;
    // tolerate two uncertain frames to avoid chopping a sustained hum.
    int candidate = -1, stable = 0, missing = 0;
    for (std::size_t i = 0; i <= contour.frames.size(); ++i)
    {
        const auto& frame = i < contour.frames.size() ? contour.frames[i] : PitchFrame {};
        const int pitch = frame.voiced && frame.confidence >= 0.65f && frame.f0Hz > 0
            ? fitPitch(std::clamp(static_cast<int>(std::lround(hzToMidi(frame.f0Hz))), 0, 127)) : -1;
        if (pitch == candidate && pitch >= 0) ++stable;
        else { candidate = pitch; stable = pitch >= 0 ? 1 : 0; }
        if (pitch == active) { missing = 0; continue; }
        if (pitch < 0 && active >= 0 && ++missing <= 2) continue;
        if (active >= 0 && (pitch < 0 || stable >= 3))
        {
            const int end = static_cast<int>(i) - (pitch < 0 ? missing : 2);
            if (end - start >= 3)
            {
                events.push_back({ start * contour.hopSamples / rate, active, 100, 1, true });
                events.push_back({ end * contour.hopSamples / rate, active, 0, 1, false });
            }
            active = -1;
        }
        if (pitch >= 0 && stable >= 3 && active < 0)
        {
            active = pitch;
            start = static_cast<int>(i) - 2;
            missing = 0;
        }
    }
    if (active >= 0 && static_cast<int>(contour.frames.size()) - start >= 3)
    {
        events.push_back({ start * contour.hopSamples / rate, active, 100, 1, true });
        events.push_back({ frames / rate, active, 0, 1, false });
    }
    result = interpretIdea(events, TrackKind::Synth, bpm, frames / rate);
    result.fromAudio = result.melody.count > 0;
    return result;
}
}
