#include "Sampler.h"
#include "SonoraPaths.h"
#include "PitchCorrect.h"
#include <algorithm>
#include <cmath>

namespace sonora
{
namespace
{
float hermite(const std::vector<float>& data, int last, double position)
{
    const int i = static_cast<int>(std::floor(position));
    const float f = static_cast<float>(position - i);
    auto at = [&](int k) { return data[static_cast<std::size_t>(std::clamp(k, 0, last))]; };
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

struct Region
{
    int start = 0, end = 2, loopStart = 0, loopEnd = 2;
};

Region regionFor(const SamplerParams& p, int frames)
{
    Region r;
    r.start = std::clamp(static_cast<int>(p.start * static_cast<float>(frames)), 0, std::max(0, frames - 2));
    r.end = std::clamp(static_cast<int>(p.end * static_cast<float>(frames)), r.start + 2, frames);
    r.loopStart = std::clamp(static_cast<int>(p.loopStart * static_cast<float>(frames)), r.start,
                             std::max(r.start, r.end - 2));
    r.loopEnd = std::clamp(static_cast<int>(p.loopEnd * static_cast<float>(frames)), r.loopStart + 2, r.end);
    return r;
}
}

std::unique_ptr<SampleData> loadSampleData(const juce::File& file, const std::atomic<bool>* cancel)
{
    if (!file.existsAsFile())
        return nullptr;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0)
        return nullptr;
    auto data = std::make_unique<SampleData>();
    data->channels = static_cast<int>(std::min<unsigned int>(reader->numChannels, 2u));
    data->rate = reader->sampleRate;
    const auto cap = static_cast<juce::int64>(maxSampleSeconds * reader->sampleRate);
    data->frames = static_cast<int>(std::min(reader->lengthInSamples, cap));
    for (int c = 0; c < data->channels; ++c)
        data->channel[static_cast<std::size_t>(c)].assign(static_cast<std::size_t>(data->frames), 0.0f);
    constexpr int chunkFrames = 65536;
    juce::AudioBuffer<float> chunk(data->channels, chunkFrames);
    for (int done = 0; done < data->frames; done += chunkFrames)
    {
        if (cancel != nullptr && cancel->load())
            return nullptr;
        const int n = std::min(chunkFrames, data->frames - done);
        chunk.clear();
        if (!reader->read(&chunk, 0, n, done, true, data->channels > 1))
            return nullptr;
        for (int c = 0; c < data->channels; ++c)
            std::copy(chunk.getReadPointer(c), chunk.getReadPointer(c) + n,
                      data->channel[static_cast<std::size_t>(c)].begin() + done);
    }
    float peak = 0.0f;
    for (int c = 0; c < data->channels; ++c)
        for (const auto value : data->channel[static_cast<std::size_t>(c)])
            peak = std::max(peak, std::abs(value));
    if (peak > 1.0e-5f)
    {
        const float scale = 0.9f / peak;
        for (int c = 0; c < data->channels; ++c)
            for (auto& value : data->channel[static_cast<std::size_t>(c)])
                value *= scale;
    }
    if (!data->valid())
        return nullptr;
    return data;
}

bool isSampleFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    for (const char* known : { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3" })
        if (extension == known)
            return true;
    return false;
}

juce::File sampleLibraryDir()
{
#if defined(__APPLE__)
    return sonora::sonoraSupportDir().getChildFile("samples");
#endif
    auto root = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {});
    if (root.isEmpty() || !juce::File::isAbsolutePath(root))
        root = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile(".local/share").getFullPathName();
    return juce::File(root).getChildFile("sonora/samples");
}

std::vector<juce::String> listSampleLibrary(const juce::File& dir)
{
    std::vector<juce::String> names;
    if (!dir.isDirectory())
        return names;
    constexpr int maxEntries = 4000;
    for (const auto& entry : juce::RangedDirectoryIterator(dir, true, "*", juce::File::findFiles))
    {
        const auto file = entry.getFile();
        if (!isSampleFile(file) || file.isHidden())
            continue;
        const auto relative = file.getRelativePathFrom(dir).replaceCharacter('\\', '/');
        if (relative.length() >= samplerFileCapacity - 1 || relative.contains(".."))
            continue;
        names.push_back(relative);
        if (static_cast<int>(names.size()) >= maxEntries)
            break;
    }
    std::sort(names.begin(), names.end(), [](const juce::String& a, const juce::String& b) {
        return a.compareIgnoreCase(b) < 0;
    });
    return names;
}

juce::String importSampleToLibrary(const juce::File& source, const juce::File& dir)
{
    if (!source.existsAsFile() || !isSampleFile(source) || !dir.createDirectory().wasOk())
        return {};
    // Already in the library: reuse it instead of piling up copies.
    if (source.isAChildOf(dir))
        return source.getRelativePathFrom(dir).replaceCharacter('\\', '/');
    auto stem = source.getFileNameWithoutExtension().retainCharacters(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.()#");
    stem = stem.trim().substring(0, 80);
    if (stem.isEmpty())
        stem = "sample";
    const auto target = dir.getNonexistentChildFile(stem, source.getFileExtension().toLowerCase(), false);
    if (!source.copyFileTo(target))
        return {};
    return target.getFileName();
}

juce::File resolveSampleFile(const juce::String& name, const std::vector<juce::File>& searchDirs)
{
    if (name.isEmpty() || !isSafeSampleName(name.toRawUTF8()))
        return {};
    for (const auto& dir : searchDirs)
    {
        if (dir == juce::File())
            continue;
        const auto candidate = dir.getChildFile(name);
        if (candidate.existsAsFile())
            return candidate;
    }
    return {};
}

int detectRootNote(const SampleData& data)
{
    if (!data.valid())
        return -1;
    // A steady stretch near the start: skip the attack, look at up to a second.
    const int begin = std::min(data.frames / 8, static_cast<int>(data.rate * 0.05));
    const int length = std::min(data.frames - begin, static_cast<int>(data.rate));
    if (length < 4096)
        return -1;
    std::vector<float> mono(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i)
    {
        float sum = data.channel[0][static_cast<std::size_t>(begin + i)];
        if (data.channels > 1)
            sum = 0.5f * (sum + data.channel[1][static_cast<std::size_t>(begin + i)]);
        mono[static_cast<std::size_t>(i)] = sum;
    }
    const int window = static_cast<int>(2048.0 * std::max(1.0, data.rate / 48000.0));
    const YinDetector detector(data.rate, window, window / 4, 30.0f, 2000.0f, 0.12f);
    const auto contour = detector.analyze(mono.data(), length);
    std::vector<double> notes;
    for (const auto& frame : contour.frames)
        if (frame.voiced && frame.f0Hz > 0.0f)
            notes.push_back(hzToMidi(frame.f0Hz));
    // Need a few voiced frames, and most of the frames must agree.
    if (notes.size() < 3 || notes.size() * 2 < contour.frames.size())
        return -1;
    std::sort(notes.begin(), notes.end());
    const double median = notes[notes.size() / 2];
    const auto agreeing = std::count_if(notes.begin(), notes.end(),
                                        [median](double n) { return std::abs(n - median) < 0.6; });
    if (static_cast<double>(agreeing) < 0.6 * static_cast<double>(notes.size()))
        return -1;
    const int note = static_cast<int>(std::lround(median));
    return note >= 0 && note <= 127 ? note : -1;
}

std::vector<float> samplePeaks(const SampleData& data, int buckets)
{
    std::vector<float> peaks(static_cast<std::size_t>(std::max(1, buckets)), 0.0f);
    if (!data.valid())
        return peaks;
    const int n = static_cast<int>(peaks.size());
    for (int b = 0; b < n; ++b)
    {
        const int from = static_cast<int>(static_cast<juce::int64>(b) * data.frames / n);
        const int to = std::max(from + 1, static_cast<int>(static_cast<juce::int64>(b + 1) * data.frames / n));
        const int stride = std::max(1, (to - from) / 64);
        float peak = 0.0f;
        for (int c = 0; c < data.channels; ++c)
            for (int i = from; i < to && i < data.frames; i += stride)
                peak = std::max(peak, std::abs(data.channel[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]));
        peaks[static_cast<std::size_t>(b)] = std::min(1.0f, peak);
    }
    return peaks;
}

SampleOverview makeOverview(const SampleData& data, int buckets)
{
    SampleOverview overview;
    if (!data.valid())
        return overview;
    overview.peaks = samplePeaks(data, buckets);
    overview.seconds = static_cast<double>(data.frames) / data.rate;
    overview.rate = data.rate;
    overview.channels = data.channels;
    overview.detectedRoot = detectRootNote(data);
    overview.loaded = true;
    return overview;
}

SamplerInstrument::SamplerInstrument() = default;

void SamplerInstrument::prepare(double engineRate)
{
    rate = engineRate > 0.0 ? engineRate : 48000.0;
    for (auto& voice : voices)
    {
        voice.envelope.setSampleRate(rate);
        voice.envelope.reset();
        voice.active = false;
    }
    bendSemis = 0.0f;
}

void SamplerInstrument::setParams(const SamplerParams& value)
{
    params = value.valid() ? value : SamplerParams {};
    const juce::ADSR::Parameters adsr { params.attack, params.decay, params.sustain, params.release };
    for (auto& voice : voices)
        if (voice.active)
            voice.envelope.setParameters(adsr);
}

int SamplerInstrument::activeVoices() const
{
    int count = 0;
    for (const auto& voice : voices)
        count += voice.active ? 1 : 0;
    return count;
}

void SamplerInstrument::stop()
{
    for (auto& voice : voices)
    {
        voice.active = false;
        voice.releasing = false;
        voice.envelope.reset();
    }
    bendSemis = 0.0f;
}

double SamplerInstrument::ratioFor(const SampleData& sample, int note) const
{
    double semitones = params.keyTrack ? static_cast<double>(note - params.rootNote) : 0.0;
    semitones += static_cast<double>(params.tune) / 100.0;
    return std::clamp(std::pow(2.0, semitones / 12.0) * sample.rate / rate, 1.0 / 64.0, 64.0);
}

void SamplerInstrument::noteOn(const SampleData& sample, int channel, int note, float velocity)
{
    Voice* chosen = nullptr;
    for (auto& voice : voices)
        if (!voice.active)
        {
            chosen = &voice;
            break;
        }
    if (chosen == nullptr)
    {
        // Steal the oldest releasing voice, else the oldest of all.
        for (auto& voice : voices)
            if (voice.releasing && (chosen == nullptr || voice.age < chosen->age))
                chosen = &voice;
        if (chosen == nullptr)
            for (auto& voice : voices)
                if (chosen == nullptr || voice.age < chosen->age)
                    chosen = &voice;
    }
    const auto region = regionFor(params, sample.frames);
    chosen->active = true;
    chosen->releasing = false;
    chosen->note = note;
    chosen->channel = channel;
    chosen->velocity = std::clamp(velocity, 0.0f, 1.0f);
    chosen->increment = ratioFor(sample, note);
    chosen->position = params.reverse ? static_cast<double>(region.end - 1) : static_cast<double>(region.start);
    chosen->age = ++clock;
    chosen->envelope.setSampleRate(rate);
    chosen->envelope.setParameters({ params.attack, params.decay, params.sustain, params.release });
    chosen->envelope.reset();
    chosen->envelope.noteOn();
}

void SamplerInstrument::noteOff(int channel, int note)
{
    if (params.oneShot)
        return; // one-shots play out regardless of the key
    for (auto& voice : voices)
        if (voice.active && !voice.releasing && voice.note == note && voice.channel == channel)
        {
            voice.releasing = true;
            voice.envelope.noteOff();
        }
}

void SamplerInstrument::renderRange(const SampleData* sample, juce::AudioBuffer<float>& output, int start, int count)
{
    if (sample == nullptr || count <= 0 || !sample->valid())
        return;
    const auto region = regionFor(params, sample->frames);
    const bool looping = params.loop && !params.reverse && !params.oneShot;
    const int last = sample->frames - 1;
    const auto& left = sample->channel[0];
    const auto& right = sample->channel[sample->channels > 1 ? 1 : 0];
    const float bend = std::pow(2.0f, bendSemis / 12.0f);
    const float level = 0.6f * params.gain;
    const int channels = output.getNumChannels();
    for (auto& voice : voices)
    {
        if (!voice.active)
            continue;
        const double step = voice.increment * static_cast<double>(bend);
        const double fadeSpan = std::max(1.0, 64.0 * voice.increment);
        for (int i = 0; i < count; ++i)
        {
            double& position = voice.position;
            if (params.reverse)
            {
                if (position <= static_cast<double>(region.start))
                {
                    voice.active = false;
                    break;
                }
            }
            else if (looping)
            {
                // The loop wraps before the end test: a loop that reaches the
                // end of the file must keep going, not finish the voice.
                for (int guard = 0; position >= static_cast<double>(region.loopEnd) && guard < 8; ++guard)
                    position -= static_cast<double>(region.loopEnd - region.loopStart);
                if (position >= static_cast<double>(region.loopEnd))
                    position = static_cast<double>(region.loopStart); // pathological step: stay inside
            }
            else if (position >= static_cast<double>(region.end - 1))
            {
                voice.active = false;
                break;
            }
            const float env = voice.envelope.getNextSample();
            if (!voice.envelope.isActive())
            {
                voice.active = false;
                break;
            }
            // Short fade into the end of the region so a cut sample never clicks.
            double edge = params.reverse ? position - static_cast<double>(region.start)
                                         : static_cast<double>(region.end - 1) - position;
            if (looping)
                edge = fadeSpan;
            const float fade = static_cast<float>(std::min(1.0, edge / fadeSpan));
            const float gain = env * voice.velocity * level * fade;
            const float l = hermite(left, last, position) * gain;
            const float r = sample->channels > 1 ? hermite(right, last, position) * gain : l;
            for (int c = 0; c < channels; ++c)
                output.addSample(c, start + i, c % 2 == 0 ? l : r);
            position += params.reverse ? -step : step;
        }
    }
}

void SamplerInstrument::render(juce::AudioBuffer<float>& output, int start, int count, const juce::MidiBuffer& events)
{
    const SampleData* sample = data.load(std::memory_order_acquire);
    const int end = start + count;
    int cursor = start;
    auto handle = [&](const juce::MidiMessage& message) {
        const int channel = message.getChannel();
        if (message.isNoteOn() && message.getVelocity() > 0)
        {
            if (sample != nullptr && sample->valid())
                noteOn(*sample, channel, message.getNoteNumber(), message.getFloatVelocity());
        }
        else if (message.isNoteOff() || (message.isNoteOn() && message.getVelocity() == 0))
            noteOff(channel, message.getNoteNumber());
        else if (message.isPitchWheel())
            bendSemis = (static_cast<float>(message.getPitchWheelValue()) - 8192.0f) / 8192.0f * 2.0f;
        else if (message.isAllSoundOff())
            stop();
        else if (message.isAllNotesOff())
            for (auto& voice : voices)
                if (voice.active && !voice.releasing && !params.oneShot)
                {
                    voice.releasing = true;
                    voice.envelope.noteOff();
                }
    };
    for (const auto event : events)
    {
        if (event.samplePosition < start || event.samplePosition >= end)
            continue;
        renderRange(sample, output, cursor, event.samplePosition - cursor);
        handle(event.getMessage());
        cursor = event.samplePosition;
    }
    renderRange(sample, output, cursor, end - cursor);
}
}
