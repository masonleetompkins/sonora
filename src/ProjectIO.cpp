#include "ProjectIO.h"
#include "KitSamples.h"

namespace sonora
{
namespace
{
bool integer(const juce::var& value) { return value.isInt() || value.isInt64(); }
bool number(const juce::var& value) { return value.isDouble() || integer(value); }

juce::Array<juce::var> encodeNotes(const Pattern& pattern)
{
    juce::Array<juce::var> notes;
    for (int i = 0; i < pattern.count; ++i)
    {
        const auto& n = pattern.notes[static_cast<std::size_t>(i)];
        auto note = std::make_unique<juce::DynamicObject>();
        note->setProperty("id", static_cast<juce::int64>(n.id));
        note->setProperty("start", n.start);
        note->setProperty("duration", n.duration);
        note->setProperty("pitch", n.pitch);
        note->setProperty("velocity", n.velocity);
        notes.add(juce::var(note.release()));
    }
    return notes;
}

juce::Result decodeNotes(const juce::var& value, Pattern& pattern)
{
    const auto* notes = value.getArray();
    if (notes == nullptr || notes->size() > Pattern::capacity)
        return juce::Result::fail("Invalid notes or pattern capacity exceeded (256 notes).");
    for (const auto& item : *notes)
    {
        const auto* object = item.getDynamicObject();
        if (object == nullptr)
            return juce::Result::fail("Invalid note object.");
        for (auto key : { "id", "start", "duration", "pitch", "velocity" })
        {
            const auto field = object->getProperty(key);
            if (!integer(field) || static_cast<juce::int64>(field) < 0
                || static_cast<juce::int64>(field) > 2147483647)
                return juce::Result::fail("Invalid note field.");
        }
        auto& n = pattern.notes[static_cast<std::size_t>(pattern.count++)];
        n.id = static_cast<std::uint32_t>(static_cast<juce::int64>(object->getProperty("id")));
        n.start = static_cast<int>(object->getProperty("start"));
        n.duration = static_cast<int>(object->getProperty("duration"));
        n.pitch = static_cast<int>(object->getProperty("pitch"));
        n.velocity = static_cast<int>(object->getProperty("velocity"));
    }
    return juce::Result::ok();
}

juce::Result decodeMix(const juce::DynamicObject& track, TrackMix& mix)
{
    const auto volume = track.getProperty("volume");
    const auto mute = track.getProperty("mute"), solo = track.getProperty("solo");
    if (!number(volume) || !mute.isBool() || !solo.isBool())
        return juce::Result::fail("Invalid track volume/mute/solo fields.");
    mix = { static_cast<float>(volume), static_cast<bool>(mute), static_cast<bool>(solo) };
    return mix.valid() ? juce::Result::ok() : juce::Result::fail("Track volume is out of range.");
}

juce::var encodeFx(const TrackFx& fx)
{
    auto* root = new juce::DynamicObject();
    auto* eq = new juce::DynamicObject();
    eq->setProperty("low", static_cast<double>(fx.eq.low));
    eq->setProperty("mid", static_cast<double>(fx.eq.mid));
    eq->setProperty("midFreq", static_cast<double>(fx.eq.midFreq));
    eq->setProperty("high", static_cast<double>(fx.eq.high));
    eq->setProperty("enabled", fx.eq.enabled);
    auto* comp = new juce::DynamicObject();
    comp->setProperty("thresholdDb", static_cast<double>(fx.comp.thresholdDb));
    comp->setProperty("ratio", static_cast<double>(fx.comp.ratio));
    comp->setProperty("attackMs", static_cast<double>(fx.comp.attackMs));
    comp->setProperty("releaseMs", static_cast<double>(fx.comp.releaseMs));
    comp->setProperty("enabled", fx.comp.enabled);
    auto* delay = new juce::DynamicObject();
    delay->setProperty("timeMs", static_cast<double>(fx.delay.timeMs));
    delay->setProperty("feedback", static_cast<double>(fx.delay.feedback));
    delay->setProperty("mix", static_cast<double>(fx.delay.mix));
    delay->setProperty("enabled", fx.delay.enabled);
    auto* reverb = new juce::DynamicObject();
    reverb->setProperty("size", static_cast<double>(fx.reverb.size));
    reverb->setProperty("damping", static_cast<double>(fx.reverb.damping));
    reverb->setProperty("mix", static_cast<double>(fx.reverb.mix));
    reverb->setProperty("enabled", fx.reverb.enabled);
    root->setProperty("eq", juce::var(eq));
    root->setProperty("comp", juce::var(comp));
    root->setProperty("delay", juce::var(delay));
    root->setProperty("reverb", juce::var(reverb));
    auto* drive = new juce::DynamicObject();
    drive->setProperty("amount", static_cast<double>(fx.drive.amount));
    drive->setProperty("tone", static_cast<double>(fx.drive.tone));
    drive->setProperty("enabled", fx.drive.enabled);
    auto* chorus = new juce::DynamicObject();
    chorus->setProperty("mix", static_cast<double>(fx.chorus.mix));
    chorus->setProperty("rate", static_cast<double>(fx.chorus.rate));
    chorus->setProperty("depth", static_cast<double>(fx.chorus.depth));
    chorus->setProperty("enabled", fx.chorus.enabled);
    root->setProperty("drive", juce::var(drive));
    root->setProperty("chorus", juce::var(chorus));
    return juce::var(root);
}

juce::Result decodeNumbered(const juce::DynamicObject& object, const char* key, float& out)
{
    const auto value = object.getProperty(key);
    if (!number(value))
        return juce::Result::fail("Invalid effect parameter.");
    out = static_cast<float>(value);
    return juce::Result::ok();
}

// requireExtended: v12+ files must carry drive and chorus; older files get
// transparent defaults.
juce::Result decodeFx(const juce::var& value, TrackFx& fx, bool requireExtended = false)
{
    const auto* root = value.getDynamicObject();
    if (root == nullptr)
        return juce::Result::fail("Invalid effect chain.");
    TrackFx candidate;
    const auto* eq = root->getProperty("eq").getDynamicObject();
    const auto* comp = root->getProperty("comp").getDynamicObject();
    const auto* delay = root->getProperty("delay").getDynamicObject();
    const auto* reverb = root->getProperty("reverb").getDynamicObject();
    if (eq == nullptr || comp == nullptr || delay == nullptr || reverb == nullptr)
        return juce::Result::fail("Incomplete effect chain.");
    juce::Result ok = juce::Result::ok();
    ok = decodeNumbered(*eq, "low", candidate.eq.low);
    if (ok.wasOk()) ok = decodeNumbered(*eq, "mid", candidate.eq.mid);
    if (ok.wasOk()) ok = decodeNumbered(*eq, "midFreq", candidate.eq.midFreq);
    if (ok.wasOk()) ok = decodeNumbered(*eq, "high", candidate.eq.high);
    if (ok.wasOk()) ok = decodeNumbered(*comp, "thresholdDb", candidate.comp.thresholdDb);
    if (ok.wasOk()) ok = decodeNumbered(*comp, "ratio", candidate.comp.ratio);
    if (ok.wasOk()) ok = decodeNumbered(*comp, "attackMs", candidate.comp.attackMs);
    if (ok.wasOk()) ok = decodeNumbered(*comp, "releaseMs", candidate.comp.releaseMs);
    if (ok.wasOk()) ok = decodeNumbered(*delay, "timeMs", candidate.delay.timeMs);
    if (ok.wasOk()) ok = decodeNumbered(*delay, "feedback", candidate.delay.feedback);
    if (ok.wasOk()) ok = decodeNumbered(*delay, "mix", candidate.delay.mix);
    if (ok.wasOk()) ok = decodeNumbered(*reverb, "size", candidate.reverb.size);
    if (ok.wasOk()) ok = decodeNumbered(*reverb, "damping", candidate.reverb.damping);
    if (ok.wasOk()) ok = decodeNumbered(*reverb, "mix", candidate.reverb.mix);
    if (ok.failed())
        return ok;
    if (requireExtended)
    {
        const auto* drive = root->getProperty("drive").getDynamicObject();
        const auto* chorus = root->getProperty("chorus").getDynamicObject();
        if (drive == nullptr || chorus == nullptr)
            return juce::Result::fail("Incomplete effect chain.");
        ok = decodeNumbered(*drive, "amount", candidate.drive.amount);
        if (ok.wasOk()) ok = decodeNumbered(*drive, "tone", candidate.drive.tone);
        if (ok.wasOk()) ok = decodeNumbered(*chorus, "mix", candidate.chorus.mix);
        if (ok.wasOk()) ok = decodeNumbered(*chorus, "rate", candidate.chorus.rate);
        if (ok.wasOk()) ok = decodeNumbered(*chorus, "depth", candidate.chorus.depth);
        if (ok.failed())
            return ok;
        for (auto [object, flag] : { std::pair<const juce::DynamicObject*, bool*> { drive, &candidate.drive.enabled },
                                     { chorus, &candidate.chorus.enabled } })
        {
            const auto enabled = object->getProperty("enabled");
            if (!enabled.isBool())
                return juce::Result::fail("Invalid effect bypass.");
            *flag = static_cast<bool>(enabled);
        }
    }
    for (auto [object, flag] : { std::pair<const juce::DynamicObject*, bool*> { eq, &candidate.eq.enabled },
                                 { comp, &candidate.comp.enabled },
                                 { delay, &candidate.delay.enabled },
                                 { reverb, &candidate.reverb.enabled } })
    {
        const auto enabled = object->getProperty("enabled");
        if (!enabled.isBool())
            return juce::Result::fail("Invalid effect bypass.");
        *flag = static_cast<bool>(enabled);
    }
    if (!candidate.valid())
        return juce::Result::fail("Effect parameters out of range.");
    fx = candidate;
    return juce::Result::ok();
}

juce::var encodeMaster(const LimiterParams& master)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("ceilingDb", static_cast<double>(master.ceilingDb));
    root->setProperty("releaseMs", static_cast<double>(master.releaseMs));
    root->setProperty("enabled", master.enabled);
    return juce::var(root);
}

juce::Result decodeMaster(const juce::var& value, LimiterParams& master)
{
    const auto* root = value.getDynamicObject();
    if (root == nullptr)
        return juce::Result::fail("Invalid master section.");
    LimiterParams candidate;
    auto ok = decodeNumbered(*root, "ceilingDb", candidate.ceilingDb);
    if (ok.wasOk()) ok = decodeNumbered(*root, "releaseMs", candidate.releaseMs);
    if (ok.failed())
        return ok;
    const auto enabled = root->getProperty("enabled");
    if (!enabled.isBool())
        return juce::Result::fail("Invalid master bypass.");
    candidate.enabled = static_cast<bool>(enabled);
    if (!candidate.valid())
        return juce::Result::fail("Master parameters out of range.");
    master = candidate;
    return juce::Result::ok();
}

juce::Result decodeDrumGrid(const juce::var& value, DrumPattern& grid)
{
    const auto* rows = value.getArray();
    if (rows == nullptr || rows->size() != drumPads)
        return juce::Result::fail("Invalid drum pad count.");
    DrumPattern candidate;
    for (int pad = 0; pad < drumPads; ++pad)
    {
        const auto* steps = (*rows)[pad].getArray();
        if (steps == nullptr || steps->size() != gridSteps)
            return juce::Result::fail("Invalid drum step count.");
        for (int step = 0; step < gridSteps; ++step)
        {
            const auto velocity = (*steps)[step];
            if (!integer(velocity) || static_cast<juce::int64>(velocity) < 0
                || static_cast<juce::int64>(velocity) > 127)
                return juce::Result::fail("Invalid drum velocity.");
            candidate.steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(step)]
                = static_cast<std::uint8_t>(static_cast<int>(velocity));
        }
    }
    grid = candidate;
    return juce::Result::ok();
}

