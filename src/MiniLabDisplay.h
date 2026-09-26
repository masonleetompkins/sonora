#pragma once
#include "KnobMaps.h"
#include <cstdint>
#include <vector>
#include <juce_core/juce_core.h>

namespace sonora::minilab
{
// Arturia MiniLab 3 host feedback (OLED text and pad RGB). Arturia does not
// publish this protocol; the messages below follow the hardware-verified
// MIT-licensed profile at github.com/oscarcs/codex-minilab3. Feedback only
// works in the MiniLab's DAW program (Shift + Pad 3); in the Arturia/User
// program the device ignores it, so every message here is harmless there.
using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint8_t header[] { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42 };
inline constexpr int line1Chars = 10, line2Chars = 18;
inline constexpr std::uint8_t padBankA = 0x30, padBankB = 0x40;

inline Bytes arturia(std::initializer_list<std::uint8_t> body)
{
    Bytes out(std::begin(header), std::end(header));
    out.insert(out.end(), body.begin(), body.end());
    out.push_back(0xF7);
    return out;
}

inline Bytes deviceInquiry() { return { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }; }
inline Bytes connectDaw() { return arturia({ 0x02, 0x02, 0x40, 0x6A, 0x21 }); }
inline Bytes disconnectDaw() { return arturia({ 0x02, 0x02, 0x40, 0x6A, 0x20 }); }
inline Bytes requestMode() { return arturia({ 0x01, 0x00, 0x40, 0x01 }); }
inline Bytes requestPadBank() { return arturia({ 0x01, 0x00, 0x40, 0x03 }); }

// Printable ASCII only (anything else becomes '?'), truncated to the line.
inline Bytes asciiLine(const juce::String& text, int maxChars)
{
    Bytes out;
    for (int i = 0; i < text.length() && static_cast<int>(out.size()) < maxChars; ++i)
    {
        const auto c = text[i];
        out.push_back(c >= 32 && c < 127 ? static_cast<std::uint8_t>(c) : static_cast<std::uint8_t>('?'));
    }
    return out;
}

inline Bytes screenMessage(const juce::String& line1, const juce::String& line2)
{
    Bytes out(std::begin(header), std::end(header));
    for (std::uint8_t b : { 0x04, 0x02, 0x60, 0x1F, 0x07, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01 })
        out.push_back(b);
    const auto top = asciiLine(line1, line1Chars);
    out.insert(out.end(), top.begin(), top.end());
    out.push_back(0x00);
    out.push_back(0x02);
    const auto bottom = asciiLine(line2, line2Chars);
    out.insert(out.end(), bottom.begin(), bottom.end());
    out.push_back(0x00);
    out.push_back(0xF7);
    return out;
}

// Eight pads, 7-bit RGB each, from a 24-bit colour scaled by brightness.
inline Bytes padBankMessage(std::uint8_t bank, std::uint32_t rgb, float brightness)
{
    Bytes out(std::begin(header), std::end(header));
    for (std::uint8_t b : { 0x04, 0x02, 0x16 })
        out.push_back(b);
    out.push_back(bank);
    const float level = juce::jlimit(0.0f, 1.0f, brightness);
    for (int pad = 0; pad < 8; ++pad)
        for (int shift : { 16, 8, 0 })
            out.push_back(static_cast<std::uint8_t>(juce::roundToInt(
                static_cast<float>((rgb >> shift) & 0xFF) / 255.0f * level * 127.0f)));
    out.push_back(0xF7);
    return out;
}

enum class Reply { None, DeviceIdentity, DawMode, ArturiaMode, DawModeChanged, ArturiaModeChanged, PadBankA, PadBankB, OtherState };

inline Reply classify(const std::uint8_t* data, int size)
{
    auto is = [&](const Bytes& expected) {
        return size == static_cast<int>(expected.size()) && std::equal(expected.begin(), expected.end(), data);
    };
    auto startsWith = [&](std::initializer_list<std::uint8_t> prefix) {
        return size >= static_cast<int>(prefix.size()) && std::equal(prefix.begin(), prefix.end(), data);
    };
    if (startsWith({ 0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x00, 0x20, 0x6B, 0x02, 0x00, 0x04 }))
        return Reply::DeviceIdentity;
    if (is(arturia({ 0x02, 0x00, 0x40, 0x01, 0x01 }))) return Reply::DawMode;
    if (is(arturia({ 0x02, 0x00, 0x40, 0x01, 0x00 }))) return Reply::ArturiaMode;
    if (is(arturia({ 0x02, 0x00, 0x40, 0x62, 0x02 }))) return Reply::DawModeChanged;
    if (is(arturia({ 0x02, 0x00, 0x40, 0x62, 0x01 }))) return Reply::ArturiaModeChanged;
    // Pad bank: 0x03 answers our request (observed on hardware), 0x63
    // announces a change made on the device.
    for (std::uint8_t id : { std::uint8_t { 0x03 }, std::uint8_t { 0x63 } })
    {
        if (is(arturia({ 0x02, 0x00, 0x40, id, 0x00 }))) return Reply::PadBankA;
        if (is(arturia({ 0x02, 0x00, 0x40, id, 0x01 }))) return Reply::PadBankB;
    }
    if (startsWith({ 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x00, 0x40 }))
        return Reply::OtherState;
    return Reply::None;
}

// Resting screen: which track the knobs control and what it is.
inline std::pair<juce::String, juce::String> trackScreen(const Track& track)
{
    juce::String kind = track.kind == TrackKind::Drums ? juce::String("Drum kit")
        : validInstrument(track.instrumentPreset) && track.instrumentPreset > 0
            ? juce::String(instruments[static_cast<std::size_t>(track.instrumentPreset)].name)
            : juce::String("Sonora Synth");
    return { track.trackName().toUpperCase(), kind };
}

// While turning: the knob's name up top, its value and the track below.
inline std::pair<juce::String, juce::String> knobScreen(const Track& track, int knob)
{
    const auto map = knobMapFor(track);
    const auto& slot = map[static_cast<std::size_t>(juce::jlimit(0, 7, knob))];
    const auto value = knobValueText(track, slot.target);
    const int room = line2Chars - value.length() - 2;
    auto line2 = value;
    if (room >= 3)
        line2 << "  " << track.trackName().substring(0, room);
    return { juce::String(slot.label).toUpperCase(), line2 };
}
}
