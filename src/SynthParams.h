#pragma once
#include <array>
#include <cmath>

namespace sonora
{
// Editable subtractive synth for "Sine Keys" (instrument 0). Plain floats so
// ProjectState stays trivially copyable for the realtime snapshot queue.
// Defaults reproduce the original fixed sine voice exactly.
// Pulse (25% duty) and Noise were appended later; ids are persisted, so keep
// adding at the end.
enum SynthWave : int { WaveSine = 0, WaveTriangle, WaveSaw, WaveSquare, WavePulse, WaveNoise, numSynthWaves };

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
inline const std::array<SynthPatch, 26>& synthPatches()
{
    static const std::array<SynthPatch, 26> patches {{
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
        // Added in 0.34 (some use the Pulse and Noise oscillators).
        { "Super Saw", makePatch(WaveSaw, WaveSaw, 0.9f, 0, 14, 6000, 0.1f, 0.5f,
                                 0.01f, 0.4f, 0.8f, 0.3f, 0.01f, 0.2f, 0.9f, 0.3f, 5, 0, 0, 0.1f, 0.7f, 0.75f) },
        { "Reese Bass", makePatch(WaveSaw, WaveSaw, 1.0f, 0, 18, 450, 0.3f, 0.5f,
                                  0.01f, 0.6f, 0.5f, 0.2f, 0.005f, 0.2f, 1.0f, 0.15f, 0.5f, 0, 1.0f, 0.3f, 0.3f, 0.8f) },
        { "Sub Bass", makePatch(WaveSine, WaveTriangle, 0.4f, -12, 0, 400, 0, 0,
                                0.01f, 0.3f, 1.0f, 0.3f, 0.005f, 0.1f, 1.0f, 0.12f, 5, 0, 0, 0.1f, 0, 1.0f) },
        { "Acid Bass", makePatch(WaveSaw, WaveSaw, 0.0f, 0, 0, 350, 0.8f, 3.0f,
                                 0.001f, 0.22f, 0.0f, 0.1f, 0.001f, 0.25f, 0.7f, 0.06f, 5, 0, 0, 0.4f, 0, 0.75f) },
        { "Electric Keys", makePatch(WaveTriangle, WaveSine, 0.5f, 12, 0, 5000, 0, 1.0f,
                                     0.001f, 0.5f, 0.3f, 0.2f, 0.002f, 0.9f, 0.35f, 0.35f, 4.5f, 0, 0, 0, 0.5f, 1.0f) },
        { "Vibes", makePatch(WaveSine, WaveSine, 0.3f, 12, 0, 20000, 0, 0,
                             0.01f, 0.3f, 1.0f, 0.3f, 0.002f, 1.8f, 0.0f, 1.2f, 5.5f, 0.04f, 0, 0, 0.3f, 1.0f) },
        { "Dream Pad", makePatch(WaveSaw, WaveTriangle, 0.7f, 7, 8, 1400, 0.15f, 1.2f,
                                 1.5f, 2.0f, 0.6f, 1.5f, 1.2f, 1.0f, 0.85f, 2.0f, 0.25f, 0, 0.8f, 0, 0.8f, 0.85f) },
        { "Vox Pad", makePatch(WaveTriangle, WaveSine, 0.6f, 12, 0, 1600, 0.2f, 0.5f,
                               0.5f, 0.8f, 0.7f, 0.8f, 0.5f, 0.5f, 0.9f, 1.0f, 5.2f, 0.1f, 0, 0, 0.6f, 0.95f) },
        { "String Machine", makePatch(WaveSaw, WaveSaw, 0.7f, 0, 12, 2600, 0.05f, 0.4f,
                                      0.3f, 0.5f, 0.8f, 0.5f, 0.35f, 0.4f, 0.9f, 0.6f, 5.8f, 0.06f, 0, 0, 0.9f, 0.8f) },
        { "Pulse Pluck", makePatch(WavePulse, WaveTriangle, 0.4f, 12, 0, 2500, 0.3f, 2.5f,
                                   0.001f, 0.15f, 0.0f, 0.1f, 0.001f, 0.3f, 0.0f, 0.2f, 5, 0, 0, 0, 0.3f, 0.9f) },
        { "Pulse Lead", makePatch(WavePulse, WavePulse, 0.6f, 0, 8, 5000, 0.2f, 1.0f,
                                  0.01f, 0.3f, 0.8f, 0.2f, 0.005f, 0.2f, 0.8f, 0.15f, 5.5f, 0.1f, 0, 0.1f, 0.3f, 0.75f) },
        { "Ocean Wind", makePatch(WaveNoise, WaveSine, 0.0f, 0, 0, 900, 0.5f, 0.0f,
                                  0.01f, 0.3f, 1.0f, 0.3f, 1.5f, 1.0f, 1.0f, 1.5f, 0.3f, 0, 1.2f, 0, 0.3f, 0.7f) },
        { "Noise Hit", makePatch(WaveNoise, WaveTriangle, 0.5f, 0, 0, 3000, 0.3f, 2.0f,
                                 0.001f, 0.1f, 0.0f, 0.1f, 0.001f, 0.15f, 0.0f, 0.1f, 5, 0, 0, 0, 0, 0.9f) },
        { "Whistle", makePatch(WaveSine, WaveSine, 0.15f, 12, 0, 20000, 0, 0,
                               0.01f, 0.3f, 1.0f, 0.3f, 0.05f, 0.1f, 1.0f, 0.2f, 5.5f, 0.25f, 0, 0, 0.2f, 0.9f) },
        { "Synth Stab", makePatch(WaveSaw, WaveSaw, 0.6f, 0, 4, 700, 0.15f, 3.0f,
                                  0.005f, 0.25f, 0.3f, 0.15f, 0.005f, 0.25f, 0.5f, 0.15f, 5, 0, 0, 0.1f, 0.25f, 0.8f) },
        { "Riser", makePatch(WaveNoise, WaveSaw, 0.3f, 0, 0, 300, 0.5f, 4.0f,
                             2.0f, 1.0f, 1.0f, 0.5f, 2.0f, 0.5f, 1.0f, 0.8f, 5, 0, 0, 0.1f, 0.4f, 0.7f) },
    }};
    return patches;
}

inline const char* synthWaveName(int wave)
{
    static constexpr const char* names[] { "Sine", "Triangle", "Saw", "Square", "Pulse", "Noise" };
    return wave >= 0 && wave < numSynthWaves ? names[wave] : "Sine";
}
}