juce::Result decodeSlotList(const juce::var& value, std::array<std::uint8_t, maxSections>& slots)
{
    const auto* list = value.getArray();
    if (list == nullptr || list->size() != legacyMaxSections)
        return juce::Result::fail("Invalid pattern slot list.");
    for (int i = 0; i < legacyMaxSections; ++i)
    {
        const auto slot = (*list)[i];
        if (!integer(slot) || static_cast<juce::int64>(slot) < 0
            || static_cast<juce::int64>(slot) >= numPatterns)
            return juce::Result::fail("Pattern slot out of range.");
        slots[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(static_cast<int>(slot));
    }
    return juce::Result::ok();
}

const std::pair<const char*, float SynthParams::*> synthFloatFields[] {
    { "mix2", &SynthParams::mix2 }, { "semis2", &SynthParams::semis2 }, { "detune2", &SynthParams::detune2 },
    { "cutoff", &SynthParams::cutoff }, { "resonance", &SynthParams::resonance },
    { "envAmount", &SynthParams::envAmount }, { "filterAttack", &SynthParams::filterAttack },
    { "filterDecay", &SynthParams::filterDecay }, { "filterSustain", &SynthParams::filterSustain },
    { "filterRelease", &SynthParams::filterRelease }, { "attack", &SynthParams::attack },
    { "decay", &SynthParams::decay }, { "sustain", &SynthParams::sustain }, { "release", &SynthParams::release },
    { "lfoRate", &SynthParams::lfoRate }, { "lfoPitch", &SynthParams::lfoPitch },
    { "lfoFilter", &SynthParams::lfoFilter }, { "drive", &SynthParams::drive },
    { "chorus", &SynthParams::chorus }, { "level", &SynthParams::level },
};

juce::var encodeSynth(const SynthParams& synth)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("wave", synth.wave);
    object->setProperty("wave2", synth.wave2);
    for (const auto& [name, field] : synthFloatFields)
        object->setProperty(name, static_cast<double>(synth.*field));
    return juce::var(object);
}

