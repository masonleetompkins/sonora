#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace sonora
{
class Sound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

// A deliberately small, allocation-free sine voice for the first audio milestone.
class Voice final : public juce::SynthesiserVoice
{
public:
    using juce::SynthesiserVoice::renderNextBlock;
    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<Sound*>(sound) != nullptr;
    }

    void startNote(int note, float velocity, juce::SynthesiserSound*, int) override
    {
        phase = 0.0;
        lfoPhase = 0.0;
        lfoIncrement = juce::MathConstants<double>::twoPi * 5.5 / getSampleRate();
        increment = juce::MathConstants<double>::twoPi
            * juce::MidiMessage::getMidiNoteInHertz(note) / getSampleRate();
        amplitude = velocity * 0.12f;
        envelope.setSampleRate(getSampleRate());
        envelope.setParameters({ 0.01f, 0.12f, 0.7f, 0.25f });
        envelope.noteOn();
    }

    void stopNote(float, bool allowTail) override
    {
        if (allowTail)
            envelope.noteOff();
        else
        {
            envelope.reset();
            clearCurrentNote();
        }
    }
    // MiniLab pitch strip: 14-bit wheel centered on 8192 bends +/-2 semitones.
    // Only voices already playing on the wheel's channel are affected (JUCE
    // core routing), so sequenced patterns never detune.
    void pitchWheelMoved(int newValue) override
    {
        bendSemis = (static_cast<float>(newValue) - 8192.0f) / 8192.0f * 2.0f;
    }
    // MiniLab mod strip (CC1): 5.5 Hz vibrato up to +/-0.5 semitones.
    // Sustain (CC64) is held by the JUCE Synthesiser core, not here.
    void controllerMoved(int number, int value) override
    {
        if (number == 1)
            modDepth = juce::jlimit(0.0f, 1.0f, static_cast<float>(value) / 127.0f);
    }

    void renderNextBlock(juce::AudioBuffer<float>& buffer, int start, int count) override
    {
        // Bend is a static multiplier; vibrato modulates around it.
        const auto bendMult = std::pow(2.0f, bendSemis / 12.0f);
        for (int i = 0; i < count; ++i)
        {
            lfoPhase += lfoIncrement;
            if (lfoPhase >= juce::MathConstants<double>::twoPi)
                lfoPhase -= juce::MathConstants<double>::twoPi;
            const auto vibrato = 1.0f + modDepth * 0.029f * static_cast<float>(std::sin(lfoPhase));
            const auto sample = static_cast<float>(std::sin(phase))
                * amplitude * envelope.getNextSample();
            phase += increment * bendMult * vibrato;
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
    juce::ADSR envelope;
    double phase = 0.0, increment = 0.0, lfoPhase = 0.0, lfoIncrement = 0.0;
    float amplitude = 0.0f, bendSemis = 0.0f, modDepth = 0.0f;
};
}
