#pragma once
#include <atomic>
#include <juce_core/juce_core.h>

namespace sonora::ai
{
struct DictationResult
{
    juce::String text, error;
    bool ok() const { return error.isEmpty(); }
};

// Voxtype's `transcribe` prints progress followed by the final transcript.
juce::String parseVoxtypeOutput(const juce::String& output);
// Run the installed local Whisper model on the saved mic WAV, without typing
// simulated keys (which enter as number codes in JUCE/XWayland text boxes).
// The audio stays on this machine; no Claude call is made until Send.
DictationResult transcribeDictation(const juce::File& wav, const std::atomic<bool>* cancel);
}