juce::Result decodeSynth(const juce::var& value, SynthParams& synth)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail("Missing synth settings.");
    SynthParams candidate;
    const auto wave = object->getProperty("wave"), wave2 = object->getProperty("wave2");
    if (!integer(wave) || !integer(wave2)
        || static_cast<juce::int64>(wave) < 0 || static_cast<juce::int64>(wave) >= numSynthWaves
        || static_cast<juce::int64>(wave2) < 0 || static_cast<juce::int64>(wave2) >= numSynthWaves)
        return juce::Result::fail("Unknown synth waveform.");
    candidate.wave = static_cast<int>(wave);
    candidate.wave2 = static_cast<int>(wave2);
    for (const auto& [name, field] : synthFloatFields)
    {
        const auto item = object->getProperty(name);
        if (!number(item))
            return juce::Result::fail("Invalid synth field: " + juce::String(name));
        candidate.*field = static_cast<float>(static_cast<double>(item));
    }
    if (!candidate.valid())
        return juce::Result::fail("Synth settings out of range.");
    synth = candidate;
    return juce::Result::ok();
}

std::unique_ptr<juce::DynamicObject> encodeTrackState(const Track& track)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("id", static_cast<juce::int64>(track.id));
    object->setProperty("name", track.trackName());
    object->setProperty("preset", track.instrumentPreset);
    object->setProperty("synth", encodeSynth(track.synth));
    object->setProperty("instrument", track.kind == TrackKind::Synth ? "sonora.sine-keys.v1"
                                : track.kind == TrackKind::Drums ? "sonora.starter-kit.v1"
                                                                 : "sonora.empty.v1");
    object->setProperty("icon", static_cast<int>(track.icon));
    object->setProperty("volume", static_cast<double>(track.mix.volume));
    object->setProperty("mute", track.mix.mute);
    object->setProperty("solo", track.mix.solo);
    juce::Array<juce::var> melodies;
    for (const auto& pattern : track.melodies)
        melodies.add(encodeNotes(pattern));
    object->setProperty("patterns", melodies);
    juce::Array<juce::var> grids;
    for (const auto& grid : track.drumPatterns)
    {
        juce::Array<juce::var> rows;
        for (const auto& row : grid.steps)
        {
            juce::Array<juce::var> steps;
            for (const auto velocity : row)
                steps.add(static_cast<int>(velocity));
            rows.add(steps);
        }
        grids.add(rows);
    }
    object->setProperty("grids", grids);
    object->setProperty("fx", encodeFx(track.fx));
    juce::Array<juce::var> samples;
    for (const auto& slot : track.padSamples)
        samples.add(padSampleName(slot));
    object->setProperty("samples", samples);
    object->setProperty("kitVariant", track.kitVariant);
    object->setProperty("swing", static_cast<double>(track.swing));
    object->setProperty("liveArp", static_cast<int>(track.liveFx.arp));
    object->setProperty("liveArpRate", static_cast<int>(track.liveFx.rate));
    object->setProperty("liveArpOctaves", track.liveFx.octaves);
    object->setProperty("liveArpLatch", track.liveFx.latch);
    object->setProperty("liveChordOn", track.liveFx.chordOn);
    object->setProperty("liveChord", static_cast<int>(track.liveFx.chord));
    return object;
}

