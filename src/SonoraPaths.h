#pragma once
#include <juce_core/juce_core.h>

namespace sonora
{
// Writable per-user directory for settings, recovery snapshots, and kit
// presets. Linux keeps the existing XDG layout (see callers); macOS uses
// ~/Library/Application Support/Sonora.
inline juce::File sonoraSupportDir()
{
#if defined(__APPLE__)
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/Application Support/Sonora");
#else
    auto root = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {});
    if (root.isEmpty() || !juce::File::isAbsolutePath(root))
        root = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile(".local/share")
                   .getFullPathName();
    return juce::File(root).getChildFile("sonora");
#endif
}
} // namespace sonora
