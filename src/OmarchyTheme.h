#pragma once
#include <cstdint>
#include <map>
#include <juce_core/juce_core.h>

namespace sonora::omarchy
{
// Runtime palette in packed ARGB, read from the active Omarchy theme's
// colors.toml (or the built-in neon defaults when Omarchy is absent).
// juce_core only, so the parsing and mapping stay unit-testable without a UI.
struct Palette
{
    std::uint32_t background = 0xff080b12;
    std::uint32_t panel = 0xff101622;
    std::uint32_t raised = 0xff192231;
    std::uint32_t border = 0xff263346;
    std::uint32_t text = 0xffe7f0fc;
    std::uint32_t muted = 0xff8b9db7;
    std::uint32_t accent = 0xff57efd5;
    std::uint32_t melody = 0xff57efd5;
    std::uint32_t drums = 0xffb19aff;
    std::uint32_t audio = 0xff62aaff;
    std::uint32_t danger = 0xffff7c93;
    std::uint32_t warn = 0xffffb86b;
    bool dark = true;
    juce::String themeName = "sonora";
    bool operator==(const Palette&) const = default;
};

inline std::uint32_t mixArgb(std::uint32_t a, std::uint32_t b, float amount)
{
    const auto channel = [&](int shift) {
        const auto ca = static_cast<float>((a >> shift) & 0xff);
        const auto cb = static_cast<float>((b >> shift) & 0xff);
        return static_cast<std::uint32_t>(juce::jlimit(0.0f, 255.0f, ca + (cb - ca) * amount));
    };
    return 0xff000000u | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

inline std::uint32_t parseHexColor(const juce::String& text, std::uint32_t fallback)
{
    auto hex = text.trim();
    if (hex.startsWithChar('#'))
        hex = hex.substring(1);
    if (hex.length() != 6 && hex.length() != 3)
        return fallback;
    const auto digit = [](juce::juce_wchar character) -> int {
        if (character >= '0' && character <= '9')
            return character - '0';
        if (character >= 'a' && character <= 'f')
            return character - 'a' + 10;
        if (character >= 'A' && character <= 'F')
            return character - 'A' + 10;
        return -1;
    };
    std::uint32_t rgb = 0;
    if (hex.length() == 6)
    {
        for (int i = 0; i < 6; ++i)
        {
            const auto value = digit(hex[i]);
            if (value < 0)
                return fallback;
            rgb = (rgb << 4) | static_cast<std::uint32_t>(value);
        }
    }
    else
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto value = digit(hex[i]);
            if (value < 0)
                return fallback;
            rgb = (rgb << 8) | static_cast<std::uint32_t>(value * 16 + value);
        }
    }
    return 0xff000000u | rgb;
}

// Minimal flat-TOML reader: `key = "value"` lines, # comments, [sections]
// and anything else ignored. Values may be quoted or bare.
inline std::map<juce::String, juce::String> parseFlatToml(const juce::String& text)
{
    std::map<juce::String, juce::String> values;
    for (const auto& rawLine : juce::StringArray::fromLines(text))
    {
        auto line = rawLine.trim();
        if (line.isEmpty() || line.startsWithChar('#') || line.startsWithChar('['))
            continue;
        const auto equals = line.indexOfChar('=');
        if (equals < 0)
            continue;
        const auto key = line.substring(0, equals).trim();
        auto value = line.substring(equals + 1).trim();
        if (value.startsWithChar('#'))
        {
            // A hex color ends at whitespace; the rest is a trailing comment.
            const auto space = value.indexOfChar(' ');
            if (space > 0)
                value = value.substring(0, space);
        }
        else
        {
            // Strip trailing comments outside quotes (good enough for palettes).
            const auto hash = value.indexOfChar('#');
            if (hash > 0 && !value.substring(0, hash).containsChar('"'))
                value = value.substring(0, hash).trim();
        }
        if (value.length() >= 2 && value.startsWithChar('"') && value.endsWithChar('"'))
            value = value.substring(1, value.length() - 1);
        if (key.isNotEmpty())
            values[key] = value;
    }
    return values;
}