juce::Result decodeTrackState(const juce::var& value, Track& track, bool requirePreset, bool requireSynth,
                              bool requireExtendedFx, bool requireGroove, bool requireLiveFx)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail("Invalid track object.");
    Track candidate;
    if (requireSynth)
    {
        auto synthResult = decodeSynth(object->getProperty("synth"), candidate.synth);
        if (synthResult.failed())
            return synthResult;
    }
    if (requirePreset)
    {
        const auto preset = object->getProperty("preset");
        if (!integer(preset) || static_cast<juce::int64>(preset) < 0
            || static_cast<juce::int64>(preset) >= static_cast<juce::int64>(instruments.size()))
            return juce::Result::fail("Unknown instrument preset.");
        candidate.instrumentPreset = static_cast<int>(preset);
    }
    const auto id = object->getProperty("id");
    const auto name = object->getProperty("name").toString();
    const auto instrument = object->getProperty("instrument").toString();
    const bool empty = instrument == "sonora.empty.v1";
    if (!integer(id) || static_cast<juce::int64>(id) < 0
        || static_cast<juce::int64>(id) > 2147483647
        || (!empty && (static_cast<juce::int64>(id) == 0 || name.isEmpty()))
        || name.length() > trackNameCapacity - 1)
        return juce::Result::fail("Invalid track identity.");
    candidate.id = static_cast<std::uint32_t>(static_cast<juce::int64>(id));
    candidate.setTrackName(name);
    if (instrument == "sonora.sine-keys.v1")
        candidate.kind = TrackKind::Synth;
    else if (instrument == "sonora.starter-kit.v1")
        candidate.kind = TrackKind::Drums;
    else if (empty)
        candidate.kind = TrackKind::None;
    else
        return juce::Result::fail("Unknown track instrument.");
    const auto icon = object->getProperty("icon");
    if (!integer(icon) || static_cast<juce::int64>(icon) < 0 || static_cast<juce::int64>(icon) > 255)
        return juce::Result::fail("Invalid track icon.");
    candidate.icon = static_cast<std::uint8_t>(static_cast<int>(icon));
    TrackMix mix;
    const auto volume = object->getProperty("volume");
    const auto mute = object->getProperty("mute"), solo = object->getProperty("solo");
    if (!number(volume) || !mute.isBool() || !solo.isBool())
        return juce::Result::fail("Invalid track volume/mute/solo fields.");
    mix = { static_cast<float>(volume), static_cast<bool>(mute), static_cast<bool>(solo) };
    if (!mix.valid())
        return juce::Result::fail("Track volume is out of range.");
    candidate.mix = mix;
    const auto* patterns = object->getProperty("patterns").getArray();
    if (patterns == nullptr || patterns->size() != numPatterns)
        return juce::Result::fail("Invalid track pattern library.");
    for (int slot = 0; slot < numPatterns; ++slot)
    {
        auto result = decodeNotes((*patterns)[slot], candidate.melodies[static_cast<std::size_t>(slot)]);
        if (result.failed())
            return result;
    }
    const auto* grids = object->getProperty("grids").getArray();
    if (grids == nullptr || grids->size() != numPatterns)
        return juce::Result::fail("Invalid track drum library.");
    for (int slot = 0; slot < numPatterns; ++slot)
    {
        auto result = decodeDrumGrid((*grids)[slot], candidate.drumPatterns[static_cast<std::size_t>(slot)]);
        if (result.failed())
            return result;
    }
    auto result = decodeFx(object->getProperty("fx"), candidate.fx, requireExtendedFx);
    if (result.failed())
        return result;
    const auto* samples = object->getProperty("samples").getArray();
    if (samples == nullptr || samples->size() != drumPads)
        return juce::Result::fail("Invalid kit sample list.");
    for (int pad = 0; pad < drumPads; ++pad)
    {
        const auto sample = (*samples)[pad].toString();
        if (sample.length() > sampleFileCapacity - 1)
            return juce::Result::fail("Kit sample name too long.");
        setPadSampleName(candidate.padSamples[static_cast<std::size_t>(pad)], sample);
    }
    if (requireGroove)
    {
        const auto swing = object->getProperty("swing");
        if (!number(swing) || static_cast<float>(swing) < 0.0f || static_cast<float>(swing) > maxSwing)
            return juce::Result::fail("Track swing out of range.");
        candidate.swing = static_cast<float>(swing);
    }
    if (requireLiveFx)
    {
        const auto arp = object->getProperty("liveArp"), rate = object->getProperty("liveArpRate"),
                   octaves = object->getProperty("liveArpOctaves"), latch = object->getProperty("liveArpLatch"),
                   chordOn = object->getProperty("liveChordOn"), chord = object->getProperty("liveChord");
        if (!integer(arp) || !integer(rate) || !integer(octaves) || !integer(chord))
            return juce::Result::fail("Track live FX malformed.");
        LiveFx fx;
        fx.arp = static_cast<ArpMode>(static_cast<int>(arp));
        fx.rate = static_cast<ArpRate>(static_cast<int>(rate));
        fx.octaves = static_cast<int>(octaves);
        fx.latch = latch.isBool() ? static_cast<bool>(latch) : static_cast<int>(latch) == 1;
        fx.chordOn = chordOn.isBool() ? static_cast<bool>(chordOn) : static_cast<int>(chordOn) == 1;
        fx.chord = static_cast<ChordType>(static_cast<int>(chord));
        if (!fx.valid())
            return juce::Result::fail("Track live FX out of range.");
        candidate.liveFx = fx;
    }
    const auto variant = object->getProperty("kitVariant");
    if (!integer(variant) || static_cast<juce::int64>(variant) < 0
        || static_cast<juce::int64>(variant) >= numKitVariants)
        return juce::Result::fail("Unknown kit variant.");
    candidate.kitVariant = static_cast<int>(variant);
    if (!candidate.valid())
        return juce::Result::fail("Track content out of range.");
    track = candidate;
    return juce::Result::ok();
}
}

