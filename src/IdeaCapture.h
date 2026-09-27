#pragma once
#include "Pattern.h"
#include "PitchCorrect.h"
#include "MidiHardware.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace sonora
{
// Captured input timestamps are seconds from the idea's start, independent
// of transport state. Conversion fits the phrase to one four-bar pattern.
struct IdeaEvent
{
    double seconds = 0.0;
    int pitch = 60, velocity = 100, channel = 1;
    bool on = true;
};

struct IdeaResult
{
    Pattern melody;
    DrumPattern drums;
    bool fromAudio = false;
    int count(TrackKind kind) const { return kind == TrackKind::Drums ? drums.hitCount() : melody.count; }
};

// Convert played keys/pads into a quantized, proportionally fitted phrase.
IdeaResult interpretIdea(const std::vector<IdeaEvent>& events, TrackKind kind, double bpm, double captureSeconds);
// Monophonic voice/hummed pitch idea -> melody. Called off the audio thread.
IdeaResult interpretAudioIdea(const float* samples, int frames, double rate, double bpm);
}
