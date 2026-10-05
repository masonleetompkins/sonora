#include "SampledInstrument.h"
#include "Instruments.h"
#include <algorithm>
#include <mutex>
#include <utility>
#define TSF_IMPLEMENTATION
#include "tsf.h"

namespace sonora
{
namespace
{
struct Bank
{
    std::mutex mutex;
    tsf* data = nullptr;
    Bank()
    {
        const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        const juce::File paths[] {
            // macOS .app bundle: Contents/Resources (wired by CMake).
            executable.getParentDirectory().getParentDirectory().getChildFile("Resources/GeneralUser-GS.sf2"),
            executable.getParentDirectory().getChildFile("GeneralUser-GS.sf2"),
            executable.getParentDirectory().getParentDirectory().getChildFile("share/sonora/GeneralUser-GS.sf2"),
            juce::File(SONORA_SOUNDFONT_PATH)
        };
        for (const auto& path : paths)
            if (path.existsAsFile() && (data = tsf_load_filename(path.getFullPathName().toRawUTF8())) != nullptr)
                break;
    }
    ~Bank() { tsf_close(data); }
};
Bank& bank() { static Bank value; return value; }

// SoundFont presets only span their sampled key range; keys outside it get
// no voice and go silent. Transpose by octaves into range so every sampled
// instrument plays the full piano roll (pitch class preserved). Pure function
// of the key, so matching note-offs transpose identically.
std::pair<int, int> presetKeyRange(tsf* synth, int program)
{
    int lo = 127, hi = 0;
    const int index = synth != nullptr ? tsf_get_presetindex(synth, 0, program) : -1;
    if (synth == nullptr || index < 0 || index >= synth->presetNum)
        return { 0, 127 };
    const auto& preset = synth->presets[index];
    for (int i = 0; i < preset.regionNum; ++i)
    {
        lo = std::min(lo, static_cast<int>(preset.regions[i].lokey));
        hi = std::max(hi, static_cast<int>(preset.regions[i].hikey));
    }
    if (hi < lo)
        return { 0, 127 };
    return { lo, hi };
}

int fitKey(int key, int lo, int hi)
{
    key = std::clamp(key, 0, 127);
    if (hi - lo < 12) // narrower than an octave: nearest edge, no oscillation
        return std::clamp(key, lo, hi);
    while (key > hi)
        key -= 12;
    while (key < lo)
        key += 12;
    return key;
}
}

SampledInstrument::SampledInstrument()
{
    auto& source = bank();
    const std::lock_guard lock(source.mutex);
    synth = tsf_copy(source.data);
    if (synth == nullptr)
        return;
    bool ready = tsf_set_max_voices(synth, 64) != 0;
    for (int channel = 0; channel < 16; ++channel)
        ready = (tsf_channel_set_bank_preset(synth, channel, 0, 0) != 0) && ready;
    for (const auto& preset : instruments)
        if (preset.program >= 0 && tsf_get_presetindex(synth, 0, preset.program) < 0)
            ready = false;
    if (!ready)
    {
        tsf_close(synth);
        synth = nullptr;
    }
}

SampledInstrument::~SampledInstrument()
{
    const std::lock_guard lock(bank().mutex);
    tsf_close(synth);
}

void SampledInstrument::prepare(double rate)
{
    if (synth != nullptr)
    {
        stop();
        tsf_set_output(synth, TSF_STEREO_INTERLEAVED, static_cast<int>(rate), -10.0f);
    }
}

void SampledInstrument::stop()
{
    if (synth == nullptr)
        return;
    // tsf_reset frees its channel array; preserve that preallocation here.
    // Pinned TSF's quick-release API leaves a 10ms tail; transport panic must
    // instead silence every voice immediately.
    for (int i = 0; i < synth->voiceNum; ++i)
        synth->voices[i].playingPreset = -1;
    for (int channel = 0; channel < 16; ++channel)
        tsf_channel_midi_control(synth, channel, 121, 0);
}

void SampledInstrument::select(int preset)
{
    if (selected == preset || !validInstrument(preset))
        return;
    stop();
    selected = preset;
    if (synth != nullptr && instruments[static_cast<std::size_t>(preset)].program >= 0)
    {
        const int program = instruments[static_cast<std::size_t>(preset)].program;
        for (int channel = 0; channel < 16; ++channel)
            tsf_channel_set_bank_preset(synth, channel, 0, program);
        const auto range = presetKeyRange(synth, program);
        rangeLo = range.first;
        rangeHi = range.second;
    }
}

void SampledInstrument::message(const juce::MidiMessage& midi)
{
    const int channel = midi.getChannel() - 1;
    if (channel < 0 || channel >= 16)
        return;
    if (midi.isNoteOn())
        tsf_channel_note_on(synth, channel, fitKey(midi.getNoteNumber(), rangeLo, rangeHi),
                            midi.getFloatVelocity());
    else if (midi.isNoteOff())
        tsf_channel_note_off(synth, channel, fitKey(midi.getNoteNumber(), rangeLo, rangeHi));
    else if (midi.isPitchWheel())
        tsf_channel_set_pitchwheel(synth, channel, midi.getPitchWheelValue());
    else if (midi.isController())
        tsf_channel_midi_control(synth, channel, midi.getControllerNumber(), midi.getControllerValue());
    // Program changes from hardware never override the saved track instrument.
}

void SampledInstrument::renderRange(juce::AudioBuffer<float>& output, int start, int count)
{
    float samples[512 * 2];
    while (count > 0)
    {
        const int n = std::min(count, 512);
        tsf_render_float(synth, samples, n, 0);
        for (int i = 0; i < n; ++i)
            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                output.addSample(channel, start + i, samples[2 * i + (channel % 2)]);
        start += n;
        count -= n;
    }
}

void SampledInstrument::render(juce::AudioBuffer<float>& output, int start, int count, const juce::MidiBuffer& events)
{
    if (synth == nullptr)
        return;
    const int end = start + count;
    int cursor = start;
    for (const auto event : events)
    {
        if (event.samplePosition < start || event.samplePosition >= end)
            continue;
        renderRange(output, cursor, event.samplePosition - cursor);
        message(event.getMessage());
        cursor = event.samplePosition;
    }
    renderRange(output, cursor, end - cursor);
}
}
