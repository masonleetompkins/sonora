#pragma once
#include "Timing.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <functional>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>

namespace sonora
{
inline constexpr int maxTakes = 8;
inline constexpr int takeFileCapacity = 260;

// Value-type take metadata: trivially copyable so it can ride the existing
// UI-to-audio snapshot queue inside ProjectState. `file` holds only a file
// name; it is resolved against the project's media folder at load/save time.
struct AudioTakeMeta
{
    std::uint32_t id = 0;
    char file[takeFileCapacity] {};
    int startTick = 0;
    int frames = 0;
    float gain = 1.0f;
    bool mute = false;
    bool solo = false; // comping: when any take is soloed, only solos play.
    int channels = 1;
    bool offline = false;
    bool valid() const
    {
        // startTick may be negative by up to one loop: a take punched at the
        // very top still compensates input latency by trimming leading frames.
        return id > 0 && id <= 2147483647u && file[0] != '\0' && startTick >= -patternTicks
            && frames > 0 && frames <= 48000 * 60 * 10 && std::isfinite(gain)
            && gain >= 0.0f && gain <= 2.0f && (channels == 1 || channels == 2);
    }
    bool operator==(const AudioTakeMeta&) const = default;
    juce::String fileName() const { return juce::String(file); }
    void setFileName(const juce::String& name)
    {
        const auto bytes = name.toRawUTF8();
        std::strncpy(file, bytes, takeFileCapacity - 1);
        file[takeFileCapacity - 1] = '\0';
    }
};

// Preloaded audio for the engine. Built on the message thread (file IO and
// resampling allowed), then handed to the audio thread with RCU semantics:
// the engine only atomically loads the pointer, never frees it. The previous
// set is retired on the message thread after a grace period.
struct PreloadedTake
{
    std::uint32_t id = 0;
    juce::AudioBuffer<float> audio;
    int startTick = 0;
    float gain = 1.0f;
    bool mute = false;
};

struct TakeSet
{
    std::vector<PreloadedTake> takes;
};

// Background take recorder. push() is realtime-safe (single producer FIFO,
// no allocation); the writer thread owns all file IO. WAV data is 24-bit.
class TakeRecorder : private juce::Thread
{
public:
    TakeRecorder();
    ~TakeRecorder() override;
    bool start(const juce::File& file, double sampleRate, int channels);
    bool isRecording() const { return recording.load(); }
    void push(const float* const* data, int numChannels, int numSamples);
    // Stops the thread and finalizes the file. Returns frames written, or -1
    // on failure. Call from the message thread only.
    int stop();
    int getOverruns() const { return overruns.load(); }

private:
    void run() override;
    bool drainOnce();
    juce::AbstractFifo fifo { 1 };
    std::vector<float> fifoBuffer, chunkScratch = std::vector<float>(8192 * 2),
                       planarScratch = std::vector<float>(8192 * 2);
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::WaitableEvent dataReady;
    std::atomic<bool> recording { false }, exitFlag { false };
    std::atomic<int> overruns { 0 }, totalFrames { 0 };
    int channels = 0;
};

// Latency compensation: recorded audio arrives late by the input latency, so
// the take start is shifted earlier by the same amount (in ticks).
inline int latencyCompensationTicks(int inputLatencySamples, double framesPerTick)
{
    if (framesPerTick <= 0.0 || inputLatencySamples <= 0)
        return 0;
    return static_cast<int>(std::llround(static_cast<double>(inputLatencySamples) / framesPerTick));
}

juce::File sessionDir();
juce::File mediaDirFor(const juce::File& projectFile);
// Loads (and resamples to targetRate when needed) every online take.
std::unique_ptr<TakeSet> loadTakes(const std::array<AudioTakeMeta, maxTakes>& takes, int takeCount,
                                   const juce::File& mediaDir, double targetRate,
                                   std::function<bool(double)> progress = {});
}
