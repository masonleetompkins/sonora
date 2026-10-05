#include "KitSamples.h"
#include "SonoraPaths.h"

namespace sonora
{
namespace
{
void resampleTo(std::vector<float>& data, double sourceRate, double targetRate)
{
    if (data.empty() || sourceRate <= 0.0 || targetRate <= 0.0
        || std::abs(sourceRate - targetRate) < 1.0)
        return;
    const double ratio = sourceRate / targetRate;
    const int outLength = std::max(1, static_cast<int>(data.size() / ratio));
    std::vector<float> resampled(static_cast<std::size_t>(outLength));
    juce::CatmullRomInterpolator interpolator;
    interpolator.process(ratio, data.data(), resampled.data(), outLength);
    data = std::move(resampled);
}

void normalizeTo(std::vector<float>& data, float target)
{
    float peak = 0.0f;
    for (const auto value : data)
        peak = std::max(peak, std::abs(value));
    if (peak > 0.0f)
        for (auto& value : data)
            value *= target / peak;
}
}

std::unique_ptr<SampleBank> loadSampleBank(const std::array<juce::String, drumPads>& files,
                                           const juce::File& mediaDir, double targetRate, int variant,
                                           std::function<bool(double)> progress)
{
    auto bank = std::make_unique<SampleBank>(
        buildStarterBank(kitVariantParams(std::clamp(variant, 0, numKitVariants - 1))));
    if (targetRate <= 0.0)
        return bank;
    bank->sampleRate = targetRate;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (int pad = 0; pad < drumPads; ++pad)
    {
        if (progress && !progress(static_cast<double>(pad) / drumPads))
            return nullptr;
        // Built-in pads are resampled from the 48 kHz starter kit below.
        if (files[static_cast<std::size_t>(pad)].isEmpty())
            continue;
        std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(mediaDir.getChildFile(files[static_cast<std::size_t>(pad)])));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            continue; // missing or undecodable: keep the starter pad
        const int channels = static_cast<int>(std::min<long long>(reader->numChannels, 2));
        const int length = static_cast<int>(std::min<long long>(reader->lengthInSamples, maxSampleFrames));
        juce::AudioBuffer<float> stereo(channels, length + 64);
        reader->read(&stereo, 0, length, 0, true, channels > 1);
        std::vector<float> mono(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i)
        {
            double sum = 0.0;
            for (int ch = 0; ch < channels; ++ch)
                sum += stereo.getSample(ch, i);
            mono[static_cast<std::size_t>(i)] = static_cast<float>(sum / channels);
        }
        resampleTo(mono, reader->sampleRate, targetRate);
        normalizeTo(mono, padLevelTarget(pad));
        bank->data[static_cast<std::size_t>(pad)] = std::move(mono);
    }
    // Starter pads ship at 48 kHz; bring them to the device rate as well so
    // the engine always plays the bank 1:1.
    for (int pad = 0; pad < drumPads; ++pad)
    {
        if (!files[static_cast<std::size_t>(pad)].isEmpty())
            continue;
        resampleTo(bank->data[static_cast<std::size_t>(pad)], 48000.0, targetRate);
    }
    return bank;
}

juce::File kitsDir()
{
#if defined(__APPLE__)
    return sonora::sonoraSupportDir().getChildFile("kits");
#else
    auto root = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {});
    if (root.isEmpty() || !juce::File::isAbsolutePath(root))
        root = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile(".local/share").getFullPathName();
    return juce::File(root).getChildFile("sonora/kits");
#endif
}

juce::String sanitizePresetName(const juce::String& name)
{
    juce::String clean;
    for (const auto character : name.trim())
    {
        if (juce::CharacterFunctions::isLetterOrDigit(character) || character == '-' || character == '_')
            clean += character;
        else if (character == ' ' && !clean.endsWithChar('_') && clean.isNotEmpty())
            clean += "_";
    }
    return clean.substring(0, 40);
}

std::vector<juce::String> listKitPresets(const juce::File& baseDir)
{
    std::vector<juce::String> names;
    if (!baseDir.isDirectory())
        return names;
    for (const auto& entry : juce::RangedDirectoryIterator(baseDir, true, "*", juce::File::findDirectories))
    {
        const auto file = entry.getFile().getChildFile("kit.json");
        if (!file.existsAsFile())
            continue;
        juce::var parsed;
        if (juce::JSON::parse(file.loadFileAsString(), parsed).wasOk()
            && parsed.getDynamicObject() != nullptr
            && parsed.getDynamicObject()->getProperty("format").toString() == "sonora-kit")
            names.push_back(entry.getFile().getFileName());
    }
    std::sort(names.begin(), names.end());
    return names;
}

