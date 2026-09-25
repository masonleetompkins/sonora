#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace sonora
{
// Offline monophonic pitch analysis and correction for vocal takes.
// Everything here runs on worker threads (allocation allowed); nothing here
// touches the audio thread. Correction is nondestructive: it renders a new
// buffer that the caller stores as a separate take.

// Key/scale quantizer. Chromatic snaps to the nearest semitone; major/minor
// snap to the nearest scale degree of the given key.
struct PitchScale
{
    int key = 0; // 0=C .. 11=B, pitch class of the tonic
    int scale = 0; // 0=chromatic, 1=major, 2=natural minor
    // Nearest target MIDI note (fractional input allowed) for a detected pitch.
    double quantize(double midiNote) const;
    bool valid() const { return key >= 0 && key < 12 && scale >= 0 && scale <= 2; }
    bool operator==(const PitchScale&) const = default;
};

struct CorrectionSettings
{
    PitchScale pitchScale;
    float amount = 1.0f; // 0 = dry, 1 = full snap
    float speedMs = 40.0f; // retune smoothing time constant
    bool valid() const
    {
        return pitchScale.valid() && std::isfinite(amount) && std::isfinite(speedMs)
            && amount >= 0.0f && amount <= 1.0f && speedMs >= 5.0f && speedMs <= 500.0f;
    }
    bool operator==(const CorrectionSettings&) const = default;
};

// YIN fundamental-frequency estimate per hop. f0Hz <= 0 means unvoiced.
struct PitchFrame
{
    float f0Hz = 0.0f;
    float confidence = 0.0f; // 1 - normalized YIN minimum (higher is better)
    bool voiced = false;
};

struct PitchContour
{
    std::vector<PitchFrame> frames;
    int hopSamples = 256;
    double sampleRate = 48000.0;
    bool empty() const { return frames.empty(); }
};

class YinDetector
{
public:
    YinDetector(double sampleRate, int windowSize = 2048, int hopSize = 256,
                float minHz = 55.0f, float maxHz = 880.0f, float threshold = 0.10f);
    PitchContour analyze(const float* mono, int numSamples,
                         std::function<bool(double)> progress = {}) const;

private:
    double rate;
    int window, hop;
    float minHz, maxHz, threshold;
};

// Shift trajectory in semitones per hop, smoothed by the speed setting.
std::vector<float> correctionTrajectory(const PitchContour& contour, const CorrectionSettings& settings);

// Pitch-synchronous overlap-add resynthesis. Identity (all-zero trajectory)
// reproduces the input; unvoiced hops are copied through untouched.
void psolaShift(const float* input, int numSamples, const PitchContour& contour,
                const std::vector<float>& shiftSemitones, float* output);

// Convenience: analyze then correct in one call.
std::vector<float> correctTake(const float* mono, int numSamples, double sampleRate,
                               const CorrectionSettings& settings, PitchContour* contourOut = nullptr,
                               std::function<bool(double)> progress = {});

inline double hzToMidi(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }
inline double midiToHz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }
}
