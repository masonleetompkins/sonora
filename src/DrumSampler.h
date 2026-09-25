#pragma once
#include "Pattern.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace sonora
{
// One immutable set of per-pad PCM. Built on the message thread (generation,
// file IO, and resampling allowed), then handed to the audio thread with RCU
// semantics: the sampler only loads the pointer, never frees it. A null bank
// means "play the built-in starter kit".
struct SampleBank
{
    std::array<std::vector<float>, drumPads> data;
    double sampleRate = 48000.0;
};

// Deterministic starter kit shared by the sampler and the bank loader.
// Variants re-voice the same recipes: pitch scales oscillator frequencies,
// decay stretches envelopes and pad lengths, seed re-rolls the noise.
struct KitVariant
{
    double pitch = 1.0, decay = 1.0;
    std::uint32_t seed = 0;
};

inline const char* kitVariantName(int variant)
{
    switch (variant)
    {
        case 1: return "Deep";
        case 2: return "Crisp";
        default: return "Starter";
    }
}
inline KitVariant kitVariantParams(int variant)
{
    switch (variant)
    {
        case 1: return { 0.65, 1.5, 0x1111u };
        case 2: return { 1.5, 0.6, 0x2222u };
        default: return { 1.0, 1.0, 0u };
    }
}

SampleBank buildStarterBank(KitVariant variant = {});

// Peak target per pad when normalizing custom samples.
inline float padLevelTarget(int pad)
{
    return pad == 0 || pad == 5 ? 0.65f : pad == 2 || pad == 3 || pad == 7 ? 0.3f : 0.5f;
}

// Sampled one-shot drum engine. Voices hold pad/position only, so a bank swap
// under playing voices degrades to early voice release, never torn reads.
class DrumSampler
{
public:
    DrumSampler();
    void prepare(double sampleRate);
    void stop();
    void trigger(int pad, float velocity);
    void render(juce::AudioBuffer<float>& buffer, int start, int count,
                const juce::MidiBuffer& midi, float targetGain);
    const std::vector<float>& sample(int pad) const
    {
        return builtin.data[static_cast<std::size_t>(pad)];
    }
    // Installs a bank built on the message thread; returns the previous bank
    // for grace-period deletion by the caller. Null restores built-ins.
    const SampleBank* requestBank(const SampleBank* next)
    {
        return bank.exchange(next, std::memory_order_acq_rel);
    }

private:
    void renderRange(juce::AudioBuffer<float>&, int start, int count,
                     const SampleBank& active, double increment);
    struct Voice
    {
        int pad = -1;
        double position = 0.0;
        float velocity = 0.0f;
        int chokeRemaining = -1;
        std::uint64_t age = 0;
    };
    SampleBank builtin;
    std::atomic<const SampleBank*> bank { nullptr };
    std::array<Voice, 64> voices;
    double deviceRate = 48000.0;
    int chokeSamples = 96;
    std::uint64_t age = 0;
    juce::SmoothedValue<float> gain;
};
}