juce::String ProjectIO::encode(const ProjectState& state)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("format", "sonora-project");
    root->setProperty("version", 16);
    root->setProperty("bpm", state.bpm);
    root->setProperty("musicKey", state.musicKey);
    root->setProperty("musicScale", static_cast<int>(state.musicScale));
    root->setProperty("songMode", state.songMode);
    juce::DynamicObject* song = new juce::DynamicObject();
    song->setProperty("sections", state.song.sections);
    juce::Array<juce::var> slots, cells;
    for (int s = 0; s < maxSections; ++s)
    {
        juce::Array<juce::var> slotRow, cellRow;
        for (int track = 0; track < maxTracks; ++track)
        {
            slotRow.add(static_cast<int>(state.song.slots[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)]));
            cellRow.add(state.song.trackOn[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)]);
        }
        slots.add(slotRow);
        cells.add(cellRow);
    }
    song->setProperty("slots", slots);
    song->setProperty("cells", cells);
    juce::Array<juce::var> parts;
    for (const auto part : state.song.parts)
        parts.add(static_cast<int>(part));
    song->setProperty("parts", parts);
    root->setProperty("song", juce::var(song));
    root->setProperty("ticksPerQuarter", ticksPerQuarter);
    root->setProperty("lengthTicks", patternTicks);
    juce::Array<juce::var> tracks;
    for (const auto& track : state.tracks)
        tracks.add(juce::var(encodeTrackState(track).release()));
    root->setProperty("tracks", tracks);
    root->setProperty("master", encodeMaster(state.master));
    juce::Array<juce::var> takes;
    for (int i = 0; i < state.takeCount; ++i)
    {
        const auto& take = state.takes[static_cast<std::size_t>(i)];
        auto* object = new juce::DynamicObject();
        object->setProperty("id", static_cast<juce::int64>(take.id));
        object->setProperty("file", take.fileName());
        object->setProperty("startTick", take.startTick);
        object->setProperty("frames", take.frames);
        object->setProperty("gain", static_cast<double>(take.gain));
        object->setProperty("mute", take.mute);
        object->setProperty("channels", take.channels);
        takes.add(juce::var(object));
    }
    root->setProperty("takes", takes);
    return juce::JSON::toString(juce::var(root.release()));
}

