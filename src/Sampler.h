#pragma once
#include "SamplerParams.h"
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace sonora
{
// One decoded audio file for the Sampler instrument. Built on a worker thread
// (file IO and decoding allowed), then handed to the audio thread with RCU
// semantics: the engine only loads the pointer, never frees it. Kept at the
// file's own sample rate; playback converts through the pitch ratio, so there
// is no load-time resampling to degrade quality.
struct SampleData
{
    std::array<std::vector<float>, 2> channel; // one channel: right is empty
    int channels = 1;
    int frames = 0;
    double rate = 48000.0;
    bool valid() const { return frames >= 2 && channels >= 1 && channels <= 2; }
};

inline constexpr double maxSampleSeconds = 90.0;

// ---- Sample library -------------------------------------------------------
// Sounds added to the sampler are copied into one per-user library
// (~/.local/share/sonora/samples), so they are reusable across projects. A
// track stores only the name; projects copy the files they use into their own
// media folder on save, so they stay portable.
bool isSampleFile(const juce::File& file);
juce::File sampleLibraryDir();
// Library-relative names (forward slashes), sorted, subfolders included.
std::vector<juce::String> listSampleLibrary(const juce::File& dir);
// Copies a file into the library (unless it is already there) and returns its
// library-relative name, or an empty string if it is not an audio file or the
// copy failed.
juce::String importSampleToLibrary(const juce::File& source, const juce::File& dir = sampleLibraryDir());
// First existing file for `name` across `searchDirs` (media, session, library
// in that order). Unsafe names (absolute, "..") never resolve.
juce::File resolveSampleFile(const juce::String& name, const std::vector<juce::File>& searchDirs);

// Decodes any JUCE-readable format (WAV, AIFF, FLAC, OGG, MP3), keeps up to
// two channels, caps the length, and peak-normalizes to 0.9 so every sample
// starts at a comparable level. Returns null if the file is unreadable or
// `cancel` is raised.
std::unique_ptr<SampleData> loadSampleData(const juce::File& file, const std::atomic<bool>* cancel = nullptr);

// Fundamental of a pitched sample as a MIDI note (0-127), or -1 when the
// sound is unpitched or the estimate is not stable (drums, noise, chords).
int detectRootNote(const SampleData& data);

// Peak overview for the editor waveform: `buckets` values in 0..1.
std::vector<float> samplePeaks(const SampleData& data, int buckets);

// What the editor shows about a loaded sample (never read by the audio thread).
struct SampleOverview
{
    std::vector<float> peaks;
    double seconds = 0.0, rate = 0.0;
    int channels = 0;
    int detectedRoot = -1;
    bool loaded = false;
};
SampleOverview makeOverview(const SampleData& data, int buckets);

// Polyphonic sample player for one track. Construct/prepare off the audio
// thread; render() and stop() never allocate, lock, or read files.
class SamplerInstrument
{
public:
    static constexpr int numVoices = 24;
    SamplerInstrument();
    void prepare(double engineRate);
    // Between blocks, from the audio thread: the owning track's settings.
    void setParams(const SamplerParams& value);
    // Installs a sample built on another thread; returns the previous one for
    // grace-period deletion by the caller. Null unloads.
    const SampleData* requestSample(const SampleData* next)
    {
        return data.exchange(next, std::memory_order_acq_rel);
    }
    bool hasSample() const { return data.load(std::memory_order_acquire) != nullptr; }
    int activeVoices() const;
    // Silences every voice immediately (transport stop / panic).
    void stop();
    void render(juce::AudioBuffer<float>& output, int start, int count, const juce::MidiBuffer& events);

private:
    struct Voice
    {
        bool active = false, releasing = false;
        int note = 0, channel = 0;
        float velocity = 1.0f;
        double position = 0.0, increment = 1.0;
        juce::ADSR envelope;
        std::uint32_t age = 0;
    };
    void noteOn(const SampleData& sample, int channel, int note, float velocity);
    void noteOff(int channel, int note);
    void renderRange(const SampleData* sample, juce::AudioBuffer<float>& output, int start, int count);
    double ratioFor(const SampleData& sample, int note) const;

    std::atomic<const SampleData*> data { nullptr };
    std::array<Voice, numVoices> voices;
    SamplerParams params;
    double rate = 48000.0;
    float bendSemis = 0.0f;
    std::uint32_t clock = 0;
};
}
