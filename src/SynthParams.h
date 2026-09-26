#pragma once
#include <array>
#include <cmath>

namespace sonora
{
// Editable subtractive synth for "Sine Keys" (instrument 0). Plain floats so
// ProjectState stays trivially copyable for the realtime snapshot queue.
// Defaults reproduce the original fixed sine voice exactly.
enum SynthWave : int { WaveSine = 0, WaveTriangle, WaveSaw, WaveSquare, numSynthWaves };

struct SynthParams
{
    int wave = WaveSine, wave2 = WaveSine;
    float mix2 = 0.0f, semis2 = 0.0f, detune2 = 0.0f;           // osc 2 level, pitch, cents
    float cutoff = 20000.0f, resonance = 0.0f, envAmount = 0.0f; // Hz, 0..0.95, octaves
    float filterAttack = 0.01f, filterDecay = 0.3f, filterSustain = 1.0f, filterRelease = 0.3f;
    float attack = 0.01f, decay = 0.12f, sustain = 0.7f, release = 0.25f;
    float lfoRate = 5.0f, lfoPitch = 0.0f, lfoFilter = 0.0f;    // Hz, semitones, octaves
    float drive = 0.0f, chorus = 0.0f, level = 1.0f;
    bool operator==(const SynthParams&) const = default;

    // Filter is skipped when fully open and unmodulated (bit-exact default).
    bool filterActive() const { return cutoff < 19999.0f || std::abs(envAmount) > 0.0f || lfoFilter > 0.0f; }

    bool valid() const
    {
        auto in = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
        auto time = [&](float v) { return in(v, 0.001f, 5.0f); };
        return wave >= 0 && wave < numSynthWaves && wave2 >= 0 && wave2 < numSynthWaves
            && in(mix2, 0.0f, 1.0f) && in(semis2, -24.0f, 24.0f) && in(detune2, -50.0f, 50.0f)
            && in(cutoff, 40.0f, 20000.0f) && in(resonance, 0.0f, 0.95f) && in(envAmount, -4.0f, 4.0f)
            && time(filterAttack) && time(filterDecay) && in(filterSustain, 0.0f, 1.0f) && time(filterRelease)
            && time(attack) && time(decay) && in(sustain, 0.0f, 1.0f) && time(release)
            && in(lfoRate, 0.1f, 20.0f) && in(lfoPitch, 0.0f, 2.0f) && in(lfoFilter, 0.0f, 4.0f)
            && in(drive, 0.0f, 1.0f) && in(chorus, 0.0f, 1.0f) && in(level, 0.0f, 1.5f);
    }
};

struct SynthPatch { const char* name; SynthParams params; };

inline SynthParams makePatch(int w1, int w2, float mix2, float semis2, float detune2,
                             float cutoff, float res, float env,
                             float fa, float fd, float fs, float fr,
                             float a, float d, float s, float r,
                             float lfoRate, float lfoPitch, float lfoFilter,
                             float drive, float chorus, float level)
{
    return { w1, w2, mix2, semis2, detune2, cutoff, res, env, fa, fd, fs, fr,
             a, d, s, r, lfoRate, lfoPitch, lfoFilter, drive, chorus, level };
}

// Starting points for sound design. Index 0 is always the default sine.
inline const std::array<SynthPatch, 10>& synthPatches()
{
    static const std::array<SynthPatch, 10> patches {{
        { "Sine Keys", SynthParams {} },
        { "Warm Pad", makePatch(WaveSaw, WaveSaw, 0.8f, 0, 9, 1800, 0.2f, 1.0f,
                                0.8f, 1.5f, 0.6f, 1.2f, 0.6f, 0.5f, 0.8f, 1.2f, 0.3f, 0, 0.3f, 0, 0.6f, 0.9f) },
        { "Saw Lead", makePatch(WaveSaw, WaveSaw, 0.6f, 0, 6, 3500, 0.35f, 2.0f,
                                0.005f, 0.25f, 0.3f, 0.2f, 0.005f, 0.2f, 0.8f, 0.15f, 5.5f, 0.12f, 0, 0.2f, 0.2f, 0.8f) },
        { "Square Bass", makePatch(WaveSquare, WaveSine, 0.7f, -12, 0, 700, 0.3f, 2.5f,
                                   0.002f, 0.18f, 0.0f, 0.1f, 0.002f, 0.3f, 0.6f, 0.08f, 5, 0, 0, 0.3f, 0, 0.9f) },
        { "Pluck", makePatch(WaveTriangle, WaveSaw, 0.3f, 12, 0, 1200, 0.25f, 3.0f,
                             0.001f, 0.18f, 0.0f, 0.2f, 0.001f, 0.45f, 0.0f, 0.3f, 5, 0, 0, 0, 0.3f, 1.0f) },
        { "Synth Brass", makePatch(WaveSaw, WaveSaw, 0.8f, 0, -7, 900, 0.15f, 2.5f,
                                   0.08f, 0.5f, 0.5f, 0.3f, 0.05f, 0.3f, 0.85f, 0.25f, 5, 0, 0, 0.1f, 0.2f, 0.8f) },
        { "Wobble Bass", makePatch(WaveSaw, WaveSquare, 0.5f, -12, 0, 500, 0.6f, 0.0f,
                                   0.01f, 0.3f, 1.0f, 0.3f, 0.003f, 0.1f, 1.0f, 0.1f, 3.0f, 0, 2.0f, 0.35f, 0, 0.8f) },
        { "Glass Bell", makePatch(WaveSine, WaveSine, 0.5f, 19, 0, 20000, 0, 0,
                                  0.01f, 0.3f, 1.0f, 0.3f, 0.001f, 1.5f, 0.0f, 1.5f, 5, 0, 0, 0, 0.25f, 1.1f) },
        { "Soft Organ", makePatch(WaveSine, WaveSine, 0.6f, 12, 0, 20000, 0, 0,
                                  0.01f, 0.3f, 1.0f, 0.3f, 0.005f, 0.1f, 1.0f, 0.08f, 6.0f, 0.05f, 0, 0.05f, 0.45f, 0.9f) },
        { "Chip Lead", makePatch(WaveSquare, WaveSquare, 0.4f, 12, 0, 20000, 0, 0,
                                 0.01f, 0.3f, 1.0f, 0.3f, 0.002f, 0.1f, 0.7f, 0.05f, 6.0f, 0.15f, 0, 0, 0, 0.7f) },
    }};
    return patches;
}

inline const char* synthWaveName(int wave)
{
    static constexpr const char* names[] { "Sine", "Triangle", "Saw", "Square" };
    return wave >= 0 && wave < numSynthWaves ? names[wave] : "Sine";
}
}
