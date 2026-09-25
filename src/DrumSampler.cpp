#include "DrumSampler.h"
#include <cmath>

namespace sonora
{
namespace
{
constexpr double sampleRate = 48000.0;
constexpr double tau = juce::MathConstants<double>::twoPi;

// Deterministic local noise source; no external recordings or sound assets.
float noise(std::uint32_t& seed)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<float>(seed & 0xffffffu) / 8388607.5f - 1.0f;
}
}

SampleBank buildStarterBank(KitVariant variant)
{
    SampleBank bank;
    bank.sampleRate = sampleRate;
    constexpr std::array<double, drumPads> baseLengths { 0.75, 0.38, 0.12, 0.75, 0.32, 0.65, 0.14, 0.18 };
    for (int pad = 0; pad < drumPads; ++pad)
    {
        auto& data = bank.data[static_cast<std::size_t>(pad)];
        const auto length = baseLengths[static_cast<std::size_t>(pad)] * variant.decay;
        data.resize(static_cast<std::size_t>(std::ceil(length * sampleRate)));
        std::uint32_t seed = 0x5a17u + static_cast<std::uint32_t>(pad) * 137u + variant.seed;
        double phase = 0.0;
        float lowNoise = 0.0f;
        const auto pitch = variant.pitch, decay = variant.decay;
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            const auto t = static_cast<double>(i) / sampleRate;
            const auto white = noise(seed);
            lowNoise += 0.28f * (white - lowNoise);
            const auto high = white - lowNoise;
            double value = 0.0;
            switch (pad)
            {
                case 0:
                    phase += tau * (48.0 * pitch + 115.0 * pitch * std::exp(-t * 55.0 / decay)) / sampleRate;
                    value = std::sin(phase) * std::exp(-t * 8.0 / decay)
                        + 0.15 * high * std::exp(-t * 160.0 / decay);
                    break;
                case 1:
                    value = 0.32 * std::sin(tau * 185.0 * pitch * t) * std::exp(-t * 24.0 / decay)
                          + 0.85 * high * std::exp(-t * 18.0 / decay);
                    break;
                case 2:
                case 3:
                    value = (0.65 * high + 0.1 * std::sin(tau * 7313.0 * pitch * t))
                          * std::exp(-t * (pad == 2 ? 65.0 : 8.0) / decay);
                    break;
                case 4:
                {
                    const auto burst = t < 0.033 ? std::exp(-std::fmod(t, 0.011) * 260.0)
                                                : std::exp(-(t - 0.033) * 24.0) * 0.6;
                    value = high * burst;
                    break;
                }
                case 5:
                    phase += tau * (92.0 * pitch + 75.0 * pitch * std::exp(-t * 32.0 / decay)) / sampleRate;
                    value = (std::sin(phase) + 0.22 * std::sin(phase * 1.51)) * std::exp(-t * 9.0 / decay);
                    break;
                case 6:
                    value = (0.6 * std::sin(tau * 830.0 * pitch * t) + 0.4 * std::sin(tau * 1730.0 * pitch * t)
                             + 0.25 * high) * std::exp(-t * 60.0 / decay);
                    break;
                case 7:
                    value = high * (1.0 - std::exp(-t * 400.0)) * std::exp(-t * 35.0 / decay);
                    break;
                default: break;
            }
            const auto fadeIn = std::min(1.0, t * 2000.0);
            const auto fadeOut = std::min(1.0, (length - t) * 200.0);
            data[i] = static_cast<float>(value * fadeIn * fadeOut);
        }
        float peak = 0.0f;
        for (const auto value : data)
            peak = std::max(peak, std::abs(value));
        const float target = padLevelTarget(pad);
        if (peak > 0.0f)
            for (auto& value : data)
                value *= target / peak;
    }
    return bank;
}

DrumSampler::DrumSampler() : builtin(buildStarterBank())
{
    prepare(sampleRate);
}

void DrumSampler::prepare(double rate)
{
    deviceRate = rate;
    chokeSamples = std::max(1, static_cast<int>(rate * 0.002));
    gain.reset(rate, 0.01);
    gain.setCurrentAndTargetValue(0.8f);
    stop();
}

void DrumSampler::stop()
{
    for (auto& voice : voices)
        voice.pad = -1;
}

void DrumSampler::trigger(int pad, float velocity)
{
    if (pad < 0 || pad >= drumPads || velocity <= 0.0f)
        return;
    // Both hi-hat pads share a choke group. A brief fade avoids a hard cut.
    if (pad == 2 || pad == 3)
        for (auto& voice : voices)
            if ((voice.pad == 2 || voice.pad == 3) && voice.chokeRemaining < 0)
                voice.chokeRemaining = chokeSamples;
    auto* chosen = &voices[0];
    for (auto& voice : voices)
    {
        if (voice.pad < 0)
        {
            chosen = &voice;
            break;
        }
        if (voice.age < chosen->age)
            chosen = &voice;
    }
    *chosen = { pad, 0.0, juce::jlimit(0.0f, 1.0f, velocity), -1, ++age };
}

void DrumSampler::renderRange(juce::AudioBuffer<float>& buffer, int start, int count,
                               const SampleBank& active, double increment)
{
    for (int frame = start; frame < start + count; ++frame)
    {
        float sum = 0.0f;
        for (auto& voice : voices)
        {
            if (voice.pad < 0)
                continue;
            const auto& data = active.data[static_cast<std::size_t>(voice.pad)];
            const auto index = static_cast<std::size_t>(voice.position);
            if (index + 1 >= data.size() || voice.chokeRemaining == 0)
            {
                voice.pad = -1;
                continue;
            }
            const auto fraction = static_cast<float>(voice.position - static_cast<double>(index));
            auto value = data[index] + fraction * (data[index + 1] - data[index]);
            if (voice.chokeRemaining > 0)
                value *= static_cast<float>(voice.chokeRemaining--) / static_cast<float>(chokeSamples);
            sum += value * voice.velocity;
            voice.position += increment;
        }
        sum *= gain.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addSample(channel, frame, sum);
    }
}

void DrumSampler::render(juce::AudioBuffer<float>& buffer, int start, int count,
                         const juce::MidiBuffer& midi, float targetGain)
{
    gain.setTargetValue(targetGain);
    const auto* custom = bank.load(std::memory_order_acquire);
    const auto& active = custom != nullptr ? *custom : builtin;
    const double increment = active.sampleRate / deviceRate;
    auto cursor = start;
    const auto end = start + count;
    for (const auto metadata : midi)
    {
        const auto position = metadata.samplePosition;
        if (position < start || position >= end)
            continue;
        renderRange(buffer, cursor, position - cursor, active, increment);
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            trigger(message.getNoteNumber() - drumBaseNote, message.getFloatVelocity());
        cursor = position;
    }
    renderRange(buffer, cursor, end - cursor, active, increment);
}
}
