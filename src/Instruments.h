#pragma once
#include <array>

namespace sonora
{
struct InstrumentPreset { const char* name; const char* family; int program; };
// Stable, persisted indices. Append new entries; never reorder existing ones.
inline constexpr std::array<InstrumentPreset, 86> instruments {{
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
    // Added in 0.34. The picker groups by family, so order here is only
    // identity; keep appending.
    { "Honky-Tonk Piano", "Piano", 3 },
    { "FM Electric Piano", "Piano", 5 },
    { "Harpsichord", "Piano", 6 },
    { "Clavinet", "Piano", 7 },
    { "Celesta", "Mallets", 8 },
    { "Glockenspiel", "Mallets", 9 },
    { "Music Box", "Mallets", 10 },
    { "Vibraphone", "Mallets", 11 },
    { "Marimba", "Mallets", 12 },
    { "Xylophone", "Mallets", 13 },
    { "Tubular Bells", "Mallets", 14 },
    { "Steel Drums", "Mallets", 114 },
    { "Drawbar Organ", "Organ", 16 },
    { "Rock Organ", "Organ", 18 },
    { "Church Organ", "Organ", 19 },
    { "Accordion", "Organ", 21 },
    { "Muted Guitar", "Guitar", 28 },
    { "Distortion Guitar", "Guitar", 30 },
    { "Fretless Bass", "Bass", 35 },
    { "Slap Bass", "Bass", 36 },
    { "Synth Bass 2", "Bass", 39 },
    { "Violin", "Strings", 40 },
    { "Viola", "Strings", 41 },
    { "Cello", "Strings", 42 },
    { "Contrabass", "Strings", 43 },
    { "Tremolo Strings", "Strings", 44 },
    { "Pizzicato Strings", "Strings", 45 },
    { "Harp", "Strings", 46 },
    { "Synth Strings", "Strings", 50 },
    { "Choir Aahs", "Choir", 52 },
    { "Voice Oohs", "Choir", 53 },
    { "Synth Voice", "Choir", 54 },
    { "Tuba", "Brass", 58 },
    { "Muted Trumpet", "Brass", 59 },
    { "Analog Brass", "Brass", 62 },
    { "Soprano Sax", "Woodwind", 64 },
    { "Alto Sax", "Woodwind", 65 },
    { "Tenor Sax", "Woodwind", 66 },
    { "Baritone Sax", "Woodwind", 67 },
    { "Oboe", "Woodwind", 68 },
    { "Bassoon", "Woodwind", 70 },
    { "Clarinet", "Woodwind", 71 },
    { "Piccolo", "Woodwind", 72 },
    { "Flute", "Woodwind", 73 },
    { "Recorder", "Woodwind", 74 },
    { "Pan Flute", "Woodwind", 75 },
    { "Square Lead", "Synth Lead", 80 },
    { "Calliope Lead", "Synth Lead", 82 },
    { "Chiff Lead", "Synth Lead", 83 },
    { "Charang Lead", "Synth Lead", 84 },
    { "Voice Lead", "Synth Lead", 85 },
    { "Fifths Lead", "Synth Lead", 86 },
    { "Bass and Lead", "Synth Lead", 87 },
    { "New Age Pad", "Synth Pad", 88 },
    { "Polysynth Pad", "Synth Pad", 90 },
    { "Bowed Pad", "Synth Pad", 92 },
    { "Halo Pad", "Synth Pad", 94 },
    { "Sweep Pad", "Synth Pad", 95 },
    { "Harmonica", "World", 22 },
    { "Sitar", "World", 104 },
    { "Banjo", "World", 105 },
    { "Koto", "World", 107 },
    { "Kalimba", "World", 108 },
    { "Fiddle", "World", 110 },
    { "Timpani", "Percussion", 47 },
    { "Woodblock", "Percussion", 115 },
    { "Taiko Drum", "Percussion", 116 },
    // Program -2: not from the sound bank. Plays the track's own audio file.
    { "Sampler", "Sampler", -2 },
}};
inline constexpr int samplerInstrument = 85;
static_assert(instruments[samplerInstrument].program == -2, "sampler must keep its table index");
inline bool isSamplerInstrument(int value) { return value == samplerInstrument; }
// True for presets that come from the bundled sound bank (not the synth or sampler).
inline bool isBankInstrument(int value)
{
    return value >= 0 && value < static_cast<int>(instruments.size())
        && instruments[static_cast<std::size_t>(value)].program >= 0;
}
inline bool validInstrument(int value) { return value >= 0 && value < static_cast<int>(instruments.size()); }
}
