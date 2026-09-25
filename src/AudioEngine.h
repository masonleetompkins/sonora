#pragma once
#include "Synth.h"
#include "DrumSampler.h"
#include "Fx.h"
#include "LoopScheduler.h"
#include "MidiHardware.h"
#include "SnapshotQueue.h"
#include <juce_audio_devices/juce_audio_devices.h>

namespace sonora
{
// One realtime processing unit per instrument track: its own synth voices or
// drum voices, insert chain, fader smoothing, and event buffer. Units for
// empty slots sit idle. Sequenced notes arrive on MIDI channel 2 + index so
// voices never steal across tracks.
struct TrackUnit
{
    juce::Synthesiser synth;
    DrumSampler drums;
    TrackChain chain;
    juce::SmoothedValue<float> gain;
    juce::MidiBuffer events;
    TrackFx activeFx;
};

class AudioEngine
{
public:
    AudioEngine();
    bool submit(const ProjectState& project)
    {
        if (!project.valid() || !pending.push(project))
            return false;
        latest = project;
        return true;
    }
    void setPlaying(bool value) { if (value) playing.store(true); else stop(); }
    bool isPlaying() const { return playing.load(); }
    void stop() { playing.store(false); rewindRequest.fetch_add(1); }
    void panic() { panicRequested.store(true); }
    double getTickPosition() const { return visibleTick.load(); }
    float getOutputPeak() const { return outputPeak.load(); }
    float getMasterReductionDb() const { return masterReductionDb.load(); }
    // RCU handoff: the caller builds a new set on the message thread, swaps it
    // in, and keeps the returned (previous) set alive for a grace period before
    // deleting it. The audio thread only loads the pointer, never frees it.
    const TakeSet* retireTakeSet(const TakeSet* next)
    {
        return takeSet.exchange(next, std::memory_order_acq_rel);
    }
    // Same RCU protocol per drum track. Null restores built-ins.
    const SampleBank* retirePadBank(int track, const SampleBank* next);
    bool auditionDrum(int track, int pad, int velocity = 100);
    // When a MiniLab is connected, channel-10 notes follow the Arturia pad
    // map (banks A/B); otherwise the fixed Sonora drum-note list applies.
    void setArturiaPadMap(bool value) { arturiaPads.store(value); }
    // Loop-mode preview follows the editor's track and library slots.
    void setLoopSelection(int track, int melodySlot, int drumSlot);
    void prepare(double sampleRate);
    void process(const juce::AudioSourceChannelInfo& block);
    void release();
    juce::MidiKeyboardState keyboardState;
    juce::MidiMessageCollector midiCollector;

private:
    bool audible(int track, const ProjectState& state) const;
    std::array<TrackUnit, maxTracks> units;
    BrickLimiter master;
    juce::MidiBuffer midi;
    struct DrumHit { int track, pad, velocity; };
    static constexpr int wildcardTrack = -1;
    SnapshotQueue<DrumHit, 64> auditions;
    LoopScheduler scheduler;
    SnapshotQueue<ProjectState> pending;
    ProjectState active;
    // Last submitted project, message thread only: validates auditions against
    // the editor state even before the audio thread picks the block up.
    ProjectState latest;
    std::atomic<const TakeSet*> takeSet { nullptr };
    double rate = 48000.0;
    bool wasPlaying = false;
    std::atomic<bool> playing { false }, panicRequested { false };
    std::atomic<bool> arturiaPads { false };
    std::atomic<int> loopTrack { 0 };
    // Per-track loop-preview slots: every track previews its own library slot.
    std::array<std::atomic<int>, maxTracks> loopMelodySlots {}, loopDrumSlots {};
    std::atomic<unsigned> rewindRequest { 0 };
    unsigned lastRewind = 0;
    std::atomic<double> visibleTick { 0.0 };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> masterReductionDb { 0.0f };
    LimiterParams activeMaster;
};
}
