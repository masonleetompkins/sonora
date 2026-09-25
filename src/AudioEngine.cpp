#include "AudioEngine.h"

namespace sonora
{
AudioEngine::AudioEngine()
{
    for (int i = 0; i < 16; ++i)
        synth.addVoice(new Voice());
    synth.addSound(new Sound());
    synth.setNoteStealingEnabled(false);
    // JUCE defaults to coalescing nearby MIDI events. Strict subdivision makes
    // timeline event offsets exact, even when they fall one sample apart.
    synth.setMinimumRenderingSubdivisionSize(1, true);
    midi.ensureSize(65536);
    renderMidi.ensureSize(65536);
    drumMidi.ensureSize(65536);
}

void AudioEngine::prepare(double sampleRate)
{
    rate = sampleRate;
    synth.setCurrentPlaybackSampleRate(rate);
    drums.prepare(rate);
    melodyChain.prepare(rate);
    drumChain.prepare(rate);
    master.prepare(rate);
    melodyGain.reset(rate, 0.01);
    melodyGain.setCurrentAndTargetValue(active.melodyMix.volume);
    midiCollector.reset(rate);
    scheduler.configure(rate, active.bpm);
    scheduler.rewind();
    wasPlaying = false;
}

void AudioEngine::process(const juce::AudioSourceChannelInfo& block)
{
    block.clearActiveBufferRegion();
    if (block.numSamples <= 0)
        return;
    auto incoming = active;
    // Bounded even if the producer keeps publishing while we consume.
    for (int i = 0; i < 7; ++i)
    {
        if (!pending.pop(incoming))
            break;
    }
    const bool tempoChanged = std::abs(incoming.bpm - active.bpm) > 1.0e-9;
    const bool melodyAudible = incoming.melodyMix.audible(incoming.drumMix);
    const bool drumsAudible = incoming.drumMix.audible(incoming.melodyMix);
    const bool melodyChanged = !(incoming.melodies == active.melodies) || tempoChanged
        || melodyAudible != active.melodyMix.audible(active.drumMix);
    const bool drumChanged = !(incoming.drumPatterns == active.drumPatterns) || tempoChanged;
    const bool drumAudibilityChanged = drumsAudible != active.drumMix.audible(active.melodyMix);
    active = incoming;
    if (tempoChanged)
        scheduler.configure(rate, active.bpm);
    const auto rewind = rewindRequest.load();
    const bool reset = rewind != lastRewind;
    lastRewind = rewind;
    const bool running = playing.load();
    const bool panicNow = panicRequested.exchange(false);
    if (reset || (!wasPlaying && running))
        scheduler.rewind();
    if (melodyChanged || reset || panicNow || running != wasPlaying)
        synth.allNotesOff(0, false);
    if (reset || panicNow || running != wasPlaying || drumAudibilityChanged)
        drums.stop();
    // Effect tails belong to the notes that made them; clear them with voices.
    if (melodyChanged || reset || panicNow || running != wasPlaying)
        melodyChain.reset();
    if (reset || panicNow || running != wasPlaying || drumAudibilityChanged)
        drumChain.reset();
    if (reset || panicNow)
        master.reset();
    if (!(incoming.melodyFx == activeMelodyFx))
    {
        melodyChain.setParams(incoming.melodyFx);
        activeMelodyFx = incoming.melodyFx;
    }
    if (!(incoming.drumFx == activeDrumFx))
    {
        drumChain.setParams(incoming.drumFx);
        activeDrumFx = incoming.drumFx;
    }
    if (!(incoming.master == activeMaster))
    {
        master.setParams(incoming.master);
        activeMaster = incoming.master;
    }

    midi.clear();
    midiCollector.removeNextBlockOfMessages(midi, block.numSamples);
    keyboardState.processNextMidiBuffer(midi, 0, block.numSamples, true);
    if (panicNow || reset)
        midi.clear();

    renderMidi.clear();
    drumMidi.clear();
    DrumHit hit {};
    for (int i = 0; i < 63 && auditions.pop(hit); ++i)
        if (!panicNow && !reset && drumsAudible)
            drumMidi.addEvent(juce::MidiMessage::noteOn(10, drumBaseNote + hit.pad,
                              static_cast<juce::uint8>(hit.velocity)), block.startSample);
    for (const auto metadata : midi)
    {
        if (metadata.numBytes > 3)
            continue;
        auto message = metadata.getMessage();
        if (message.getChannel() == 10)
        {
            if (message.isNoteOn() && drumsAudible)
            {
                int pad = -1;
                if (arturiaPads.load())
                    pad = midi::arturiaPadForNote(message.getNoteNumber());
                else
                    for (int candidate = 0; candidate < drumPads; ++candidate)
                        if (message.getNoteNumber() == drumMidiNotes[static_cast<std::size_t>(candidate)])
                            pad = candidate;
                if (pad >= 0)
                    drumMidi.addEvent(juce::MidiMessage::noteOn(10, drumBaseNote + pad, message.getVelocity()),
                                      metadata.samplePosition + block.startSample);
            }
        }
        else if (message.getChannel() > 0 && melodyAudible)
        {
            message.setChannel(1);
            renderMidi.addEvent(message, metadata.samplePosition + block.startSample);
        }
    }

    std::int64_t blockStartPosition = scheduler.samplePosition();
    // Loop mode previews the editor-selected library patterns; song mode
    // follows the arrangement. Indices arrive on atomics outside snapshots.
    const int loopMelody = std::clamp(loopMelodyPattern.load(), 0, numPatterns - 1);
    const int loopDrums = std::clamp(loopDrumPattern.load(), 0, numPatterns - 1);
    if (running && !panicNow)
    {
        const bool songMode = active.songMode;
        // blockStartPosition was captured above, before processSong() advances it.
        if (drumsAudible)
        {
            auto scheduleDrumEvents = [this, start = block.startSample](int pad, std::uint8_t velocity, int offset) {
                drumMidi.addEvent(juce::MidiMessage::noteOn(10, drumBaseNote + pad, velocity), start + offset);
            };
            if (songMode)
                scheduler.scheduleDrumsSong(active.drumPatterns, active.song, block.numSamples, scheduleDrumEvents);
            else
                scheduler.scheduleDrums(active.drumPatterns[static_cast<std::size_t>(loopDrums)],
                                        block.numSamples, scheduleDrumEvents);
        }
        auto emitNote = [this, start = block.startSample, melodyAudible](const Note& note, bool on, int offset) {
            // Channel 2 isolates sequenced voices from live keyboard notes.
            if (melodyAudible)
                renderMidi.addEvent(on ? juce::MidiMessage::noteOn(2, note.pitch, static_cast<juce::uint8>(note.velocity))
                                       : juce::MidiMessage::noteOff(2, note.pitch), start + offset);
        };
        bool finished = false;
        if (songMode)
            finished = scheduler.processSong(active.melodies, active.song, block.numSamples,
                                             melodyChanged || drumChanged, emitNote);
        else
            scheduler.processLoop(active.melodies[static_cast<std::size_t>(loopMelody)],
                                  block.numSamples, melodyChanged || drumChanged, emitNote);
        if (finished)
            playing.store(false);
    }
    synth.renderNextBlock(*block.buffer, renderMidi, block.startSample, block.numSamples);
    melodyChain.process(*block.buffer, block.startSample, block.numSamples);
    melodyGain.setTargetValue(melodyAudible ? active.melodyMix.volume : 0.0f);
    for (int frame = block.startSample; frame < block.startSample + block.numSamples; ++frame)
    {
        const auto gain = melodyGain.getNextValue();
        for (int channel = 0; channel < block.buffer->getNumChannels(); ++channel)
            block.buffer->getWritePointer(channel)[frame] *= gain;
    }
    drums.render(*block.buffer, block.startSample, block.numSamples, drumMidi,
                 drumsAudible ? active.drumMix.volume : 0.0f);
    drumChain.process(*block.buffer, block.startSample, block.numSamples);
    // Recorded takes play in song mode from their punch-in position. Takes are
    // preloaded PCM owned by the RCU set; this loop only reads, never allocates.
    // blockStartPosition was captured before the schedulers advanced above.
    if (running && !panicNow && active.songMode)
    {
        if (auto* set = takeSet.load(std::memory_order_acquire))
        {
            const auto position = blockStartPosition;
            const int numChannels = block.buffer->getNumChannels();
            for (const auto& take : set->takes)
            {
                if (take.mute || take.audio.getNumSamples() <= 0)
                    continue;
                const auto takeStart = scheduler.framesForTick(take.startTick);
                const auto offset = takeStart - position;
                const int from = static_cast<int>(std::max<std::int64_t>(0, offset));
                const int to = static_cast<int>(std::min<std::int64_t>(block.numSamples,
                    offset + take.audio.getNumSamples()));
                const int takeChannels = take.audio.getNumChannels();
                for (int i = from; i < to; ++i)
                {
                    const int frame = static_cast<int>(static_cast<std::int64_t>(i) - offset);
                    for (int channel = 0; channel < numChannels; ++channel)
                        block.buffer->addSample(channel, block.startSample + i,
                            take.audio.getSample(channel % takeChannels, frame) * take.gain);
                }
            }
        }
    }
    if (block.buffer->getNumChannels() >= 2)
        master.process(block.buffer->getWritePointer(0, block.startSample),
                       block.buffer->getWritePointer(1, block.startSample), block.numSamples);
    masterReductionDb.store(master.getReductionDb());
    outputPeak.store(block.buffer->getMagnitude(block.startSample, block.numSamples));
    wasPlaying = running;
    visibleTick.store(scheduler.tickPosition());
}

void AudioEngine::release()
{
    synth.allNotesOff(0, false);
    drums.stop();
    melodyChain.reset();
    drumChain.reset();
    master.reset();
    outputPeak.store(0.0f);
    masterReductionDb.store(0.0f);
    visibleTick.store(0.0);
}
}
