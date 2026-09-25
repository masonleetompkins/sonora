#include "Export.h"
#include "AudioTakes.h"

namespace sonora
{
int OfflineExport::expectedFrames(const ExportJob& job)
{
    const double framesPerTick = job.sampleRate * 60.0 / (job.project.bpm * ticksPerQuarter);
    const int musicTicks = job.songRange ? job.project.song.songTicks() : patternTicks;
    const auto musicFrames = static_cast<int>(std::ceil(musicTicks * framesPerTick));
    const auto tailFrames = static_cast<int>(std::ceil(job.tailSeconds * job.sampleRate));
    return musicFrames + (job.songRange ? tailFrames : 0);
}

ExportResult OfflineExport::render(const ExportJob& job, std::function<bool(double)> progress)
{
    ExportResult result;
    result.sampleRate = job.sampleRate;
    if (!job.valid())
    {
        result.error = "Invalid export settings.";
        return result;
    }
    // Loop range reuses the pattern directly; song range keeps the arrangement.
    ProjectState project = job.project;
    project.songMode = job.songRange;
    if (!project.valid())
    {
        result.error = "Invalid project state.";
        return result;
    }
    const int total = expectedFrames(job);
    if (total <= 0 || total > static_cast<int>(job.sampleRate * 60 * 10))
    {
        result.error = "Export length is out of range.";
        return result;
    }
    AudioEngine engine;
    engine.prepare(job.sampleRate);
    if (!engine.submit(project))
    {
        result.error = "The engine rejected this project.";
        return result;
    }
    // Recorded takes ride along through the RCU set; the render is synchronous
    // so the set can be retired and freed before returning.
    auto takes = loadTakes(project.takes, project.takeCount, job.mediaDir, job.sampleRate);
    if (takes != nullptr)
        engine.retireTakeSet(takes.get());
    engine.setPlaying(true);
    result.audio.setSize(2, total, false, true, false);
    juce::AudioBuffer<float> scratch(2, 1024);
    int rendered = 0;
    while (rendered < total)
    {
        if (progress && !progress(static_cast<double>(rendered) / total))
        {
            result.error = "Export cancelled.";
            engine.retireTakeSet(nullptr);
            return result;
        }
        const int count = std::min(1024, total - rendered);
        scratch.clear();
        engine.process({ &scratch, 0, count });
        // In song mode the transport stops itself at the arrangement end and
        // the remaining tail frames capture the natural voice ring-out.
        for (int channel = 0; channel < 2; ++channel)
            result.audio.copyFrom(channel, rendered, scratch, channel, 0, count);
        rendered += count;
    }
    result.peak = result.audio.getMagnitude(0, result.audio.getNumSamples());
    result.clipped = result.peak >= 1.0f;
    if (job.normalize && result.peak > 0.0f)
    {
        result.audio.applyGain(0.99f / result.peak);
        result.peak = 0.99f;
        result.clipped = false;
        result.normalized = true;
    }
    for (int channel = 0; channel < result.audio.getNumChannels(); ++channel)
    {
        const auto* data = result.audio.getReadPointer(channel);
        for (int i = 0; i < result.audio.getNumSamples(); ++i)
            if (!std::isfinite(data[i]))
            {
                result.error = "Export produced invalid audio.";
                engine.retireTakeSet(nullptr);
                return result;
            }
    }
    engine.retireTakeSet(nullptr);
    return result;
}

juce::Result OfflineExport::writeWav(const juce::File& file, const ExportResult& result, int bitDepth)
{
    if (!result.ok() || result.audio.getNumSamples() <= 0)
        return juce::Result::fail("Nothing to write.");
    if (bitDepth != 16 && bitDepth != 24)
        return juce::Result::fail("Bit depth must be 16 or 24.");
    juce::TemporaryFile temporary(file);
    {
        auto* stream = new juce::FileOutputStream(temporary.getFile());
        if (!stream->openedOk())
        {
            const auto status = stream->getStatus();
            delete stream;
            return status;
        }
        juce::WavAudioFormat format;
        // The writer takes ownership of the stream.
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream, result.sampleRate,
            static_cast<unsigned>(result.audio.getNumChannels()), bitDepth, {}, 0));
        if (writer == nullptr)
            return juce::Result::fail("Could not create the WAV file.");
        if (!writer->writeFromAudioSampleBuffer(result.audio, 0, result.audio.getNumSamples()))
            return juce::Result::fail("Could not write audio data.");
    }
    return temporary.overwriteTargetFileWithTemporary()
        ? juce::Result::ok() : juce::Result::fail("Could not replace the output file.");
}
}
