#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace sonora
{
// Per-track settings for the Sampler instrument: any audio file played
// chromatically across the keyboard. Plain values so ProjectState stays
// trivially copyable for the realtime snapshot queue. The audio itself is not
// stored here; the track keeps only a file name (see Track::samplerFile).
inline constexpr int samplerFileCapacity = 260;

struct SamplerParams
{
    int rootNote = 60;                        // MIDI key that plays the file at its own pitch
    float tune = 0.0f;                        // cents
    float start = 0.0f, end = 1.0f;           // trim, as fractions of the file
    float loopStart = 0.0f, loopEnd = 1.0f;   // loop region, fractions of the file
    bool loop = false;                        // sustain by looping the region while a key is held
    bool oneShot = false;                     // ignore key release; the sound plays to its end
    bool reverse = false;
    bool keyTrack = true;                     // false: every key plays the file at its own pitch
    float attack = 0.002f, decay = 0.2f, sustain = 1.0f, release = 0.12f;
    float gain = 1.0f;
    bool operator==(const SamplerParams&) const = default;

    // Smallest usable region, as a fraction of the file.
    static constexpr float minRegion = 0.001f;
    // Float rounding at the extremes (start 0.999 + 0.001) must not flip validity.
    static constexpr float regionSlack = 1.0e-6f;

    bool valid() const
    {
        auto in = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
        auto time = [&](float v) { return in(v, 0.001f, 5.0f); };
        return rootNote >= 0 && rootNote <= 127 && in(tune, -100.0f, 100.0f)
            && in(start, 0.0f, 1.0f) && in(end, 0.0f, 1.0f) && end - start >= minRegion - regionSlack
            && in(loopStart, 0.0f, 1.0f) && in(loopEnd, 0.0f, 1.0f) && loopEnd - loopStart >= minRegion - regionSlack
            && time(attack) && time(decay) && in(sustain, 0.0f, 1.0f) && time(release)
            && in(gain, 0.0f, 2.0f);
    }
};

// Keeps both regions non-degenerate and the loop inside the trim. The editor
// and the AI agent call this after changing a marker, so every route to the
// settings ends in a state the engine plays as drawn.
inline void normalizeRegions(SamplerParams& p)
{
    constexpr float m = SamplerParams::minRegion;
    // Every clamp keeps lo <= hi even after rounding (std::clamp with lo > hi
    // is undefined), so the result is always an ordered, in-range region.
    p.start = std::clamp(p.start, 0.0f, 1.0f - m);
    p.end = std::clamp(p.end, std::min(p.start + m, 1.0f), 1.0f);
    p.loopStart = std::clamp(p.loopStart, p.start, std::max(p.start, p.end - m));
    p.loopEnd = std::clamp(p.loopEnd, std::min(p.loopStart + m, p.end), p.end);
}

// A sample file name is a relative path inside the project media folder, the
// session folder, or the sample library. Project files are untrusted input,
// so absolute paths and parent-directory hops are never accepted.
inline bool isSafeSampleName(const char* name)
{
    if (name == nullptr || name[0] == '\0' || name[0] == '/' || name[0] == '\\')
        return false;
    const std::size_t length = std::strlen(name);
    if (length >= static_cast<std::size_t>(samplerFileCapacity))
        return false;
    for (std::size_t i = 0; i < length; ++i)
    {
        const char c = name[i];
        if (static_cast<unsigned char>(c) < 0x20 || c == '\\')
            return false;
        const bool atStart = i == 0 || name[i - 1] == '/';
        if (atStart && c == '.' && name[i + 1] == '.' && (name[i + 2] == '/' || name[i + 2] == '\0'))
            return false;
    }
    return true;
}
}