// Versions 2-8 predate per-track projects: the two legacy tracks migrate
// into slots 0 (melody) and 1 (drums); remaining slots stay empty.
juce::Result decodeLegacyV2ToV8(const juce::DynamicObject* root, juce::int64 versionNumber,
                                ProjectState& candidate)
{
    juce::Result result = juce::Result::ok();
    // Legacy documents predate named tracks: slots 0 and 1 always mean the
    // melody and drum instruments.
    candidate.tracks[0].id = melodyTrackId;
    candidate.tracks[0].setTrackName("Sine Keys");
    candidate.tracks[0].kind = TrackKind::Synth;
    candidate.tracks[0].icon = 0;
    candidate.tracks[1].id = drumTrackId;
    candidate.tracks[1].setTrackName("Starter Drums");
    candidate.tracks[1].kind = TrackKind::Drums;
    candidate.tracks[1].icon = 1;
    const auto songMode = root->getProperty("songMode");
    if (versionNumber >= 3)
    {
        if (!songMode.isBool())
            return juce::Result::fail("Invalid transport mode.");
        candidate.songMode = static_cast<bool>(songMode);
        const auto* songObject = root->getProperty("song").getDynamicObject();
        const auto* melodyFlags = songObject != nullptr ? songObject->getProperty("melody").getArray() : nullptr;
        const auto* drumFlags = songObject != nullptr ? songObject->getProperty("drums").getArray() : nullptr;
        const auto sections = songObject != nullptr ? songObject->getProperty("sections") : juce::var();
        if (melodyFlags == nullptr || drumFlags == nullptr || !integer(sections)
            || melodyFlags->size() != legacyMaxSections || drumFlags->size() != legacyMaxSections)
            return juce::Result::fail("Invalid song arrangement.");
        candidate.song.sections = static_cast<int>(sections);
        if (candidate.song.sections < 1 || candidate.song.sections > legacyMaxSections)
            return juce::Result::fail("Invalid section count.");
        for (int i = 0; i < legacyMaxSections; ++i)
        {
            if (!(*melodyFlags)[i].isBool() || !(*drumFlags)[i].isBool())
                return juce::Result::fail("Invalid section switch.");
            candidate.song.trackOn[static_cast<std::size_t>(i)][0] = static_cast<bool>((*melodyFlags)[i]);
            candidate.song.trackOn[static_cast<std::size_t>(i)][1] = static_cast<bool>((*drumFlags)[i]);
        }
        // Versions before v6 play slot A everywhere.
        if (versionNumber >= 6)
        {
            std::array<std::uint8_t, maxSections> melodySlots {}, drumSlots {};
            result = decodeSlotList(songObject->getProperty("melodyPatterns"), melodySlots);
            if (result.wasOk())
                result = decodeSlotList(songObject->getProperty("drumPatterns"), drumSlots);
            if (result.failed())
                return result;
            for (int i = 0; i < legacyMaxSections; ++i)
            {
                candidate.song.slots[static_cast<std::size_t>(i)][0] = melodySlots[static_cast<std::size_t>(i)];
                candidate.song.slots[static_cast<std::size_t>(i)][1] = drumSlots[static_cast<std::size_t>(i)];
            }
        }
    }
    const auto* tracks = root->getProperty("tracks").getArray();
    if (tracks == nullptr || tracks->size() != 2)
        return juce::Result::fail("This version requires a melody track and a drum track.");
    bool melodySeen = false, drumsSeen = false;
    for (const auto& item : *tracks)
    {
        const auto* track = item.getDynamicObject();
        if (track == nullptr || !integer(track->getProperty("id")))
            return juce::Result::fail("Invalid track identity.");
        const auto id = static_cast<juce::int64>(track->getProperty("id"));
        if (id == melodyTrackId && !melodySeen)
        {
            melodySeen = true;
            if (track->getProperty("instrument").toString() != "sonora.sine-keys.v1")
                return juce::Result::fail("Unsupported melody instrument.");
            result = decodeMix(*track, candidate.tracks[0].mix);
            if (result.failed())
                return result;
            if (versionNumber >= 6)
            {
                const auto* patterns = track->getProperty("patterns").getArray();
                if (patterns == nullptr || patterns->size() != numPatterns)
                    return juce::Result::fail("Invalid melody pattern library.");
                for (int slot = 0; slot < numPatterns; ++slot)
                {
                    result = decodeNotes((*patterns)[slot],
                                         candidate.tracks[0].melodies[static_cast<std::size_t>(slot)]);
                    if (result.failed())
                        return result;
                }
            }
            else
                result = decodeNotes(track->getProperty("notes"), candidate.tracks[0].melodies[0]);
            if (result.wasOk() && versionNumber >= 4)
                result = decodeFx(track->getProperty("fx"), candidate.tracks[0].fx);
        }
        else if (id == drumTrackId && !drumsSeen)
        {
            drumsSeen = true;
            if (track->getProperty("instrument").toString() != "sonora.starter-kit.v1")
                return juce::Result::fail("Unsupported drum kit.");
            result = decodeMix(*track, candidate.tracks[1].mix);
            if (result.failed())
                return result;
            if (versionNumber >= 6)
            {
                const auto* grids = track->getProperty("grids").getArray();
                if (grids == nullptr || grids->size() != numPatterns)
                    return juce::Result::fail("Invalid drum pattern library.");
                for (int slot = 0; slot < numPatterns; ++slot)
                {
                    result = decodeDrumGrid((*grids)[slot],
                                            candidate.tracks[1].drumPatterns[static_cast<std::size_t>(slot)]);
                    if (result.failed())
                        return result;
                }
            }
            else
            {
                result = decodeDrumGrid(track->getProperty("steps"), candidate.tracks[1].drumPatterns[0]);
                if (result.failed())
                    return result;
            }
            if (versionNumber >= 4)
            {
                result = decodeFx(track->getProperty("fx"), candidate.tracks[1].fx);
                if (result.failed())
                    return result;
            }
            // Versions before v7 use the built-in starter kit throughout.
            if (versionNumber >= 7)
            {
                const auto* samples = track->getProperty("samples").getArray();
                if (samples == nullptr || samples->size() != drumPads)
                    return juce::Result::fail("Invalid kit sample list.");
                for (int pad = 0; pad < drumPads; ++pad)
                {
                    const auto name = (*samples)[pad].toString();
                    if (name.length() > sampleFileCapacity - 1)
                        return juce::Result::fail("Kit sample name too long.");
                    setPadSampleName(candidate.tracks[1].padSamples[static_cast<std::size_t>(pad)], name);
                }
            }
            // Versions before v8 voice built-ins with the Starter variant.
            if (versionNumber >= 8)
            {
                const auto variant = track->getProperty("kitVariant");
                if (!integer(variant) || static_cast<juce::int64>(variant) < 0
                    || static_cast<juce::int64>(variant) >= numKitVariants)
                    return juce::Result::fail("Unknown kit variant.");
                candidate.tracks[1].kitVariant = static_cast<int>(variant);
            }
        }
        else
            return juce::Result::fail("Unknown or duplicate track ID.");
        if (result.failed())
            return result;
    }
    // Versions before v4 have no effects; defaults are transparent by design.
    if (versionNumber >= 4)
    {
        result = decodeMaster(root->getProperty("master"), candidate.master);
        if (result.failed())
            return result;
    }
    // Versions before v5 have no audio takes.
    if (versionNumber >= 5)
    {
        const auto* takes = root->getProperty("takes").getArray();
        if (takes == nullptr || takes->size() > maxTakes)
            return juce::Result::fail("Invalid take list.");
        for (const auto& item : *takes)
        {
            const auto* object = item.getDynamicObject();
            if (object == nullptr)
                return juce::Result::fail("Invalid take object.");
            AudioTakeMeta take;
            const auto id = object->getProperty("id");
            const auto file = object->getProperty("file").toString();
            const auto startTick = object->getProperty("startTick");
            const auto frames = object->getProperty("frames");
            const auto gain = object->getProperty("gain");
            const auto mute = object->getProperty("mute");
            const auto channels = object->getProperty("channels");
            if (!integer(id) || file.isEmpty() || file.length() > takeFileCapacity - 1
                || !integer(startTick) || !integer(frames) || !number(gain)
                || !mute.isBool() || !integer(channels))
                return juce::Result::fail("Invalid take field.");
            take.id = static_cast<std::uint32_t>(static_cast<juce::int64>(id));
            take.setFileName(file);
            take.startTick = static_cast<int>(startTick);
            take.frames = static_cast<int>(frames);
            take.gain = static_cast<float>(gain);
            take.mute = static_cast<bool>(mute);
            take.channels = static_cast<int>(channels);
            if (!take.valid())
                return juce::Result::fail("Take parameters out of range.");
            candidate.takes[static_cast<std::size_t>(candidate.takeCount++)] = take;
        }
    }
    return juce::Result::ok();
}

