#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace sonora
{
// Realtime-safe building blocks: prepare() once (may allocate), then process()
// never allocates, locks, or touches the UI. All parameter changes are applied
// through precomputed coefficients or smoothed values set outside process().

struct EqParams
{
    float low = 0.0f, mid = 0.0f, midFreq = 1200.0f, high = 0.0f; // gains in dB
    bool enabled = true;
    bool operator==(const EqParams&) const = default;
    bool valid() const
    {
        return std::isfinite(low) && std::isfinite(mid) && std::isfinite(high) && std::isfinite(midFreq)
            && low >= -15.0f && low <= 15.0f && mid >= -15.0f && mid <= 15.0f
            && high >= -15.0f && high <= 15.0f && midFreq >= 200.0f && midFreq <= 8000.0f;
    }
};

struct CompParams
{
    float thresholdDb = 0.0f, ratio = 1.0f, attackMs = 10.0f, releaseMs = 120.0f;
    bool enabled = true;
    bool operator==(const CompParams&) const = default;
    bool valid() const
    {
        return std::isfinite(thresholdDb) && std::isfinite(ratio) && std::isfinite(attackMs) && std::isfinite(releaseMs)
            && thresholdDb >= -40.0f && thresholdDb <= 0.0f && ratio >= 1.0f && ratio <= 12.0f
            && attackMs >= 0.5f && attackMs <= 100.0f && releaseMs >= 20.0f && releaseMs <= 1000.0f;
    }
};

struct DelayParams
{
    float timeMs = 375.0f, feedback = 0.3f, mix = 0.0f;
    bool enabled = true;
    bool operator==(const DelayParams&) const = default;
    bool valid() const
    {
        return std::isfinite(timeMs) && std::isfinite(feedback) && std::isfinite(mix)
            && timeMs >= 20.0f && timeMs <= 1000.0f && feedback >= 0.0f && feedback <= 0.8f
            && mix >= 0.0f && mix <= 1.0f;
    }
};

struct ReverbParams
{
    float size = 0.6f, damping = 0.5f, mix = 0.0f;
    bool enabled = true;
    bool operator==(const ReverbParams&) const = default;
    bool valid() const
    {
        return std::isfinite(size) && std::isfinite(damping) && std::isfinite(mix)
            && size >= 0.0f && size <= 1.0f && damping >= 0.0f && damping <= 1.0f
            && mix >= 0.0f && mix <= 1.0f;
    }
};

// Overdrive: tanh waveshaper with loudness compensation and a post tone
// low-pass. Amount 0 bypasses the stage entirely (bit-exact).
struct DriveParams
{
    float amount = 0.0f, tone = 12000.0f; // 0..1, Hz
    bool enabled = true;
    bool operator==(const DriveParams&) const = default;
    bool valid() const
    {
        return std::isfinite(amount) && std::isfinite(tone)
            && amount >= 0.0f && amount <= 1.0f && tone >= 800.0f && tone <= 16000.0f;
    }
};

// Stereo chorus: two modulated delay taps 90 degrees apart. Mix 0 bypasses.
struct ChorusParams
{
    float mix = 0.0f, rate = 0.7f, depth = 0.5f; // 0..1, Hz, 0..1
    bool enabled = true;
    bool operator==(const ChorusParams&) const = default;
    bool valid() const
    {
        return std::isfinite(mix) && std::isfinite(rate) && std::isfinite(depth)
            && mix >= 0.0f && mix <= 1.0f && rate >= 0.1f && rate <= 5.0f && depth >= 0.0f && depth <= 1.0f;
    }
};

struct LimiterParams
{
    float ceilingDb = -0.5f, releaseMs = 80.0f;
    bool enabled = true;
    bool operator==(const LimiterParams&) const = default;
    bool valid() const
    {
        return std::isfinite(ceilingDb) && std::isfinite(releaseMs)
            && ceilingDb >= -12.0f && ceilingDb <= 0.0f && releaseMs >= 20.0f && releaseMs <= 500.0f;
    }
};

struct TrackFx
{
    EqParams eq;
    CompParams comp;
    DelayParams delay;
    ReverbParams reverb;
    DriveParams drive;
    ChorusParams chorus;
    bool operator==(const TrackFx&) const = default;
    bool valid() const
    {
        return eq.valid() && comp.valid() && delay.valid() && reverb.valid() && drive.valid() && chorus.valid();
    }
};

class ThreeBandEq
{
public:
    void prepare(double sampleRate);
    void setParams(const EqParams& params);
    void process(float* left, float* right, int count);
    void reset();

private:
    void updateFilters();
    juce::IIRFilter lowL, lowR, midL, midR, highL, highR;
    EqParams current;
    double rate = 48000.0;
};

class Compressor
{
public:
    void prepare(double sampleRate);
    void setParams(const CompParams& params);
    void process(float* left, float* right, int count);
    void reset();

private:
    CompParams current;
    double envelope = 0.0, attackCoeff = 0.0, releaseCoeff = 0.0;
    juce::SmoothedValue<float> makeup;
    double rate = 48000.0;
};

class TempoDelay
{
public:
    void prepare(double sampleRate);
    void setParams(const DelayParams& params);
    void process(float* left, float* right, int count);
    void reset();

private:
    juce::AudioBuffer<float> line { 2, 96000 };
    int writePos = 0;
    DelayParams current;
    juce::SmoothedValue<float> timeSmooth, feedbackSmooth, mixSmooth;
    double rate = 48000.0;
};

class SimpleReverb
{
public:
    void prepare(double sampleRate);
    void setParams(const ReverbParams& params);
    void process(float* left, float* right, int count);
    void reset();

private:
    juce::Reverb reverb;
    juce::Reverb::Parameters juniper;
    ReverbParams current;
    juce::SmoothedValue<float> mixSmooth;
    float wetL[2048] {}, wetR[2048] {};
};

class Overdrive
{
public:
    void prepare(double sampleRate);
    void setParams(const DriveParams& params);
    void process(float* left, float* right, int count);
    void reset();

private:
    DriveParams current;
    juce::SmoothedValue<float> amountSmooth;
    float toneCoeff = 1.0f, toneL = 0.0f, toneR = 0.0f, tone2L = 0.0f, tone2R = 0.0f;
    double rate = 48000.0;
};

class StereoChorus
{
public:
    void prepare(double sampleRate);
    void setParams(const ChorusParams& params);
    void process(float* left, float* right, int count);
    void reset();

private:
    std::vector<float> lineL, lineR;
    ChorusParams current;
    juce::SmoothedValue<float> mixSmooth;
    double rate = 48000.0, lfo = 0.0;
    int write = 0;
};

class BrickLimiter
{
public:
    void prepare(double sampleRate);
    void setParams(const LimiterParams& params);
    void process(float* left, float* right, int count);
    void reset();
    float getReductionDb() const { return reductionDb.load(); }

private:
    LimiterParams current;
    double envelope = 0.0, releaseCoeff = 0.0, ceiling = 1.0;
    std::atomic<float> reductionDb { 0.0f };
    double rate = 48000.0;
};

// Fixed track chain: drive -> EQ -> compressor -> chorus -> delay -> reverb. Mix knobs at zero
// make delay/reverb transparent; flat EQ and 1:1 compression pass audio
// through untouched, so default projects sound exactly as before.
class TrackChain
{
public:
    void prepare(double sampleRate);
    void setParams(const TrackFx& params);
    void process(juce::AudioBuffer<float>& buffer, int start, int count);
    void reset();

private:
    Overdrive drive;
    ThreeBandEq eq;
    Compressor comp;
    StereoChorus chorus;
    TempoDelay delay;
    SimpleReverb reverb;
    TrackFx current;
};
}