juce::Result saveKitPreset(const juce::File& baseDir, const juce::String& name, int variant,
                           const std::array<juce::String, drumPads>& files,
                           std::function<juce::File(const juce::String&)> resolve)
{
    const auto clean = sanitizePresetName(name);
    if (clean.isEmpty())
        return juce::Result::fail("Give the preset a name using letters and numbers.");
    if (variant < 0 || variant >= numKitVariants)
        return juce::Result::fail("Unknown kit variant.");
    const auto dir = baseDir.getChildFile(clean);
    if (dir.createDirectory().failed())
        return juce::Result::fail("Could not create the preset folder.");
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("format", "sonora-kit");
    root->setProperty("version", 1);
    root->setProperty("name", clean);
    root->setProperty("variant", variant);
    juce::Array<juce::var> stored;
    for (int pad = 0; pad < drumPads; ++pad)
    {
        const auto& file = files[static_cast<std::size_t>(pad)];
        if (file.isEmpty())
        {
            stored.add("");
            continue;
        }
        const auto source = resolve(file);
        if (!source.existsAsFile())
            return juce::Result::fail("Sample missing: " + file);
        auto target = dir.getChildFile(source.getFileName());
        if (!target.existsAsFile() && !source.copyFileTo(target))
            return juce::Result::fail("Could not store " + file);
        stored.add(target.getFileName());
    }
    root->setProperty("files", stored);
    const auto text = juce::JSON::toString(juce::var(root.release()));
    juce::TemporaryFile temporary(dir.getChildFile("kit.json"));
    {
        juce::FileOutputStream stream(temporary.getFile());
        if (!stream.openedOk())
            return stream.getStatus();
        if (!stream.writeText(text, false, false, "\n"))
            return juce::Result::fail("Could not write the preset.");
        stream.flush();
        if (stream.getStatus().failed())
            return stream.getStatus();
    }
    return temporary.overwriteTargetFileWithTemporary()
        ? juce::Result::ok() : juce::Result::fail("Could not replace the preset file.");
}

juce::Result loadKitPreset(const juce::File& baseDir, const juce::String& name, KitPreset& preset)
{
    const auto file = baseDir.getChildFile(name).getChildFile("kit.json");
    juce::var parsed;
    if (juce::JSON::parse(file.loadFileAsString(), parsed).failed() || parsed.getDynamicObject() == nullptr)
        return juce::Result::fail("Preset file is missing or corrupt.");
    const auto* root = parsed.getDynamicObject();
    if (root->getProperty("format").toString() != "sonora-kit")
        return juce::Result::fail("Not a Sonora kit preset.");
    const auto version = root->getProperty("version");
    if (!version.isInt() && !version.isInt64())
        return juce::Result::fail("Unsupported preset version.");
    const auto variant = root->getProperty("variant");
    if ((!variant.isInt() && !variant.isInt64()) || static_cast<int>(variant) < 0
        || static_cast<int>(variant) >= numKitVariants)
        return juce::Result::fail("Unknown kit variant.");
    const auto* files = root->getProperty("files").getArray();
    if (files == nullptr || files->size() != drumPads)
        return juce::Result::fail("Preset sample list is incomplete.");
    KitPreset candidate;
    candidate.name = root->getProperty("name").toString();
    candidate.variant = static_cast<int>(variant);
    for (int pad = 0; pad < drumPads; ++pad)
    {
        const auto fileName = (*files)[pad].toString();
        if (fileName.isNotEmpty()
            && !baseDir.getChildFile(name).getChildFile(fileName).existsAsFile())
            return juce::Result::fail("Preset sample missing: " + fileName);
        candidate.files[static_cast<std::size_t>(pad)] = fileName;
    }
    preset = candidate;
    return juce::Result::ok();
}

juce::Result deleteKitPreset(const juce::File& baseDir, const juce::String& name)
{
    const auto dir = baseDir.getChildFile(name);
    if (!dir.isDirectory() || !dir.getChildFile("kit.json").existsAsFile())
        return juce::Result::fail("Preset not found.");
    return dir.deleteRecursively() ? juce::Result::ok() : juce::Result::fail("Could not delete the preset.");
}
}
