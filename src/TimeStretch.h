#pragma once

// Offline take time-stretching via the vendored Rubber Band library
// (third_party/rubberband, single-file build). Worker thread only:
// allocation and blocking are expected, never call on the audio thread.

#include <juce_audio_basics/juce_audio_basics.h>

namespace sonora
{
// Output duration = input duration * timeRatio, pitch preserved. Ratios
// outside 0.1..4.0 are rejected (the take model clamps tighter anyway).
// Returns an empty buffer on failure or empty input.
juce::AudioBuffer<float> stretchAudio(const juce::AudioBuffer<float>& input, double sampleRate,
                                      double timeRatio);
} // namespace sonora