juce::Result ProjectIO::decode(const juce::String& json, ProjectState& destination)
{
    juce::var parsed;
    auto result = juce::JSON::parse(json, parsed);
    if (result.failed())
        return result;
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr || root->getProperty("format").toString() != "sonora-project")
        return juce::Result::fail("This is not a Sonora project.");
    const auto version = root->getProperty("version");
    if (!integer(version))
        return juce::Result::fail("Unsupported project version.");
    const auto versionNumber = static_cast<juce::int64>(version);
    if (versionNumber < 1 || versionNumber > 16)
        return juce::Result::fail("Unsupported project version.");
    if (!integer(root->getProperty("ticksPerQuarter")) || !integer(root->getProperty("lengthTicks"))
        || static_cast<juce::int64>(root->getProperty("ticksPerQuarter")) != ticksPerQuarter
        || static_cast<juce::int64>(root->getProperty("lengthTicks")) != patternTicks)
        return juce::Result::fail("Unsupported project timing.");
    const auto tempo = root->getProperty("bpm");
    if (!number(tempo))
        return juce::Result::fail("Invalid tempo.");
    ProjectState candidate;
    candidate.bpm = static_cast<double>(tempo);
    if (versionNumber >= 15)
    {
        const auto key = root->getProperty("musicKey"), scale = root->getProperty("musicScale");
        if (!integer(key) || !integer(scale) || static_cast<juce::int64>(key) < 0
            || static_cast<juce::int64>(key) > 11
            || static_cast<juce::int64>(scale) < 0
            || static_cast<juce::int64>(scale) >= static_cast<juce::int64>(MusicScale::numScales))
            return juce::Result::fail("Invalid song key.");
        candidate.musicKey = static_cast<int>(key);
        candidate.musicScale = static_cast<MusicScale>(static_cast<int>(scale));
    }
    if (versionNumber == 1)
    {
        // Version 1 is the original melody-only document. Start from the
        // default two-track project with unity melody gain.
        candidate = defaultProject();
        candidate.bpm = static_cast<double>(tempo);
        candidate.tracks[0].mix.volume = 1.0f;
        result = decodeNotes(root->getProperty("notes"), candidate.tracks[0].melodies[0]);
        if (result.failed())
            return result;
    }
    else if (versionNumber >= 9)
    {
        const auto songMode = root->getProperty("songMode");
        if (!songMode.isBool())
            return juce::Result::fail("Invalid transport mode.");
        candidate.songMode = static_cast<bool>(songMode);
        const auto* songObject = root->getProperty("song").getDynamicObject();
        const auto* slots = songObject != nullptr ? songObject->getProperty("slots").getArray() : nullptr;
        const auto* cells = songObject != nullptr ? songObject->getProperty("cells").getArray() : nullptr;
        const auto sections = songObject != nullptr ? songObject->getProperty("sections") : juce::var();
        const int rows = versionNumber >= 13 ? maxSections : legacyMaxSections;
        if (slots == nullptr || cells == nullptr || !integer(sections)
            || slots->size() != rows || cells->size() != rows)
            return juce::Result::fail("Invalid song arrangement.");
        candidate.song.sections = static_cast<int>(sections);
        if (candidate.song.sections < 1 || candidate.song.sections > rows)
            return juce::Result::fail("Invalid section count.");
        if (versionNumber >= 13)
        {
            const auto* parts = songObject->getProperty("parts").getArray();
            if (parts == nullptr || parts->size() != maxSections)
                return juce::Result::fail("Invalid song part list.");
            for (int s = 0; s < maxSections; ++s)
            {
                const auto part = (*parts)[s];
                if (!integer(part) || static_cast<juce::int64>(part) < 0
                    || static_cast<juce::int64>(part) >= static_cast<juce::int64>(SongPart::numParts))
                    return juce::Result::fail("Unknown song part.");
                candidate.song.parts[static_cast<std::size_t>(s)] = static_cast<SongPart>(static_cast<int>(part));
            }
        }
        for (int s = 0; s < rows; ++s)
        {
            const auto* slotRow = (*slots)[s].getArray();
            const auto* cellRow = (*cells)[s].getArray();
            if (slotRow == nullptr || cellRow == nullptr
                || slotRow->size() != maxTracks || cellRow->size() != maxTracks)
                return juce::Result::fail("Invalid arrangement row.");
            for (int track = 0; track < maxTracks; ++track)
            {
                const auto slot = (*slotRow)[track];
                const auto cell = (*cellRow)[track];
                if (!integer(slot) || static_cast<juce::int64>(slot) < 0
                    || static_cast<juce::int64>(slot) >= numPatterns || !cell.isBool())
                    return juce::Result::fail("Invalid arrangement cell.");
                candidate.song.slots[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)]
                    = static_cast<std::uint8_t>(static_cast<int>(slot));
                candidate.song.trackOn[static_cast<std::size_t>(s)][static_cast<std::size_t>(track)]
                    = static_cast<bool>(cell);
            }
        }
        const auto* tracks = root->getProperty("tracks").getArray();
        if (tracks == nullptr || tracks->size() != maxTracks)
            return juce::Result::fail("This version requires a full track list.");
        for (int track = 0; track < maxTracks; ++track)
        {
            result = decodeTrackState((*tracks)[track], candidate.tracks[static_cast<std::size_t>(track)],
                                      versionNumber >= 10, versionNumber >= 11, versionNumber >= 12, versionNumber >= 14,
                                      versionNumber >= 16);
            if (result.failed())
                return result;
        }
        for (int i = 0; i < maxTracks; ++i)
            for (int j = 0; j < i; ++j)
                if (candidate.tracks[static_cast<std::size_t>(i)].id != 0
                    && candidate.tracks[static_cast<std::size_t>(i)].id
                           == candidate.tracks[static_cast<std::size_t>(j)].id)
                    return juce::Result::fail("Duplicate track ID.");
        result = decodeMaster(root->getProperty("master"), candidate.master);
        if (result.failed())
            return result;
        const auto* takes = root->getProperty("takes").getArray();
        if (takes == nullptr || takes->size() > maxTakes)
            return juce::Result::fail("Invalid take list.");
        for (const auto& item : *takes)
        {
            const auto* object = item.getDynamicObject();
            if (object == nullptr)
                return juce::Result::fail("Invalid take object.");
            AudioTakeMeta take;
            const auto id = object->getProperty("id");
            const auto file = object->getProperty("file").toString();
            const auto startTick = object->getProperty("startTick");
            const auto frames = object->getProperty("frames");
            const auto gain = object->getProperty("gain");
            const auto mute = object->getProperty("mute");
            const auto channels = object->getProperty("channels");
            if (!integer(id) || file.isEmpty() || file.length() > takeFileCapacity - 1
                || !integer(startTick) || !integer(frames) || !number(gain)
                || !mute.isBool() || !integer(channels))
                return juce::Result::fail("Invalid take field.");
            take.id = static_cast<std::uint32_t>(static_cast<juce::int64>(id));
            take.setFileName(file);
            take.startTick = static_cast<int>(startTick);
            take.frames = static_cast<int>(frames);
            take.gain = static_cast<float>(gain);
            take.mute = static_cast<bool>(mute);
            take.channels = static_cast<int>(channels);
            if (!take.valid())
                return juce::Result::fail("Take parameters out of range.");
            candidate.takes[static_cast<std::size_t>(candidate.takeCount++)] = take;
        }
    }
    else
    {
        result = decodeLegacyV2ToV8(root, versionNumber, candidate);
        if (result.failed())
            return result;
    }
    if (!candidate.valid())
        return juce::Result::fail("Invalid tempo or notes: check ranges, IDs, and overlapping pitches.");
    destination = candidate;
    return juce::Result::ok();
}

juce::Result ProjectIO::save(const juce::File& file, const ProjectState& state)
{
    if (!state.valid())
        return juce::Result::fail("Cannot save an invalid project.");
    juce::TemporaryFile temporary(file);
    {
        juce::FileOutputStream stream(temporary.getFile());
        if (!stream.openedOk())
            return stream.getStatus();
        const auto text = encode(state);
        if (!stream.writeText(text, false, false, "\n"))
            return juce::Result::fail("Could not write the project.");
        stream.flush();
        if (stream.getStatus().failed())
            return stream.getStatus();
    }
    return temporary.overwriteTargetFileWithTemporary()
        ? juce::Result::ok() : juce::Result::fail("Could not replace the project file.");
}

juce::Result ProjectIO::load(const juce::File& file, ProjectState& destination)
{
    if (!file.existsAsFile() || file.getSize() > 1024 * 1024)
        return juce::Result::fail("Project is missing or exceeds the 1 MB size limit.");
    return decode(file.loadFileAsString(), destination);
}
}
