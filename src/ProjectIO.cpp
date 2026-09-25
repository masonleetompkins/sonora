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

std::unique_ptr<juce::DynamicObject> encodeTrack(std::uint32_t id, const char* instrument, const TrackMix& mix)
{
    auto track = std::make_unique<juce::DynamicObject>();
    track->setProperty("id", static_cast<juce::int64>(id));
    track->setProperty("instrument", instrument);
    track->setProperty("volume", static_cast<double>(mix.volume));
    track->setProperty("mute", mix.mute);
    track->setProperty("solo", mix.solo);
    return track;
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

juce::Result decodeFx(const juce::var& value, TrackFx& fx)
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
    if (list == nullptr || list->size() != maxSections)
        return juce::Result::fail("Invalid pattern slot list.");
    for (int i = 0; i < maxSections; ++i)
    {
        const auto slot = (*list)[i];
        if (!integer(slot) || static_cast<juce::int64>(slot) < 0
            || static_cast<juce::int64>(slot) >= numPatterns)
            return juce::Result::fail("Pattern slot out of range.");
        slots[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(static_cast<int>(slot));
    }
    return juce::Result::ok();
}
}

juce::String ProjectIO::encode(const ProjectState& state)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("format", "sonora-project");
    root->setProperty("version", 8);
    root->setProperty("bpm", state.bpm);
    root->setProperty("songMode", state.songMode);
    juce::DynamicObject* song = new juce::DynamicObject();
    song->setProperty("sections", state.song.sections);
    juce::Array<juce::var> melodySections, drumSections, melodySlots, drumSlots;
    for (int i = 0; i < maxSections; ++i)
    {
        melodySections.add(state.song.melodyOn[static_cast<std::size_t>(i)]);
        drumSections.add(state.song.drumsOn[static_cast<std::size_t>(i)]);
        melodySlots.add(static_cast<int>(state.song.melodyPattern[static_cast<std::size_t>(i)]));
        drumSlots.add(static_cast<int>(state.song.drumPattern[static_cast<std::size_t>(i)]));
    }
    song->setProperty("melody", melodySections);
    song->setProperty("drums", drumSections);
    song->setProperty("melodyPatterns", melodySlots);
    song->setProperty("drumPatterns", drumSlots);
    root->setProperty("song", juce::var(song));
    root->setProperty("ticksPerQuarter", ticksPerQuarter);
    root->setProperty("lengthTicks", patternTicks);
    juce::Array<juce::var> tracks;
    auto melody = encodeTrack(melodyTrackId, "sonora.sine-keys.v1", state.melodyMix);
    juce::Array<juce::var> melodyPatterns;
    for (const auto& pattern : state.melodies)
        melodyPatterns.add(encodeNotes(pattern));
    melody->setProperty("patterns", melodyPatterns);
    melody->setProperty("fx", encodeFx(state.melodyFx));
    tracks.add(juce::var(melody.release()));
    auto drums = encodeTrack(drumTrackId, "sonora.starter-kit.v1", state.drumMix);
    juce::Array<juce::var> drumGrids;
    for (const auto& grid : state.drumPatterns)
    {
        juce::Array<juce::var> rows;
        for (const auto& row : grid.steps)
        {
            juce::Array<juce::var> steps;
            for (const auto velocity : row)
                steps.add(static_cast<int>(velocity));
            rows.add(steps);
        }
        drumGrids.add(rows);
    }
    drums->setProperty("grids", drumGrids);
    drums->setProperty("fx", encodeFx(state.drumFx));
    juce::Array<juce::var> samples;
    for (const auto& slot : state.padSamples)
        samples.add(padSampleName(slot));
    drums->setProperty("samples", samples);
    drums->setProperty("kitVariant", state.kitVariant);
    tracks.add(juce::var(drums.release()));
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
    if (versionNumber < 1 || versionNumber > 8)
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
    if (versionNumber == 1)
    {
        // Version 1 is the original melody-only document. Add an empty drum
        // track and unity melody gain before the new shared master headroom.
        candidate.melodyMix.volume = 1.0f;
        result = decodeNotes(root->getProperty("notes"), candidate.melodies[0]);
        if (result.failed())
            return result;
    }
    else
    {
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
                || melodyFlags->size() != maxSections || drumFlags->size() != maxSections)
                return juce::Result::fail("Invalid song arrangement.");
            candidate.song.sections = static_cast<int>(sections);
            for (int i = 0; i < maxSections; ++i)
            {
                if (!(*melodyFlags)[i].isBool() || !(*drumFlags)[i].isBool())
                    return juce::Result::fail("Invalid section switch.");
                candidate.song.melodyOn[static_cast<std::size_t>(i)] = static_cast<bool>((*melodyFlags)[i]);
                candidate.song.drumsOn[static_cast<std::size_t>(i)] = static_cast<bool>((*drumFlags)[i]);
            }
            // Versions before v6 play slot A everywhere.
            if (versionNumber >= 6)
            {
                result = decodeSlotList(songObject->getProperty("melodyPatterns"), candidate.song.melodyPattern);
                if (result.wasOk())
                    result = decodeSlotList(songObject->getProperty("drumPatterns"), candidate.song.drumPattern);
                if (result.failed())
                    return result;
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
                result = decodeMix(*track, candidate.melodyMix);
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
                                             candidate.melodies[static_cast<std::size_t>(slot)]);
                        if (result.failed())
                            return result;
                    }
                }
                else
                    result = decodeNotes(track->getProperty("notes"), candidate.melodies[0]);
                if (result.wasOk() && versionNumber >= 4)
                    result = decodeFx(track->getProperty("fx"), candidate.melodyFx);
            }
            else if (id == drumTrackId && !drumsSeen)
            {
                drumsSeen = true;
                if (track->getProperty("instrument").toString() != "sonora.starter-kit.v1")
                    return juce::Result::fail("Unsupported drum kit.");
                result = decodeMix(*track, candidate.drumMix);
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
                                                candidate.drumPatterns[static_cast<std::size_t>(slot)]);
                        if (result.failed())
                            return result;
                    }
                }
                else
                {
                    result = decodeDrumGrid(track->getProperty("steps"), candidate.drumPatterns[0]);
                    if (result.failed())
                        return result;
                }
                if (versionNumber >= 4)
                {
                    result = decodeFx(track->getProperty("fx"), candidate.drumFx);
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
                        setPadSampleName(candidate.padSamples[static_cast<std::size_t>(pad)], name);
                    }
                }
                // Versions before v8 voice built-ins with the Starter variant.
                if (versionNumber >= 8)
                {
                    const auto variant = track->getProperty("kitVariant");
                    if (!integer(variant) || static_cast<juce::int64>(variant) < 0
                        || static_cast<juce::int64>(variant) >= numKitVariants)
                        return juce::Result::fail("Unknown kit variant.");
                    candidate.kitVariant = static_cast<int>(variant);
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
