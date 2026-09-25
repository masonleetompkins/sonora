#pragma once
#include "AudioEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace sonora
{
// Offline bounce: drives a private AudioEngine through the same graph as live
// playback, without an audio device, and returns stereo audio plus metering.
// Loop range renders exactly one 4-bar pattern; song range renders the full
// arrangement plus a natural ring-out tail (tails cut at the loop seam, as in
// looped playback). Safe to run on a worker thread; not on the audio thread.
struct ExportJob
{
    ProjectState project;
    juce::File mediaDir;
    double sampleRate = 48000.0;
    int bitDepth = 16;
    bool songRange = false;
    bool normalize = false;
    double tailSeconds = 1.0;
    bool valid() const
    {
        // Sample rates come from a fixed combo-box set; compare as integers.
        const auto rate = static_cast<long>(sampleRate + 0.5);
        const bool rateOk = rate == 44100L || rate == 48000L || rate == 96000L;
        return project.valid() && rateOk
            && (bitDepth == 16 || bitDepth == 24) && tailSeconds >= 0.0 && tailSeconds <= 5.0;
    }
};

struct ExportResult
{
    juce::AudioBuffer<float> audio { 2, 0 };
    double sampleRate = 48000.0;
    float peak = 0.0f;
    bool clipped = false;
    bool normalized = false;
    juce::String error;
    bool ok() const { return error.isEmpty(); }
};

class OfflineExport
{
public:
    // Progress is 0..1. Return false from progress to cancel.
    static ExportResult render(const ExportJob& job, std::function<bool(double)> progress = {});
    static juce::Result writeWav(const juce::File& file, const ExportResult& result, int bitDepth);
    static int expectedFrames(const ExportJob& job);
};
}
