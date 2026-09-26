#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
struct tsf;

namespace sonora
{
// Construct/prepare/destroy off the audio thread. Rendering and program changes
// use preallocated voices/channels and never read files or acquire locks.
class SampledInstrument
{
public:
    SampledInstrument();
    ~SampledInstrument();
    SampledInstrument(const SampledInstrument&) = delete;
    SampledInstrument& operator=(const SampledInstrument&) = delete;
    bool available() const { return synth != nullptr; }
    void prepare(double rate);
    void select(int preset);
    void stop();
    void render(juce::AudioBuffer<float>& output, int start, int count, const juce::MidiBuffer& events);
private:
    void renderRange(juce::AudioBuffer<float>& output, int start, int count);
    void message(const juce::MidiMessage& midi);
    tsf* synth = nullptr;
    int selected = -1;
};
}
