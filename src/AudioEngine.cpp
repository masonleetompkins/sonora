#include "AudioEngine.h"

namespace sonora
{
AudioEngine::AudioEngine()
{
    for (auto& unit : units)
    {
        for (int i = 0; i < 16; ++i)
            unit.synth.addVoice(new Voice());
        unit.synth.addSound(new Sound());
        unit.synth.setNoteStealingEnabled(false);
        // JUCE defaults to coalescing nearby MIDI events. Strict subdivision
        // makes timeline event offsets exact, even when they fall one sample apart.
        unit.synth.setMinimumRenderingSubdivisionSize(1, true);
        unit.events.ensureSize(8192);
    }
    midi.ensureSize(65536);
}

bool AudioEngine::audible(int track, const ProjectState& state) const
{
    const auto& mix = state.tracks[static_cast<std::size_t>(track)].mix;
    if (mix.mute)
        return false;
    if (mix.solo)
        return true;
    for (const auto& other : state.tracks)
        if (other.kind != TrackKind::None && other.mix.solo)
            return false;
    return true;
}

const SampleBank* AudioEngine::retirePadBank(int track, const SampleBank* next)
{
    if (track < 0 || track >= maxTracks)
        return nullptr;
    return units[static_cast<std::size_t>(track)].drums.requestBank(next);
}

bool AudioEngine::auditionDrum(int track, int pad, int velocity)
{
    if (track < 0 || track >= maxTracks || pad < 0 || pad >= drumPads || velocity <= 0 || velocity > 127)
        return false;
    if (latest.tracks[static_cast<std::size_t>(track)].kind != TrackKind::Drums)
        return false;
    return auditions.push({ track, pad, velocity });
}

void AudioEngine::setLoopSelection(int track, int melodySlot, int drumSlot)
{
    loopTrack.store(std::clamp(track, 0, maxTracks - 1));
    loopMelodySlots[static_cast<std::size_t>(std::clamp(track, 0, maxTracks - 1))].store(
        std::clamp(melodySlot, 0, numPatterns - 1));
    loopDrumSlots[static_cast<std::size_t>(std::clamp(track, 0, maxTracks - 1))].store(
        std::clamp(drumSlot, 0, numPatterns - 1));
}

void AudioEngine::prepare(double sampleRate)
{
    rate = sampleRate;
    for (auto& unit : units)
    {
        unit.synth.setCurrentPlaybackSampleRate(rate);
        unit.drums.prepare(rate);
        unit.chain.prepare(rate);
        unit.gain.reset(rate, 0.01);
        unit.gain.setCurrentAndTargetValue(1.0f);
    }
    master.prepare(rate);
    midiCollector.reset(rate);
    scheduler.configure(rate, active.bpm);
    scheduler.rewind();
    wasPlaying = false;
    // Re-apply effect parameters: prepare() resets DSP state to defaults.
    for (int track = 0; track < maxTracks; ++track)
    {
        auto& unit = units[static_cast<std::size_t>(track)];
        unit.chain.setParams(active.tracks[static_cast<std::size_t>(track)].fx);
        unit.activeFx = active.tracks[static_cast<std::size_t>(track)].fx;
    }
    master.setParams(active.master);
    activeMaster = active.master;
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
    const bool tracksChanged = !(incoming.tracks == active.tracks);
    const bool songToggled = incoming.songMode != active.songMode;
    bool audibilityChanged = false;
    for (int track = 0; track < maxTracks; ++track)
    {
        if (audible(track, incoming) != audible(track, active))
        {
            audibilityChanged = true;
            break;
        }
    }
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
    const bool voicesChanged = tracksChanged || tempoChanged || reset || panicNow || running != wasPlaying;
    if (voicesChanged || songToggled)
        for (auto& unit : units)
            unit.synth.allNotesOff(0, false);
    if (reset || panicNow || running != wasPlaying || audibilityChanged || songToggled)
        for (auto& unit : units)
            unit.drums.stop();
    // Effect tails belong to the notes that made them; clear them with voices.
    if (voicesChanged || songToggled)
        for (auto& unit : units)
            unit.chain.reset();
    if (reset || panicNow)
        master.reset();
    for (int track = 0; track < maxTracks; ++track)
    {
        auto& unit = units[static_cast<std::size_t>(track)];
        if (!(incoming.tracks[static_cast<std::size_t>(track)].fx == unit.activeFx))
        {
            unit.chain.setParams(incoming.tracks[static_cast<std::size_t>(track)].fx);
            unit.activeFx = incoming.tracks[static_cast<std::size_t>(track)].fx;
        }
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

    for (auto& unit : units)
        unit.events.clear();
    DrumHit hit {};
    for (int i = 0; i < 63 && auditions.pop(hit); ++i)
    {
        if (panicNow || reset)
            continue;
        int target = hit.track;
        if (target == wildcardTrack)
        {
            target = -1;
            const int selected = std::clamp(loopTrack.load(), 0, maxTracks - 1);
            if (active.tracks[static_cast<std::size_t>(selected)].kind == TrackKind::Drums)
                target = selected;
            for (int track = 0; target < 0 && track < maxTracks; ++track)
                if (active.tracks[static_cast<std::size_t>(track)].kind == TrackKind::Drums)
                    target = track;
        }
        if (target < 0 || target >= maxTracks
            || active.tracks[static_cast<std::size_t>(target)].kind != TrackKind::Drums
            || !audible(target, active))
            continue;
        units[static_cast<std::size_t>(target)].events.addEvent(
            juce::MidiMessage::noteOn(10, drumBaseNote + hit.pad, static_cast<juce::uint8>(hit.velocity)),
            block.startSample);
    }
    // Live input routing: channel 10 plays the selected-or-first drum track,
    // everything else plays the selected-or-first synth track.
    const int liveTrack = std::clamp(loopTrack.load(), 0, maxTracks - 1);
    int drumTarget = -1, synthTarget = -1;
    if (active.tracks[static_cast<std::size_t>(liveTrack)].kind == TrackKind::Drums)
        drumTarget = liveTrack;
    if (active.tracks[static_cast<std::size_t>(liveTrack)].kind == TrackKind::Synth)
        synthTarget = liveTrack;
    for (int track = 0; track < maxTracks; ++track)
    {
        const auto kind = active.tracks[static_cast<std::size_t>(track)].kind;
        if (kind == TrackKind::Drums && drumTarget < 0)
            drumTarget = track;
        if (kind == TrackKind::Synth && synthTarget < 0)
            synthTarget = track;
    }
    for (const auto metadata : midi)
    {
        if (metadata.numBytes > 3)
            continue;
        auto message = metadata.getMessage();
        if (message.getChannel() == 10)
        {
            if (message.isNoteOn() && drumTarget >= 0 && audible(drumTarget, active))
            {
                int pad = -1;
                if (arturiaPads.load())
                    pad = midi::arturiaPadForNote(message.getNoteNumber());
                else
                    for (int candidate = 0; candidate < drumPads; ++candidate)
                        if (message.getNoteNumber() == drumMidiNotes[static_cast<std::size_t>(candidate)])
                            pad = candidate;
                if (pad >= 0)
                    units[static_cast<std::size_t>(drumTarget)].events.addEvent(
                        juce::MidiMessage::noteOn(10, drumBaseNote + pad, message.getVelocity()),
                        metadata.samplePosition + block.startSample);
            }
        }
        else if (message.getChannel() > 0 && synthTarget >= 0 && audible(synthTarget, active))
        {
            // Channel 1 isolates live keys from sequenced voices (2 + track).
            message.setChannel(1);
            units[static_cast<std::size_t>(synthTarget)].events.addEvent(
                message, metadata.samplePosition + block.startSample);
        }
    }

    std::int64_t blockStartPosition = scheduler.samplePosition();
    if (running && !panicNow)
    {
        const bool songMode = active.songMode;
        // blockStartPosition was captured above, before processSong() advances it.
        if (songMode)
        {
            for (int track = 0; track < maxTracks; ++track)
            {
                if (active.tracks[static_cast<std::size_t>(track)].kind != TrackKind::Drums
                    || !audible(track, active))
                    continue;
                scheduler.scheduleDrumsSong(active.tracks[static_cast<std::size_t>(track)].drumPatterns,
                                            active.song, track, block.numSamples,
                    [this, track, start = block.startSample](int pad, std::uint8_t velocity, int offset) {
                        units[static_cast<std::size_t>(track)].events.addEvent(
                            juce::MidiMessage::noteOn(10, drumBaseNote + pad, velocity), start + offset);
                    });
            }
        }
        auto emitNote = [this, start = block.startSample](int track, const Note& note, bool on, int offset) {
            // Channel 2 + track isolates sequenced voices from live keys.
            if (audible(track, active))
                units[static_cast<std::size_t>(track)].events.addEvent(
                    on ? juce::MidiMessage::noteOn(2 + track, note.pitch, static_cast<juce::uint8>(note.velocity))
                       : juce::MidiMessage::noteOff(2 + track, note.pitch),
                    start + offset);
        };
        bool finished = false;
        if (songMode)
            finished = scheduler.processSong(active.tracks, active.song, block.numSamples,
                                             tracksChanged || tempoChanged, emitNote);
        else
        {
            for (int track = 0; track < maxTracks; ++track)
            {
                const auto kind = active.tracks[static_cast<std::size_t>(track)].kind;
                if (kind == TrackKind::None || !audible(track, active))
                    continue;
                const auto trackIndex = static_cast<std::size_t>(track);
                const int loopMelody = std::clamp(
                    loopMelodySlots[trackIndex].load(), 0, numPatterns - 1);
                const int loopDrums = std::clamp(
                    loopDrumSlots[trackIndex].load(), 0, numPatterns - 1);
                if (kind == TrackKind::Synth)
                    scheduler.processLoopAt(active.tracks[static_cast<std::size_t>(track)].melodies[
                                                static_cast<std::size_t>(loopMelody)],
                                            block.numSamples, tracksChanged || tempoChanged,
                                            blockStartPosition,
                                            [this, track, &emitNote](const Note& note, bool on, int offset) {
                                                emitNote(track, note, on, offset);
                                            });
                else
                {
                    scheduler.scheduleDrumsAt(active.tracks[static_cast<std::size_t>(track)].drumPatterns[
                                                  static_cast<std::size_t>(loopDrums)],
                                              block.numSamples, blockStartPosition,
                        [this, track, start = block.startSample](int pad, std::uint8_t velocity, int offset) {
                            units[static_cast<std::size_t>(track)].events.addEvent(
                                juce::MidiMessage::noteOn(10, drumBaseNote + pad, velocity), start + offset);
                        });
                }
            }
            scheduler.advance(block.numSamples);
        }
        if (finished)
            playing.store(false);
    }
    // Render every track through its own voices, chain, and fader.
    for (int track = 0; track < maxTracks; ++track)
    {
        auto& unit = units[static_cast<std::size_t>(track)];
        const auto kind = active.tracks[static_cast<std::size_t>(track)].kind;
        if (kind == TrackKind::None)
            continue;
        if (kind == TrackKind::Synth)
            unit.synth.renderNextBlock(*block.buffer, unit.events, block.startSample, block.numSamples);
        else
            unit.drums.render(*block.buffer, block.startSample, block.numSamples, unit.events,
                              audible(track, active)
                                  ? active.tracks[static_cast<std::size_t>(track)].mix.volume
                                  : 0.0f);
        unit.chain.process(*block.buffer, block.startSample, block.numSamples);
        // The drum sampler applies its own track volume; the synth fader is
        // smoothed here. Muted tracks were already gated at event time, but
        // the fader still silences tails on mute toggles.
        const float target = kind == TrackKind::Synth && audible(track, active)
            ? active.tracks[static_cast<std::size_t>(track)].mix.volume : 0.0f;
        unit.gain.setTargetValue(kind == TrackKind::Drums ? 1.0f : target);
        for (int frame = block.startSample; frame < block.startSample + block.numSamples; ++frame)
        {
            const auto gain = unit.gain.getNextValue();
            for (int channel = 0; channel < block.buffer->getNumChannels(); ++channel)
                block.buffer->getWritePointer(channel)[frame] *= gain;
        }
    }
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
    for (auto& unit : units)
    {
        unit.synth.allNotesOff(0, false);
        unit.drums.stop();
        unit.chain.reset();
    }
    master.reset();
    outputPeak.store(0.0f);
    masterReductionDb.store(0.0f);
    visibleTick.store(0.0);
}
}
