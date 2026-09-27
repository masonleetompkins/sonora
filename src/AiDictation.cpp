#include "AiDictation.h"
#include "AiMelody.h"
#include <juce_audio_formats/juce_audio_formats.h>

extern char** environ;

namespace sonora::ai
{
juce::String parseVoxtypeOutput(const juce::String& output)
{
    const auto lines = juce::StringArray::fromLines(output);
    for (int i = lines.size() - 1; i >= 0; --i)
    {
        const auto line = lines[i].trim();
        if (line.isEmpty())
            continue;
        if (line.startsWith("Loading audio file:") || line.startsWith("Audio format:")
            || line.startsWith("Processing "))
            return {};
        return line.substring(0, maxPromptChars);
    }
    return {};
}

DictationResult transcribeDictation(const juce::File& wav, const std::atomic<bool>* cancel)
{
    DictationResult result;
    if (!wav.existsAsFile())
    {
        result.error = "Microphone recording was not saved.";
        return result;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
    if (reader == nullptr || reader->lengthInSamples < static_cast<juce::int64>(reader->sampleRate / 4))
    {
        result.error = "Speak for at least a moment before stopping the mic.";
        return result;
    }
    // Silence can make Whisper hallucinate a phrase; reject it first.
    juce::AudioBuffer<float> sample(1, static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, 32768)));
    reader->read(&sample, 0, sample.getNumSamples(), 0, true, false);
    if (sample.getMagnitude(0, 0, sample.getNumSamples()) < 0.015f)
    {
        result.error = "I didn't hear speech. Check the mic and try again.";
        return result;
    }
    reader.reset();

    const juce::File ffmpeg("/usr/bin/ffmpeg"), voxtype("/usr/bin/voxtype");
    if (!ffmpeg.existsAsFile() || !voxtype.existsAsFile())
    {
        result.error = "Local dictation needs ffmpeg and Voxtype installed.";
        return result;
    }
    const auto target = wav.getParentDirectory().getNonexistentChildFile("sonora-dictation-16k", ".wav");
    juce::StringArray environment;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry)
        environment.add(juce::String::fromUTF8(*entry));
    environment = sanitizedEnvironment(environment);
    const auto convert = runProcess(ffmpeg,
        { "-nostdin", "-hide_banner", "-loglevel", "error", "-i", wav.getFullPathName(),
          "-vn", "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le", target.getFullPathName() },
        {}, target.getParentDirectory(), environment, 20000, cancel);
    if (!convert.ok())
    {
        target.deleteFile();
        result.error = convert.cancelled ? "Dictation cancelled." : "Could not prepare the mic audio for transcription.";
        return result;
    }
    // Explicitly force the local Whisper model: no remote speech provider,
    // simulated typing, clipboard, or shell is involved.
    const auto transcript = runProcess(voxtype,
        { "--quiet", "--whisper-mode", "local", "transcribe", target.getFullPathName() },
        {}, target.getParentDirectory(), environment, 120000, cancel);
    target.deleteFile();
    if (!transcript.ok())
        result.error = transcript.cancelled ? "Dictation cancelled." : "Voxtype could not transcribe the mic audio.";
    else
    {
        result.text = parseVoxtypeOutput(transcript.output);
        if (result.text.isEmpty())
            result.error = "I couldn't make out any words. Try speaking more clearly.";
    }
    return result;
}
}