inline Palette paletteFromMap(const std::map<juce::String, juce::String>& values, juce::String themeName)
{
    Palette palette;
    palette.themeName = themeName;
    const auto color = [&](const char* key, std::uint32_t fallback) {
        const auto found = values.find(key);
        return found != values.end() ? parseHexColor(found->second, fallback) : fallback;
    };
    palette.background = color("background", palette.background);
    palette.text = color("foreground", palette.text);
    palette.muted = color("muted", palette.muted);
    // Surfaces derive from bg/fg so both dark and light themes stay readable.
    palette.panel = mixArgb(palette.background, palette.text, 0.07f);
    palette.raised = mixArgb(palette.background, palette.text, 0.13f);
    palette.border = mixArgb(palette.background, palette.text, 0.28f);
    palette.accent = color("cyan", palette.accent);
    palette.melody = color("cyan", palette.melody);
    palette.drums = color("magenta", palette.drums);
    palette.audio = color("blue", palette.audio);
    palette.danger = color("red", palette.danger);
    palette.warn = color("orange", palette.warn);
    const auto mode = values.find("mode");
    palette.dark = mode == values.end() || mode->second.trim() != "light";
    return palette;
}

inline juce::File themeColorsFile()
{
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const auto nameFile = home.getChildFile(".local/state/omarchy/current/theme.name");
    const auto name = nameFile.existsAsFile() ? nameFile.loadFileAsString().trim() : juce::String();
    if (name.isNotEmpty())
    {
        const auto themed = home.getChildFile(".config/omarchy/themes/" + name + "/colors.toml");
        if (themed.existsAsFile())
            return themed;
    }
    return home.getChildFile(".local/state/omarchy/current/theme/colors.toml");
}

inline std::uint64_t themeFingerprint()
{
    const auto file = themeColorsFile();
    if (!file.existsAsFile())
        return 0;
    const auto stamp = file.getLastModificationTime().toMilliseconds();
    return static_cast<std::uint64_t>(stamp) * 1000003ull + static_cast<std::uint64_t>(file.getSize());
}

inline Palette loadOmarchyPalette()
{
    const auto file = themeColorsFile();
    if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > 65536)
        return {};
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const auto nameFile = home.getChildFile(".local/state/omarchy/current/theme.name");
    const auto name = nameFile.existsAsFile() ? nameFile.loadFileAsString().trim()
                                              : file.getParentDirectory().getFileName();
    return paletteFromMap(parseFlatToml(file.loadFileAsString()), name.isEmpty() ? "omarchy" : name);
}

// Built-in appearance modes for the header theme toggle. The dark one
// refines the classic neon look; the light one is the Daylight Paper design
// (warm paper, ink text, deep teal accent, dark piano-roll inset preserved).
enum class ThemeMode { System, Light, Dark };

inline const char* themeModeName(ThemeMode mode)
{
    switch (mode)
    {
        case ThemeMode::System: return "system";
        case ThemeMode::Light: return "light";
        case ThemeMode::Dark: return "dark";
    }
    return "system";
}

inline ThemeMode themeModeFromString(const juce::String& name)
{
    const auto lower = name.trim().toLowerCase();
    if (lower == "light")
        return ThemeMode::Light;
    if (lower == "dark")
        return ThemeMode::Dark;
    return ThemeMode::System;
}

inline Palette sonoraDarkPalette()
{
    Palette palette;
    palette.background = 0xff080b12;
    palette.panel = 0xff101622;
    palette.raised = 0xff192231;
    palette.border = 0xff263346;
    palette.text = 0xffe7f0fc;
    palette.muted = 0xff8b9db7;
    palette.accent = 0xff57efd5;
    palette.melody = 0xff57efd5;
    palette.drums = 0xffb19aff;
    palette.audio = 0xff62aaff;
    palette.danger = 0xffff7c93;
    palette.warn = 0xffffb86b;
    palette.dark = true;
    palette.themeName = "sonora";
    return palette;
}

inline Palette sonoraLightPalette()
{
    Palette palette;
    palette.background = 0xfff3eee2;
    palette.panel = 0xfffdfaf1;
    palette.raised = 0xffffffff;
    palette.border = 0xffd9d2c1;
    palette.text = 0xff1c2433;
    palette.muted = 0xff68738a;
    palette.accent = 0xff0e7c7b;
    palette.melody = 0xff0e7c7b;
    palette.drums = 0xff7a4fd0;
    palette.audio = 0xff2f6bde;
    palette.danger = 0xffc23b4e;
    palette.warn = 0xffa86a12;
    palette.dark = false;
    palette.themeName = "sonora-light";
    return palette;
}
}
