#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>

namespace sonora::midi
{
// MiniLab 3 factory pad layout (verified against the Arturia manual): both
// banks send on channel 10, bank A = notes 36-43, bank B = notes 44-51.
// Either bank drives our 8 drum voices in pad order, so every pad sounds
// without any configuration.
inline constexpr int arturiaBankANotes[8] { 36, 37, 38, 39, 40, 41, 42, 43 };
inline constexpr int arturiaBankBNotes[8] { 44, 45, 46, 47, 48, 49, 50, 51 };

// Returns the drum pad index for a channel-10 note under the Arturia map,
// or -1 when the note belongs to neither bank.
inline int arturiaPadForNote(int note)
{
    for (int pad = 0; pad < 8; ++pad)
        if (note == arturiaBankANotes[pad] || note == arturiaBankBNotes[pad])
            return pad;
    return -1;
}

// Standard Mackie Control transport note numbers (channel 1).
inline constexpr int mcuStopNote = 93, mcuPlayNote = 94, mcuRecordNote = 95, mcuCycleNote = 86;

enum class McuAction { None, Stop, Play, Record, ToggleLoop };

inline McuAction mcuTransportAction(int noteNumber, bool isNoteOn)
{
    if (!isNoteOn)
        return McuAction::None;
    switch (noteNumber)
    {
        case mcuStopNote: return McuAction::Stop;
        case mcuPlayNote: return McuAction::Play;
        case mcuRecordNote: return McuAction::Record;
        case mcuCycleNote: return McuAction::ToggleLoop;
        default: return McuAction::None;
    }
}

struct MidiPort
{
    juce::String identifier, name;
};

// Pure decision logic (unit-tested): which available ports to enable.
// Already-enabled ports are left alone; THRU ports are skipped to avoid
// doubling notes from external DIN gear or dead-end loopbacks.
struct AutoConnectResult
{
    std::vector<juce::String> enableIds;
    std::vector<juce::String> mcuIds;
    bool miniLabPresent = false;
};

inline bool isThruPort(const juce::String& name)
{
    return name.containsIgnoreCase("thru") || name.containsIgnoreCase("through");
}

inline AutoConnectResult midiAutoConnect(const std::vector<MidiPort>& available,
                                         const std::vector<juce::String>& enabledIds)
{
    AutoConnectResult result;
    auto enabled = [&](const juce::String& id) {
        for (const auto& known : enabledIds)
            if (known == id)
                return true;
        return false;
    };
    for (const auto& port : available)
    {
        if (port.name.containsIgnoreCase("minilab"))
            result.miniLabPresent = true;
        if (enabled(port.identifier))
        {
            if (port.name.containsIgnoreCase("mcu"))
                result.mcuIds.push_back(port.identifier);
            continue;
        }
        if (port.name.containsIgnoreCase("thru") || port.name.containsIgnoreCase("through"))
            continue;
        result.enableIds.push_back(port.identifier);
        if (port.name.containsIgnoreCase("mcu"))
            result.mcuIds.push_back(port.identifier);
    }
    return result;
}

// Short human-readable device summary for the status bar.
inline juce::String midiStatusText(const std::vector<MidiPort>& enabled)
{
    if (enabled.empty())
        return "no MIDI controller (plug one in — inputs auto-enable)";
    juce::String text;
    int shown = 0, musical = 0;
    for (const auto& port : enabled)
        if (!isThruPort(port.name))
            ++musical;
    for (const auto& port : enabled)
    {
        if (isThruPort(port.name))
            continue;
        if (shown == 3)
        {
            text += "+" + juce::String(musical - shown) + " more";
            break;
        }
        if (text.isNotEmpty())
            text += "+";
        auto shortName = port.name.upToFirstOccurrenceOf(" MIDI", false, false);
        text += shortName.isEmpty() ? port.name : shortName;
        ++shown;
    }
    return text.isEmpty() ? "no MIDI controller" : text;
}
}
