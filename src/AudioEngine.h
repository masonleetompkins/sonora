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
class AudioEngine
{
public:
    AudioEngine();
    bool submit(const ProjectState& project) { return project.valid() && pending.push(project); }
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
    // Same RCU protocol for the drum sample bank. Null restores built-ins.
    const SampleBank* retirePadBank(const SampleBank* next)
    {
        return drums.requestBank(next);
    }
    bool auditionDrum(int pad, int velocity = 100)
    {
        return pad >= 0 && pad < drumPads && velocity > 0 && velocity <= 127
            && auditions.push({ pad, velocity });
    }
    // When a MiniLab is connected, channel-10 notes follow the Arturia pad
    // map (banks A/B); otherwise the fixed Sonora drum-note list applies.
    void setArturiaPadMap(bool value) { arturiaPads.store(value); }
    // Loop-mode preview follows the editor's selected library patterns.
    void setLoopPatterns(int melodySlot, int drumSlot)
    {
        loopMelodyPattern.store(std::clamp(melodySlot, 0, numPatterns - 1));
        loopDrumPattern.store(std::clamp(drumSlot, 0, numPatterns - 1));
    }
    void prepare(double sampleRate);
    void process(const juce::AudioSourceChannelInfo& block);
    void release();
    juce::MidiKeyboardState keyboardState;
    juce::MidiMessageCollector midiCollector;

private:
    juce::Synthesiser synth;
    DrumSampler drums;
    TrackChain melodyChain, drumChain;
    BrickLimiter master;
    juce::MidiBuffer midi, renderMidi, drumMidi;
    juce::SmoothedValue<float> melodyGain;
    struct DrumHit { int pad, velocity; };
    SnapshotQueue<DrumHit, 64> auditions;
    LoopScheduler scheduler;
    SnapshotQueue<ProjectState> pending;
    ProjectState active;
    std::atomic<const TakeSet*> takeSet { nullptr };
    double rate = 48000.0;
    bool wasPlaying = false;
    std::atomic<bool> playing { false }, panicRequested { false };
    std::atomic<bool> arturiaPads { false };
    std::atomic<int> loopMelodyPattern { 0 }, loopDrumPattern { 0 };
    std::atomic<unsigned> rewindRequest { 0 };
    unsigned lastRewind = 0;
    std::atomic<double> visibleTick { 0.0 };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> masterReductionDb { 0.0f };
    TrackFx activeMelodyFx, activeDrumFx;
    LimiterParams activeMaster;
};
}
