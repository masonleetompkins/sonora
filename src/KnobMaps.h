#pragma once
#include "Pattern.h"
#include <cmath>
#include <juce_core/juce_core.h>

namespace sonora
{
// MiniLab 3 encoders are absolute (0-127). Knobs 1-8 send these CCs in the
// Arturia/User program and in DAW mode respectively; both are accepted.
inline constexpr int miniLabKnobCcs[8] { 74, 71, 76, 77, 93, 18, 19, 16 };
inline constexpr int miniLabDawKnobCcs[8] { 86, 87, 89, 90, 110, 111, 116, 117 };

inline int knobIndexForController(int controller)
{
    for (int knob = 0; knob < 8; ++knob)
        if (controller == miniLabKnobCcs[knob] || controller == miniLabDawKnobCcs[knob])
            return knob;
    return -1;
}

enum class KnobTarget
{
    SynthCutoff, SynthResonance, SynthAttack, SynthRelease, SynthDrive, SynthChorus,
    Drive, EqLow, EqMid, EqHigh, Compress, Chorus,
    DelayMix, DelayTime, DelayFeedback, ReverbSize, ReverbMix, Volume
};

struct KnobSlot { const char* label; KnobTarget target; };
using KnobMap = std::array<KnobSlot, 8>;

// Eight controls per instrument family, ordered by how often you reach for
// them: tone-shaping first, space (delay/reverb) last.
inline KnobMap knobMapFor(const Track& track)
{
    using T = KnobTarget;
    if (track.kind == TrackKind::Drums)
        return {{ { "Drive", T::Drive }, { "Low", T::EqLow }, { "Mid", T::EqMid }, { "High", T::EqHigh },
                  { "Compress", T::Compress }, { "Delay", T::DelayMix }, { "Room", T::ReverbSize },
                  { "Reverb", T::ReverbMix } }};
    if (!validInstrument(track.instrumentPreset) || track.instrumentPreset == 0)
        return {{ { "Cutoff", T::SynthCutoff }, { "Resonance", T::SynthResonance }, { "Attack", T::SynthAttack },
                  { "Release", T::SynthRelease }, { "Drive", T::SynthDrive }, { "Chorus", T::SynthChorus },
                  { "Delay", T::DelayMix }, { "Reverb", T::ReverbMix } }};
    const juce::String family(instruments[static_cast<std::size_t>(track.instrumentPreset)].family);
    if (family == "Guitar")
        return {{ { "Distortion", T::Drive }, { "Tone", T::EqHigh }, { "Mids", T::EqMid }, { "Compress", T::Compress },
                  { "Chorus", T::Chorus }, { "Delay", T::DelayMix }, { "Echoes", T::DelayFeedback },
                  { "Reverb", T::ReverbMix } }};
    if (family == "Bass")
        return {{ { "Drive", T::Drive }, { "Low", T::EqLow }, { "Mids", T::EqMid }, { "Tone", T::EqHigh },
                  { "Compress", T::Compress }, { "Chorus", T::Chorus }, { "Reverb", T::ReverbMix },
                  { "Volume", T::Volume } }};
    if (family == "Brass")
        return {{ { "Tone", T::EqHigh }, { "Body", T::EqLow }, { "Mids", T::EqMid }, { "Compress", T::Compress },
                  { "Grit", T::Drive }, { "Delay", T::DelayMix }, { "Room", T::ReverbSize },
                  { "Reverb", T::ReverbMix } }};
    if (family == "Strings")
        return {{ { "Tone", T::EqHigh }, { "Body", T::EqLow }, { "Chorus", T::Chorus }, { "Compress", T::Compress },
                  { "Delay", T::DelayMix }, { "Echoes", T::DelayFeedback }, { "Room", T::ReverbSize },
                  { "Reverb", T::ReverbMix } }};
    // Pianos and anything added later.
    return {{ { "Tone", T::EqHigh }, { "Body", T::EqLow }, { "Compress", T::Compress }, { "Chorus", T::Chorus },
              { "Delay", T::DelayMix }, { "Delay time", T::DelayTime }, { "Room", T::ReverbSize },
              { "Reverb", T::ReverbMix } }};
}

namespace knobdetail
{
inline float logMap(float v, float lo, float hi) { return lo * std::pow(hi / lo, v); }
inline float logUnmap(float x, float lo, float hi) { return std::log(x / lo) / std::log(hi / lo); }
inline float eqMap(float v) { const float db = -15.0f + 30.0f * v; return std::abs(db) < 0.6f ? 0.0f : db; }
}

// Sets the target from a normalized knob position (0..1) and switches the
// owning effect on, so a bypassed effect never ignores a knob turn.
inline void applyKnob(Track& track, KnobTarget target, float v)
{
    using namespace knobdetail;
    v = juce::jlimit(0.0f, 1.0f, v);
    auto& fx = track.fx;
    auto& synth = track.synth;
    switch (target)
    {
        case KnobTarget::SynthCutoff: synth.cutoff = logMap(v, 40.0f, 20000.0f); break;
        case KnobTarget::SynthResonance: synth.resonance = 0.95f * v; break;
        case KnobTarget::SynthAttack: synth.attack = logMap(v, 0.001f, 5.0f); break;
        case KnobTarget::SynthRelease: synth.release = logMap(v, 0.001f, 5.0f); break;
        case KnobTarget::SynthDrive: synth.drive = v; break;
        case KnobTarget::SynthChorus: synth.chorus = v; break;
        case KnobTarget::Drive: fx.drive.amount = v; fx.drive.enabled = true; break;
        case KnobTarget::EqLow: fx.eq.low = eqMap(v); fx.eq.enabled = true; break;
        case KnobTarget::EqMid: fx.eq.mid = eqMap(v); fx.eq.enabled = true; break;
        case KnobTarget::EqHigh: fx.eq.high = eqMap(v); fx.eq.enabled = true; break;
        case KnobTarget::Compress:
            fx.comp.thresholdDb = -30.0f * v;
            fx.comp.ratio = 1.0f + 5.0f * v;
            fx.comp.enabled = true;
            break;
        case KnobTarget::Chorus: fx.chorus.mix = v; fx.chorus.enabled = true; break;
        case KnobTarget::DelayMix: fx.delay.mix = v; fx.delay.enabled = true; break;
        case KnobTarget::DelayTime: fx.delay.timeMs = logMap(v, 20.0f, 1000.0f); fx.delay.enabled = true; break;
        case KnobTarget::DelayFeedback: fx.delay.feedback = 0.8f * v; fx.delay.enabled = true; break;
        case KnobTarget::ReverbSize: fx.reverb.size = v; fx.reverb.enabled = true; break;
        case KnobTarget::ReverbMix: fx.reverb.mix = v; fx.reverb.enabled = true; break;
        case KnobTarget::Volume: track.mix.volume = 1.5f * v; break;
    }
}

// Current normalized position of a target, for on-screen knob readouts.
inline float knobPosition(const Track& track, KnobTarget target)
{
    using namespace knobdetail;
    const auto& fx = track.fx;
    const auto& synth = track.synth;
    switch (target)
    {
        case KnobTarget::SynthCutoff: return logUnmap(synth.cutoff, 40.0f, 20000.0f);
        case KnobTarget::SynthResonance: return synth.resonance / 0.95f;
        case KnobTarget::SynthAttack: return logUnmap(synth.attack, 0.001f, 5.0f);
        case KnobTarget::SynthRelease: return logUnmap(synth.release, 0.001f, 5.0f);
        case KnobTarget::SynthDrive: return synth.drive;
        case KnobTarget::SynthChorus: return synth.chorus;
        case KnobTarget::Drive: return fx.drive.amount;
        case KnobTarget::EqLow: return (fx.eq.low + 15.0f) / 30.0f;
        case KnobTarget::EqMid: return (fx.eq.mid + 15.0f) / 30.0f;
        case KnobTarget::EqHigh: return (fx.eq.high + 15.0f) / 30.0f;
        case KnobTarget::Compress: return -fx.comp.thresholdDb / 30.0f;
        case KnobTarget::Chorus: return fx.chorus.mix;
        case KnobTarget::DelayMix: return fx.delay.mix;
        case KnobTarget::DelayTime: return logUnmap(fx.delay.timeMs, 20.0f, 1000.0f);
        case KnobTarget::DelayFeedback: return fx.delay.feedback / 0.8f;
        case KnobTarget::ReverbSize: return fx.reverb.size;
        case KnobTarget::ReverbMix: return fx.reverb.mix;
        case KnobTarget::Volume: return track.mix.volume / 1.5f;
    }
    return 0.0f;
}

inline juce::String knobValueText(const Track& track, KnobTarget target)
{
    auto percent = [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
    auto db = [](float v) { return (v > 0.0f ? "+" : "") + juce::String(v, 1) + " dB"; };
    auto seconds = [](float s) { return s < 1.0f ? juce::String(juce::roundToInt(s * 1000.0f)) + " ms"
                                                 : juce::String(s, 2) + " s"; };
    const auto& fx = track.fx;
    using T = KnobTarget;
    if (target == T::SynthCutoff)
        return track.synth.cutoff >= 1000.0f ? juce::String(track.synth.cutoff / 1000.0f, 1) + " kHz"
                                             : juce::String(juce::roundToInt(track.synth.cutoff)) + " Hz";
    if (target == T::SynthAttack) return seconds(track.synth.attack);
    if (target == T::SynthRelease) return seconds(track.synth.release);
    if (target == T::EqLow) return db(fx.eq.low);
    if (target == T::EqMid) return db(fx.eq.mid);
    if (target == T::EqHigh) return db(fx.eq.high);
    if (target == T::DelayTime) return juce::String(juce::roundToInt(fx.delay.timeMs)) + " ms";
    if (target == T::Volume) return percent(track.mix.volume);
    return percent(juce::jlimit(0.0f, 1.0f, knobPosition(track, target)));
}
}
