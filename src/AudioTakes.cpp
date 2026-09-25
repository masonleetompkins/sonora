#include "AudioTakes.h"

namespace sonora
{
TakeRecorder::TakeRecorder() : juce::Thread("Sonora take recorder") {}

TakeRecorder::~TakeRecorder()
{
    if (isRecording())
        stop();
}

bool TakeRecorder::start(const juce::File& file, double sampleRate, int numChannels)
{
    if (isRecording() || numChannels < 1 || numChannels > 2 || sampleRate <= 0.0)
        return false;
    auto* stream = new juce::FileOutputStream(file);
    if (!stream->openedOk())
    {
        delete stream;
        return false;
    }
    juce::WavAudioFormat format;
    writer.reset(format.createWriterFor(stream, sampleRate,
        static_cast<unsigned>(numChannels), 24, {}, 0));
    if (writer == nullptr)
        return false;
    channels = numChannels;
    // 10 seconds of headroom at 96 kHz stereo; never reallocates afterwards.
    fifoBuffer.assign(static_cast<std::size_t>(96000 * 10 * 2), 0.0f);
    fifo.setTotalSize(static_cast<int>(fifoBuffer.size()));
    fifo.reset();
    overruns.store(0);
    totalFrames.store(0);
    exitFlag.store(false);
    recording.store(true);
    startThread();
    return true;
}

void TakeRecorder::push(const float* const* data, int numChannels, int numSamples)
{
    if (!recording.load() || data == nullptr || numChannels <= 0 || numSamples <= 0)
        return;
    const int wanted = numSamples * channels;
    int start1, size1, start2, size2;
    fifo.prepareToWrite(wanted, start1, size1, start2, size2);
    const int writable = size1 + size2;
    if (writable < wanted)
        overruns.fetch_add(1);
    // Interleave into the FIFO; the writer thread deinterleaves on write.
    // Missing source channels (e.g. mono input in stereo mode) record silence.
    int srcOffset = 0;
    auto interleave = [&](int fifoOffset, int count) {
        for (int i = 0; i < count; ++i)
        {
            const int linear = srcOffset + i;
            const int srcFrame = linear / channels;
            const int srcChannel = linear % channels;
            fifoBuffer[static_cast<std::size_t>(fifoOffset + i)] =
                srcChannel < numChannels ? data[srcChannel][srcFrame] : 0.0f;
        }
        srcOffset += count;
    };
    interleave(start1, size1);
    interleave(start2, size2);
    fifo.finishedWrite(writable);
    if (writable > 0)
        dataReady.signal();
}

void TakeRecorder::run()
{
    while (!threadShouldExit())
    {
        dataReady.wait(50);
        drainOnce();
        if (exitFlag.load() && fifo.getNumReady() <= 0)
            break;
    }
}

// Shared by the writer thread and stop(): a push that lands after the
// writer's last pass (including a thread that never got scheduled before
// stop) is written synchronously by the caller instead of being lost.
bool TakeRecorder::drainOnce()
{
    bool wrote = false;
    for (;;)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead(static_cast<int>(chunkScratch.size()), start1, size1, start2, size2);
        const int available = size1 + size2;
        if (available <= 0)
            break;
        // Whole frames only; leave a partial frame for the next pass.
        const int frames = available / channels;
        if (frames <= 0)
            break;
        const int floats = frames * channels;
        int copied = 0;
        for (int k = 0; k < size1 && copied < floats; ++k)
            chunkScratch[static_cast<std::size_t>(copied++)] = fifoBuffer[static_cast<std::size_t>(start1 + k)];
        for (int k = 0; k < size2 && copied < floats; ++k)
            chunkScratch[static_cast<std::size_t>(copied++)] = fifoBuffer[static_cast<std::size_t>(start2 + k)];
        // Deinterleave into per-channel segments sharing one scratch buffer.
        const float* planarPtrs[2] = { nullptr, nullptr };
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* segment = planarScratch.data() + static_cast<std::size_t>(ch * frames);
            for (int i = 0; i < frames; ++i)
                segment[i] = chunkScratch[static_cast<std::size_t>(i * channels + ch)];
            planarPtrs[ch] = segment;
        }
        if (writer != nullptr)
            writer->writeFromFloatArrays(planarPtrs, channels, frames);
        totalFrames.fetch_add(frames);
        fifo.finishedRead(floats);
        wrote = true;
        if (exitFlag.load())
            continue; // drain everything before leaving
    }
    return wrote;
}

int TakeRecorder::stop()
{
    if (!isRecording())
        return -1;
    recording.store(false);
    exitFlag.store(true);
    dataReady.signal();
    stopThread(5000);
    drainOnce(); // catch anything that landed after the writer's last pass
    writer.reset();
    return totalFrames.load();
}

juce::File sessionDir()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sonora-session");
    dir.createDirectory();
    return dir;
}

juce::File mediaDirFor(const juce::File& projectFile)
{
    return projectFile.getSiblingFile(projectFile.getFileNameWithoutExtension() + ".media");
}

std::unique_ptr<TakeSet> loadTakes(const std::array<AudioTakeMeta, maxTakes>& takes, int takeCount,
                                   const juce::File& mediaDir, double targetRate,
                                   std::function<bool(double)> progress)
{
    auto set = std::make_unique<TakeSet>();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (int t = 0; t < takeCount; ++t)
    {
        const auto& meta = takes[static_cast<std::size_t>(t)];
        if (meta.mute || meta.offline || meta.id == 0)
            continue;
        if (progress && !progress(static_cast<double>(t) / std::max(1, takeCount)))
            return nullptr;
        std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(mediaDir.getChildFile(meta.fileName())));
        if (reader == nullptr)
            continue;
        PreloadedTake take;
        take.id = meta.id;
        take.startTick = meta.startTick;
        take.gain = meta.gain;
        take.mute = false;
        const int channels = static_cast<int>(std::min<long long>(reader->numChannels, 2));
        const auto length = static_cast<int>(std::min<long long>(reader->lengthInSamples, 48000LL * 60 * 10));
        take.audio.setSize(channels, length + 64, false, true, false);
        reader->read(&take.audio, 0, length, 0, true, channels > 1);
        if (std::abs(reader->sampleRate - targetRate) > 1.0 && length > 0)
        {
            const double ratio = reader->sampleRate / targetRate;
            const int outLength = static_cast<int>(length / ratio) + 64;
            juce::AudioBuffer<float> resampled(channels, outLength);
            juce::CatmullRomInterpolator interpolator;
            for (int ch = 0; ch < channels; ++ch)
                interpolator.process(ratio, take.audio.getReadPointer(ch),
                                     resampled.getWritePointer(ch), outLength);
            take.audio = std::move(resampled);
        }
        set->takes.push_back(std::move(take));
    }
    return set;
}
}
