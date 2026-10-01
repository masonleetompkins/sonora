#pragma once
#include "SynthParams.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sonora
{
class Sound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

// Allocation-free subtractive voice: two oscillators (polyBLEP saw/square),
// drive, TPT state-variable low-pass with its own envelope, amp envelope, and
// an LFO for vibrato/filter wobble. Parameters are read from the owning
// track's shared SynthParams, which the audio thread updates between blocks.
// With default parameters this renders exactly like the original sine voice.
class Voice final : public juce::SynthesiserVoice
{
public:
    explicit Voice(const SynthParams* shared = nullptr) : params(shared != nullptr ? shared : &fallback) {}
    using juce::SynthesiserVoice::renderNextBlock;
    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<Sound*>(sound) != nullptr;
    }

    void startNote(int note, float velocity, juce::SynthesiserSound*, int) override
    {
        const auto rate = getSampleRate();
        phase = 0.0;
        phase2 = 0.0;
        // Deterministic per-note noise seed: renders are reproducible.
        noiseState = 0x9E3779B9u ^ (static_cast<std::uint32_t>(note) * 2654435761u);
        if (noiseState == 0)
            noiseState = 1;
        lfoPhase = 0.0;
        synthLfoPhase = 0.0;
        ic1 = ic2 = 0.0f;
        coefficientCountdown = 0;
        lfoIncrement = juce::MathConstants<double>::twoPi * 5.5 / rate;
        noteHz = juce::MidiMessage::getMidiNoteInHertz(note);
        increment = juce::MathConstants<double>::twoPi * noteHz / rate;
        amplitude = velocity * 0.12f;
        envelope.setSampleRate(rate);
        filterEnvelope.setSampleRate(rate);
        updateEnvelopes();
        envelope.noteOn();
        filterEnvelope.noteOn();
    }

    void stopNote(float, bool allowTail) override
    {
        if (allowTail)
        {
            envelope.noteOff();
            filterEnvelope.noteOff();
        }
        else
        {
            envelope.reset();
            filterEnvelope.reset();
            clearCurrentNote();
        }
    }

    // Re-read envelope times after a live edit; held notes keep sounding.
    void updateEnvelopes()
    {
        const auto& p = *params;
        envelope.setParameters({ p.attack, p.decay, p.sustain, p.release });
        filterEnvelope.setParameters({ p.filterAttack, p.filterDecay, p.filterSustain, p.filterRelease });
    }

    // MiniLab pitch strip: 14-bit wheel centered on 8192 bends +/-2 semitones.
    void pitchWheelMoved(int newValue) override
    {
        bendSemis = (static_cast<float>(newValue) - 8192.0f) / 8192.0f * 2.0f;
    }
    // MiniLab mod strip (CC1): 5.5 Hz vibrato up to +/-0.5 semitones.
    void controllerMoved(int number, int value) override
    {
        if (number == 1)
            modDepth = juce::jlimit(0.0f, 1.0f, static_cast<float>(value) / 127.0f);
    }

    void renderNextBlock(juce::AudioBuffer<float>& buffer, int start, int count) override
    {
        const auto& p = *params;
        const auto rate = getSampleRate();
        const auto bendMult = std::pow(2.0f, bendSemis / 12.0f);
        const bool useOsc2 = p.mix2 > 0.0f;
        const bool useFilter = p.filterActive();
        const bool useLfo = p.lfoPitch > 0.0f || p.lfoFilter > 0.0f;
        const double ratio2 = std::pow(2.0, (p.semis2 + p.detune2 / 100.0) / 12.0);
        const double synthLfoIncrement = juce::MathConstants<double>::twoPi * p.lfoRate / rate;
        const float driveGain = 1.0f + p.drive * 24.0f;
        const float driveNorm = 1.0f / std::pow(driveGain, 0.6f);
        const float damping = 2.0f - 2.0f * p.resonance;
        for (int i = 0; i < count; ++i)
        {
            lfoPhase += lfoIncrement;
            if (lfoPhase >= juce::MathConstants<double>::twoPi)
                lfoPhase -= juce::MathConstants<double>::twoPi;
            float pitchMult = 1.0f + modDepth * 0.029f * static_cast<float>(std::sin(lfoPhase));
            float lfoValue = 0.0f;
            if (useLfo)
            {
                synthLfoPhase += synthLfoIncrement;
                if (synthLfoPhase >= juce::MathConstants<double>::twoPi)
                    synthLfoPhase -= juce::MathConstants<double>::twoPi;
                lfoValue = static_cast<float>(std::sin(synthLfoPhase));
                if (p.lfoPitch > 0.0f)
                    pitchMult *= std::pow(2.0f, p.lfoPitch * lfoValue / 12.0f);
            }
            const auto step = increment * bendMult * pitchMult;
            float sample = voiceOscillator(p.wave, phase, step);
            if (useOsc2)
            {
                sample += p.mix2 * voiceOscillator(p.wave2, phase2, step * ratio2);
                phase2 += step * ratio2;
                if (phase2 >= juce::MathConstants<double>::twoPi)
                    phase2 = std::fmod(phase2, juce::MathConstants<double>::twoPi);
            }
            if (p.drive > 0.0f)
                sample = std::tanh(sample * driveGain) * driveNorm;
            if (useFilter)
            {
                const float filterEnv = filterEnvelope.getNextSample();
                if (--coefficientCountdown <= 0)
                {
                    coefficientCountdown = 8;
                    const float octaves = p.envAmount * filterEnv + p.lfoFilter * lfoValue;
                    const float cutoff = juce::jlimit(20.0f, static_cast<float>(rate) * 0.45f,
                                                      p.cutoff * std::pow(2.0f, octaves));
                    const float g = std::tan(juce::MathConstants<float>::pi * cutoff / static_cast<float>(rate));
                    a1 = 1.0f / (1.0f + g * (g + damping));
                    a2 = g * a1;
                    a3 = g * a2;
                }
                const float v3 = sample - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                sample = v2;
            }
            sample = sample * amplitude * envelope.getNextSample();
            if (p.level < 1.0f || p.level > 1.0f)
                sample *= p.level;
            phase += step;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase = std::fmod(phase, juce::MathConstants<double>::twoPi);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.addSample(channel, start + i, sample);
            if (!envelope.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    static float polyBlep(double t, double dt)
    {
        if (t < dt)
        {
            t /= dt;
            return static_cast<float>(t + t - t * t - 1.0);
        }
        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return static_cast<float>(t * t + t + t + 1.0);
        }
        return 0.0f;
    }

    // White noise (xorshift32), scaled to sit with the other oscillators.
    float nextNoise()
    {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return 0.5f * (static_cast<float>(noiseState) * (2.0f / 4294967296.0f) - 1.0f);
    }

    float voiceOscillator(int wave, double phaseRadians, double step)
    {
        return wave == WaveNoise ? nextNoise() : oscillator(wave, phaseRadians, step);
    }

    // phase in radians [0, 2pi); step in radians per sample.
    static float oscillator(int wave, double phaseRadians, double step)
    {
        if (wave == WaveSine)
            return static_cast<float>(std::sin(phaseRadians));
        const double t = phaseRadians / juce::MathConstants<double>::twoPi;
        const double dt = juce::jlimit(1.0e-9, 0.5, step / juce::MathConstants<double>::twoPi);
        switch (wave)
        {
            case WaveTriangle: return static_cast<float>(4.0 * std::abs(t - 0.5) - 1.0);
            case WaveSaw: return 0.8f * (static_cast<float>(2.0 * t - 1.0) - polyBlep(t, dt));
            case WavePulse:
            {
                // 25% duty with the DC offset removed (a raw pulse leans -0.5).
                float pulse = t < 0.25 ? 1.0f : -1.0f;
                pulse += polyBlep(t, dt);
                pulse -= polyBlep(std::fmod(t + 0.75, 1.0), dt);
                return 0.7f * (pulse + 0.5f);
            }
            case WaveNoise: return 0.0f; // handled per voice (needs state)
            default:
            {
                float square = t < 0.5 ? 1.0f : -1.0f;
                square += polyBlep(t, dt);
                square -= polyBlep(std::fmod(t + 0.5, 1.0), dt);
                return 0.6f * square;
            }
        }
    }

    static inline const SynthParams fallback {};
    const SynthParams* params;
    juce::ADSR envelope, filterEnvelope;
    double phase = 0.0, phase2 = 0.0, increment = 0.0, lfoPhase = 0.0, lfoIncrement = 0.0;
    double synthLfoPhase = 0.0, noteHz = 440.0;
    std::uint32_t noiseState = 0x9E3779B9u;
    float amplitude = 0.0f, bendSemis = 0.0f, modDepth = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    int coefficientCountdown = 0;
};

// Stereo chorus for the synth track: two modulated delay taps, 90 degrees
// apart. prepare() allocates; process() does not. Bypassed at mix 0.
class Chorus
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        line.assign(static_cast<std::size_t>(sampleRate * 0.05) + 4, 0.0f);
        reset();
    }
    void reset()
    {
        std::fill(line.begin(), line.end(), 0.0f);
        write = 0;
        lfo = 0.0;
    }
    void process(juce::AudioBuffer<float>& buffer, int count, float mix)
    {
        if (mix <= 0.0f || line.empty() || buffer.getNumChannels() < 2)
            return;
        const auto size = static_cast<int>(line.size());
        const double increment = juce::MathConstants<double>::twoPi * 0.7 / rate;
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        for (int i = 0; i < count; ++i)
        {
            const float dry = 0.5f * (left[i] + right[i]);
            line[static_cast<std::size_t>(write)] = dry;
            auto tap = [&](double offset) {
                const double delay = rate * (0.012 + 0.004 * std::sin(lfo + offset));
                double read = write - delay;
                while (read < 0.0)
                    read += size;
                const int a = static_cast<int>(read) % size;
                const int b = (a + 1) % size;
                const float frac = static_cast<float>(read - std::floor(read));
                return line[static_cast<std::size_t>(a)] * (1.0f - frac) + line[static_cast<std::size_t>(b)] * frac;
            };
            const float wetLeft = tap(0.0), wetRight = tap(juce::MathConstants<double>::halfPi);
            left[i] = left[i] * (1.0f - 0.5f * mix) + wetLeft * 0.7f * mix;
            right[i] = right[i] * (1.0f - 0.5f * mix) + wetRight * 0.7f * mix;
            write = (write + 1) % size;
            lfo += increment;
            if (lfo >= juce::MathConstants<double>::twoPi)
                lfo -= juce::MathConstants<double>::twoPi;
        }
    }

private:
    std::vector<float> line;
    double rate = 48000.0, lfo = 0.0;
    int write = 0;
};
}
