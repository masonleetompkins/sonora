#pragma once
#include <array>

namespace sonora
{
struct InstrumentPreset { const char* name; const char* family; int program; };
// Stable, persisted indices. Append new entries; never reorder existing ones.
inline constexpr std::array<InstrumentPreset, 18> instruments {{
    { "Sine Keys", "Synth", -1 },
    { "Grand Piano", "Piano", 0 },
    { "Bright Piano", "Piano", 1 },
    { "Electric Piano", "Piano", 4 },
    { "Nylon Guitar", "Guitar", 24 },
    { "Steel Guitar", "Guitar", 25 },
    { "Jazz Guitar", "Guitar", 26 },
    { "Clean Electric Guitar", "Guitar", 27 },
    { "Overdriven Guitar", "Guitar", 29 },
    { "Acoustic Bass", "Bass", 32 },
    { "Fingered Bass", "Bass", 33 },
    { "Picked Bass", "Bass", 34 },
    { "Synth Bass", "Bass", 38 },
    { "Trumpet", "Brass", 56 },
    { "Trombone", "Brass", 57 },
    { "French Horn", "Brass", 60 },
    { "Brass Section", "Brass", 61 },
    { "String Ensemble", "Strings", 48 },
}};
inline bool validInstrument(int value) { return value >= 0 && value < static_cast<int>(instruments.size()); }
}
