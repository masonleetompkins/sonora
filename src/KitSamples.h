#pragma once
#include "DrumSampler.h"
#include <array>
#include <juce_audio_formats/juce_audio_formats.h>

namespace sonora
{
inline juce::String padSampleName(const std::array<char, sampleFileCapacity>& slot)
{
    return juce::String(slot.data());
}

inline void setPadSampleName(std::array<char, sampleFileCapacity>& slot, const juce::String& name)
{
    const auto bytes = name.toRawUTF8();
    std::strncpy(slot.data(), bytes, sampleFileCapacity - 1);
    slot[sampleFileCapacity - 1] = '\0';
}
// Custom sample import. All work happens on the message thread: files are
// read in any JUCE-decodable format, mixed to mono, capped in length,
// resampled to the device rate, and peak-normalized to the pad's starter-kit
// target level. Empty names keep the built-in pad; missing or undecodable
// files fall back to the built-in pad so playback never goes silent.
inline constexpr int maxSampleFrames = 48000 * 10;

std::unique_ptr<SampleBank> loadSampleBank(const std::array<juce::String, drumPads>& files,
                                           const juce::File& mediaDir, double targetRate, int variant,
                                           std::function<bool(double)> progress = {});

// Named kits live in <baseDir>/<sanitized-name>/kit.json alongside their
// samples, so a preset is a portable folder. Factory kits (Starter/Deep/Crisp)
// are generator variants, not files, and never appear on disk.
struct KitPreset
{
    juce::String name;
    int variant = 0;
    std::array<juce::String, drumPads> files {};
};

juce::File kitsDir();
std::vector<juce::String> listKitPresets(const juce::File& baseDir);
juce::String sanitizePresetName(const juce::String& name);
// Copies the current kit's samples into a new preset folder.
juce::Result saveKitPreset(const juce::File& baseDir, const juce::String& name, int variant,
                           const std::array<juce::String, drumPads>& files,
                           std::function<juce::File(const juce::String&)> resolve);
juce::Result loadKitPreset(const juce::File& baseDir, const juce::String& name, KitPreset& preset);
juce::Result deleteKitPreset(const juce::File& baseDir, const juce::String& name);
}
