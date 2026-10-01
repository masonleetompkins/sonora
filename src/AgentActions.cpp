#include "AgentActions.h"
#include "DrumSampler.h"
#include <cmath>
#include <map>

namespace sonora::agent
{
namespace
{
std::size_t idx(int i) { return static_cast<std::size_t>(i); }

juce::String lower(const juce::String& text) { return text.trim().toLowerCase(); }

// Strips control characters and bounds the length of model-supplied text.
juce::String cleanName(const juce::String& text, int maxChars = 40)
{
    juce::String out;
    for (auto c : text)
        if (c >= 0x20)
            out += juce::String::charToString(c);
    return out.substring(0, maxChars).trim();
}

juce::String compact(const juce::String& text) { return lower(text).removeCharacters(" _-"); }

juce::String number(double value)
{
    auto text = juce::String(value, 3);
    if (text.contains("."))
        text = text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return text;
}

// ---- Parameter registry --------------------------------------------------------
using FloatRef = std::function<float&(ProjectState&, int)>;
using IntRef = std::function<int&(ProjectState&, int)>;
using BoolRef = std::function<bool&(ProjectState&, int)>;

void addFloat(std::vector<ParamSpec>& out, const char* name, bool perTrack, float lo, float hi, const char* note,
              FloatRef ref)
{
    ParamSpec spec;
    spec.name = name;
    spec.perTrack = perTrack;
    spec.min = lo;
    spec.max = hi;
    spec.note = note;
    spec.get = [ref](const ProjectState& p, int t) { return ref(const_cast<ProjectState&>(p), t); };
    spec.set = [ref, lo, hi](ProjectState& p, int t, float v) { ref(p, t) = std::clamp(v, lo, hi); };
    out.push_back(std::move(spec));
}

void addInt(std::vector<ParamSpec>& out, const char* name, bool perTrack, int lo, int hi, const char* note,
            IntRef ref, std::vector<juce::String> choices = {})
{
    ParamSpec spec;
    spec.name = name;
    spec.perTrack = perTrack;
    spec.min = static_cast<float>(lo);
    spec.max = static_cast<float>(hi);
    spec.isInt = true;
    spec.note = note;
    spec.choices = std::move(choices);
    spec.get = [ref](const ProjectState& p, int t) { return static_cast<float>(ref(const_cast<ProjectState&>(p), t)); };
    spec.set = [ref, lo, hi](ProjectState& p, int t, float v) {
        ref(p, t) = std::clamp(static_cast<int>(std::lround(v)), lo, hi);
    };
    out.push_back(std::move(spec));
}

void addBool(std::vector<ParamSpec>& out, const char* name, bool perTrack, const char* note, BoolRef ref)
{
    ParamSpec spec;
    spec.name = name;
    spec.perTrack = perTrack;
    spec.min = 0.0f;
    spec.max = 1.0f;
    spec.isBool = true;
    spec.note = note;
    spec.get = [ref](const ProjectState& p, int t) { return ref(const_cast<ProjectState&>(p), t) ? 1.0f : 0.0f; };
    spec.set = [ref](ProjectState& p, int t, float v) { ref(p, t) = v >= 0.5f; };
    out.push_back(std::move(spec));
}

#define TRACK_FLOAT(NAME, LO, HI, NOTE, MEMBER) \
    addFloat(list, NAME, true, LO, HI, NOTE, [](ProjectState& p, int t) -> float& { return p.tracks[idx(t)].MEMBER; })
#define TRACK_INT(NAME, LO, HI, NOTE, MEMBER) \
    addInt(list, NAME, true, LO, HI, NOTE, [](ProjectState& p, int t) -> int& { return p.tracks[idx(t)].MEMBER; })
#define TRACK_ENUM(NAME, LO, HI, NOTE, MEMBER, CHOICES) \
    addInt(list, NAME, true, LO, HI, NOTE, [](ProjectState& p, int t) -> int& { return p.tracks[idx(t)].MEMBER; }, CHOICES)
#define TRACK_BOOL(NAME, NOTE, MEMBER) \
    addBool(list, NAME, true, NOTE, [](ProjectState& p, int t) -> bool& { return p.tracks[idx(t)].MEMBER; })
#define PROJECT_FLOAT(NAME, LO, HI, NOTE, MEMBER) \
    addFloat(list, NAME, false, LO, HI, NOTE, [](ProjectState& p, int) -> float& { return p.MEMBER; })
#define PROJECT_BOOL(NAME, NOTE, MEMBER) \
    addBool(list, NAME, false, NOTE, [](ProjectState& p, int) -> bool& { return p.MEMBER; })

std::vector<ParamSpec> buildParameters()
{
    std::vector<ParamSpec> list;
    // Mixer
    TRACK_FLOAT("mix.volume", 0.0f, 1.5f, "track fader", mix.volume);
    TRACK_FLOAT("mix.pan", -1.0f, 1.0f, "-1 left, 1 right", mix.pan);
    TRACK_FLOAT("mix.sendDelay", 0.0f, 1.0f, "send to the delay bus", mix.sendDelay);
    TRACK_FLOAT("mix.sendReverb", 0.0f, 1.0f, "send to the reverb bus", mix.sendReverb);
    TRACK_BOOL("mix.mute", "mute the track", mix.mute);
    TRACK_BOOL("mix.solo", "solo the track", mix.solo);
    TRACK_FLOAT("track.swing", 0.0f, maxSwing, "delays offbeat 16ths; 0.15-0.3 is a groove", swing);
    // Track insert effects
    TRACK_FLOAT("fx.eq.low", -15.0f, 15.0f, "dB", fx.eq.low);
    TRACK_FLOAT("fx.eq.mid", -15.0f, 15.0f, "dB", fx.eq.mid);
    TRACK_FLOAT("fx.eq.midFreq", 200.0f, 8000.0f, "Hz", fx.eq.midFreq);
    TRACK_FLOAT("fx.eq.high", -15.0f, 15.0f, "dB", fx.eq.high);
    TRACK_BOOL("fx.eq.enabled", "EQ on/off", fx.eq.enabled);
    TRACK_FLOAT("fx.comp.thresholdDb", -40.0f, 0.0f, "dB", fx.comp.thresholdDb);
    TRACK_FLOAT("fx.comp.ratio", 1.0f, 12.0f, "1 = off", fx.comp.ratio);
    TRACK_FLOAT("fx.comp.attackMs", 0.5f, 100.0f, "ms", fx.comp.attackMs);
    TRACK_FLOAT("fx.comp.releaseMs", 20.0f, 1000.0f, "ms", fx.comp.releaseMs);
    TRACK_BOOL("fx.comp.enabled", "compressor on/off", fx.comp.enabled);
    TRACK_FLOAT("fx.delay.timeMs", 20.0f, 1000.0f, "ms", fx.delay.timeMs);
    TRACK_FLOAT("fx.delay.feedback", 0.0f, 0.8f, "repeats", fx.delay.feedback);
    TRACK_FLOAT("fx.delay.mix", 0.0f, 1.0f, "0 = off", fx.delay.mix);
    TRACK_BOOL("fx.delay.enabled", "delay on/off", fx.delay.enabled);
    TRACK_FLOAT("fx.reverb.size", 0.0f, 1.0f, "room size", fx.reverb.size);
    TRACK_FLOAT("fx.reverb.damping", 0.0f, 1.0f, "high-frequency damping", fx.reverb.damping);
    TRACK_FLOAT("fx.reverb.mix", 0.0f, 1.0f, "0 = off", fx.reverb.mix);
    TRACK_BOOL("fx.reverb.enabled", "reverb on/off", fx.reverb.enabled);
    TRACK_FLOAT("fx.drive.amount", 0.0f, 1.0f, "overdrive, 0 = off", fx.drive.amount);
    TRACK_FLOAT("fx.drive.tone", 800.0f, 16000.0f, "Hz low-pass after drive", fx.drive.tone);
    TRACK_BOOL("fx.drive.enabled", "drive on/off", fx.drive.enabled);
    TRACK_FLOAT("fx.chorus.mix", 0.0f, 1.0f, "0 = off", fx.chorus.mix);
    TRACK_FLOAT("fx.chorus.rate", 0.1f, 5.0f, "Hz", fx.chorus.rate);
    TRACK_FLOAT("fx.chorus.depth", 0.0f, 1.0f, "", fx.chorus.depth);
    TRACK_BOOL("fx.chorus.enabled", "chorus on/off", fx.chorus.enabled);
    // Sonora Synth (the editable instrument)
    const std::vector<juce::String> waves { "Sine", "Triangle", "Saw", "Square", "Pulse", "Noise" };
    TRACK_ENUM("synth.wave", 0, numSynthWaves - 1, "oscillator 1 shape", synth.wave, waves);
    TRACK_ENUM("synth.wave2", 0, numSynthWaves - 1, "oscillator 2 shape", synth.wave2, waves);
    TRACK_FLOAT("synth.mix2", 0.0f, 1.0f, "oscillator 2 level", synth.mix2);
    TRACK_FLOAT("synth.semis2", -24.0f, 24.0f, "oscillator 2 pitch, semitones", synth.semis2);
    TRACK_FLOAT("synth.detune2", -50.0f, 50.0f, "oscillator 2 fine tune, cents", synth.detune2);
    TRACK_FLOAT("synth.cutoff", 40.0f, 20000.0f, "filter Hz; 20000 = open", synth.cutoff);
    TRACK_FLOAT("synth.resonance", 0.0f, 0.95f, "", synth.resonance);
    TRACK_FLOAT("synth.envAmount", -4.0f, 4.0f, "filter envelope depth, octaves", synth.envAmount);
    TRACK_FLOAT("synth.filterAttack", 0.001f, 5.0f, "s", synth.filterAttack);
    TRACK_FLOAT("synth.filterDecay", 0.001f, 5.0f, "s", synth.filterDecay);
    TRACK_FLOAT("synth.filterSustain", 0.0f, 1.0f, "", synth.filterSustain);
    TRACK_FLOAT("synth.filterRelease", 0.001f, 5.0f, "s", synth.filterRelease);
    TRACK_FLOAT("synth.attack", 0.001f, 5.0f, "s", synth.attack);
    TRACK_FLOAT("synth.decay", 0.001f, 5.0f, "s", synth.decay);
    TRACK_FLOAT("synth.sustain", 0.0f, 1.0f, "", synth.sustain);
    TRACK_FLOAT("synth.release", 0.001f, 5.0f, "s", synth.release);
    TRACK_FLOAT("synth.lfoRate", 0.1f, 20.0f, "Hz", synth.lfoRate);
    TRACK_FLOAT("synth.lfoPitch", 0.0f, 2.0f, "vibrato depth, semitones", synth.lfoPitch);
    TRACK_FLOAT("synth.lfoFilter", 0.0f, 4.0f, "filter wobble, octaves", synth.lfoFilter);
    TRACK_FLOAT("synth.drive", 0.0f, 1.0f, "", synth.drive);
    TRACK_FLOAT("synth.chorus", 0.0f, 1.0f, "", synth.chorus);
    TRACK_FLOAT("synth.level", 0.0f, 1.5f, "", synth.level);
    // Sampler instrument
    TRACK_INT("sampler.rootNote", 0, 127, "MIDI key that plays the sample as recorded (60 = C4)", sampler.rootNote);
    TRACK_FLOAT("sampler.tune", -100.0f, 100.0f, "cents", sampler.tune);
    TRACK_FLOAT("sampler.start", 0.0f, 1.0f, "trim start, fraction of the file", sampler.start);
    TRACK_FLOAT("sampler.end", 0.0f, 1.0f, "trim end, fraction of the file", sampler.end);
    TRACK_FLOAT("sampler.loopStart", 0.0f, 1.0f, "fraction of the file", sampler.loopStart);
    TRACK_FLOAT("sampler.loopEnd", 0.0f, 1.0f, "fraction of the file", sampler.loopEnd);
    TRACK_BOOL("sampler.loop", "sustain by looping while a key is held", sampler.loop);
    TRACK_BOOL("sampler.oneShot", "ignore key release", sampler.oneShot);
    TRACK_BOOL("sampler.reverse", "play backwards", sampler.reverse);
    TRACK_BOOL("sampler.keyTrack", "keys change the pitch", sampler.keyTrack);
    TRACK_FLOAT("sampler.attack", 0.001f, 5.0f, "s", sampler.attack);
    TRACK_FLOAT("sampler.decay", 0.001f, 5.0f, "s", sampler.decay);
    TRACK_FLOAT("sampler.sustain", 0.0f, 1.0f, "", sampler.sustain);
    TRACK_FLOAT("sampler.release", 0.001f, 5.0f, "s", sampler.release);
    TRACK_FLOAT("sampler.gain", 0.0f, 2.0f, "", sampler.gain);
    // Project-wide: tempo, send returns, master limiter
    {
        ParamSpec tempo;
        tempo.name = "project.tempo";
        tempo.perTrack = false;
        tempo.min = 40.0f;
        tempo.max = 240.0f;
        tempo.note = "BPM";
        tempo.get = [](const ProjectState& p, int) { return static_cast<float>(p.bpm); };
        tempo.set = [](ProjectState& p, int, float v) { p.bpm = std::clamp(static_cast<double>(v), 40.0, 240.0); };
        list.push_back(std::move(tempo));
    }
    PROJECT_FLOAT("sends.delayTimeMs", 20.0f, 1000.0f, "shared delay bus time", sends.delay.timeMs);
    PROJECT_FLOAT("sends.delayFeedback", 0.0f, 0.8f, "shared delay bus repeats", sends.delay.feedback);
    PROJECT_FLOAT("sends.delayReturn", 0.0f, 1.5f, "delay bus return level", sends.delayReturn);
    PROJECT_FLOAT("sends.reverbSize", 0.0f, 1.0f, "shared reverb bus room size", sends.reverb.size);
    PROJECT_FLOAT("sends.reverbDamping", 0.0f, 1.0f, "", sends.reverb.damping);
    PROJECT_FLOAT("sends.reverbReturn", 0.0f, 1.5f, "reverb bus return level", sends.reverbReturn);
    PROJECT_FLOAT("master.ceilingDb", -12.0f, 0.0f, "limiter ceiling, dB", master.ceilingDb);
    PROJECT_FLOAT("master.releaseMs", 20.0f, 500.0f, "limiter release", master.releaseMs);
    PROJECT_BOOL("master.enabled", "master limiter on/off", master.enabled);
    return list;
}
#undef TRACK_FLOAT
#undef TRACK_INT
#undef TRACK_ENUM
#undef TRACK_BOOL
#undef PROJECT_FLOAT
#undef PROJECT_BOOL

juce::String formatParam(const ParamSpec& spec, float value)
{
    if (spec.isBool)
        return value >= 0.5f ? "on" : "off";
    if (!spec.choices.empty())
    {
        const int i = static_cast<int>(std::lround(value));
        if (i >= 0 && i < static_cast<int>(spec.choices.size()))
            return spec.choices[idx(i)];
    }
    return number(value);
}

// ---- Name parsers ------------------------------------------------------------------
bool usableTrack(const ProjectState& p, int track)
{
    return track >= 0 && track < maxTracks && p.tracks[idx(track)].kind != TrackKind::None;
}

juce::String trackLabel(const ProjectState& p, int track)
{
    return usableTrack(p, track) ? "\"" + p.tracks[idx(track)].trackName() + "\"" : "track " + juce::String(track);
}

std::vector<juce::String> factoryKitNames()
{
    std::vector<juce::String> names;
    for (int i = 0; i < numKitVariants; ++i)
        names.emplace_back(kitVariantName(i));
    return names;
}

std::optional<int> parseKit(const juce::String& text)
{
    const auto q = lower(text);
    const auto names = factoryKitNames();
    for (std::size_t i = 0; i < names.size(); ++i)
        if (lower(names[i]) == q || q.startsWith(lower(names[i])))
            return static_cast<int>(i);
    return std::nullopt;
}

std::optional<ArpMode> parseArpMode(const juce::String& text)
{
    const auto q = compact(text);
    for (int m = 0; m < static_cast<int>(ArpMode::numModes); ++m)
        if (compact(arpModeName(static_cast<ArpMode>(m))) == q)
            return static_cast<ArpMode>(m);
    if (q == "none" || q == "bypass")
        return ArpMode::Off;
    if (q == "random" || q == "shuffle")
        return ArpMode::Random;
    return std::nullopt;
}

std::optional<ArpRate> parseArpRate(const juce::String& text)
{
    const auto q = compact(text);
    for (int r = 0; r < static_cast<int>(ArpRate::numRates); ++r)
        if (compact(arpRateName(static_cast<ArpRate>(r))) == q)
            return static_cast<ArpRate>(r);
    if (q == "eighth" || q == "8th")
        return ArpRate::Eighth;
    if (q == "sixteenth" || q == "16th")
        return ArpRate::Sixteenth;
    return std::nullopt;
}

// Text with an optional leading verb, e.g. "mute" for take params.
std::optional<bool> parseBoolWord(const juce::String& text)
{
    const auto q = lower(text);
    if (q == "on" || q == "true" || q == "yes" || q == "1" || q == "enabled")
        return true;
    if (q == "off" || q == "false" || q == "no" || q == "0" || q == "disabled")
        return false;
    return std::nullopt;
}
} // namespace

const std::vector<ParamSpec>& parameters()
{
    static const std::vector<ParamSpec> list = buildParameters();
    return list;
}

const ParamSpec* findParam(const juce::String& name)
{
    const auto q = lower(name);
    for (const auto& spec : parameters())
        if (spec.name.toLowerCase() == q)
            return &spec;
    return nullptr;
}

juce::String describeParameters()
{
    // One line per group ("fx.eq", "synth", ...): name[min..max, default].
    static const ProjectState reference;
    std::vector<juce::String> groups;
    std::map<juce::String, juce::StringArray> lines;
    for (const auto& spec : parameters())
    {
        const auto group = spec.name.upToLastOccurrenceOf(".", false, false);
        if (lines.find(group) == lines.end())
            groups.push_back(group);
        juce::String entry = spec.name.fromLastOccurrenceOf(".", false, false) + "[";
        if (spec.isBool)
            entry += "on/off";
        else if (!spec.choices.empty())
        {
            juce::StringArray names;
            for (const auto& choice : spec.choices)
                names.add(choice);
            entry += names.joinIntoString("/");
        }
        else
            entry += number(spec.min) + ".." + number(spec.max);
        entry += ", default " + formatParam(spec, spec.get(reference, 0)) + "]";
        if (spec.note.isNotEmpty())
            entry += " " + spec.note;
        lines[group].add(entry);
    }
    juce::String out;
    for (const auto& group : groups)
        out << group << ": " << lines[group].joinIntoString("; ") << "\n";
    return out;
}

juce::String describeChangedParameters(const ProjectState& project, int track)
{
    if (!usableTrack(project, track))
        return {};
    static const ProjectState reference;
    const auto& state = project.tracks[idx(track)];
    const bool synth = state.kind == TrackKind::Synth && state.instrumentPreset == 0;
    const bool sampler = state.kind == TrackKind::Synth && isSamplerInstrument(state.instrumentPreset);
    juce::StringArray changed;
    for (const auto& spec : parameters())
    {
        if (!spec.perTrack)
            continue;
        if (spec.name.startsWith("synth.") && !synth)
            continue;
        if (spec.name.startsWith("sampler.") && !sampler)
            continue;
        const float now = spec.get(project, track), base = spec.get(reference, 0);
        if (std::abs(now - base) > 1.0e-4f)
            changed.add(spec.name + "=" + formatParam(spec, now));
    }
    return changed.joinIntoString(" ");
}

// ---- Parsers -----------------------------------------------------------------------
std::optional<int> parsePitchClass(const juce::String& text)
{
    const auto q = lower(text);
    if (q.isEmpty())
        return std::nullopt;
    static const std::map<juce::juce_wchar, int> letters { { 'c', 0 }, { 'd', 2 }, { 'e', 4 }, { 'f', 5 },
                                                      { 'g', 7 }, { 'a', 9 }, { 'b', 11 } };
    const auto found = letters.find(q[0]);
    if (found == letters.end())
        return std::nullopt;
    int pc = found->second;
    const auto rest = q.substring(1);
    if (rest.startsWith("#") || rest.startsWith("sharp"))
        ++pc;
    else if (rest.startsWith("b") || rest.startsWith("flat"))
        --pc;
    return ((pc % 12) + 12) % 12;
}

std::optional<MusicScale> parseScale(const juce::String& text)
{
    const auto q = compact(text);
    for (int s = 0; s < static_cast<int>(MusicScale::numScales); ++s)
        if (compact(scaleName(static_cast<MusicScale>(s))) == q)
            return static_cast<MusicScale>(s);
    static const std::map<juce::String, MusicScale> aliases {
        { "minor", MusicScale::NaturalMinor }, { "aeolian", MusicScale::NaturalMinor },
        { "ionian", MusicScale::Major }, { "pentatonic", MusicScale::MajorPentatonic },
        { "majorpent", MusicScale::MajorPentatonic }, { "minorpent", MusicScale::MinorPentatonic } };
    const auto alias = aliases.find(q);
    return alias != aliases.end() ? std::optional<MusicScale>(alias->second) : std::nullopt;
}

std::optional<ChordType> parseChordType(const juce::String& text)
{
    const auto q = compact(text);
    for (int c = 0; c < static_cast<int>(ChordType::numChords); ++c)
        if (compact(chordName(static_cast<ChordType>(c))) == q)
            return static_cast<ChordType>(c);
    static const std::map<juce::String, ChordType> aliases {
        { "maj", ChordType::Major }, { "m", ChordType::Minor }, { "min", ChordType::Minor },
        { "dom7", ChordType::Dom7 }, { "dominant7", ChordType::Dom7 }, { "major7", ChordType::Maj7 },
        { "m7", ChordType::Min7 }, { "minor7", ChordType::Min7 }, { "sus", ChordType::Sus4 },
        { "diminished", ChordType::Dim }, { "augmented", ChordType::Aug } };
    const auto alias = aliases.find(q);
    return alias != aliases.end() ? std::optional<ChordType>(alias->second) : std::nullopt;
}

std::optional<SongPart> parseSongPart(const juce::String& text)
{
    const auto q = compact(text);
    for (int p = 0; p < static_cast<int>(SongPart::numParts); ++p)
        if (compact(songPartName(static_cast<SongPart>(p))) == q)
            return static_cast<SongPart>(p);
    if (q == "section")
        return SongPart::Section;
    return std::nullopt;
}

std::optional<SongTemplate> parseTemplate(const juce::String& text)
{
    const auto q = compact(text);
    if (q.startsWith("simple"))
        return SongTemplate::Simple;
    if (q.startsWith("pop"))
        return SongTemplate::Pop;
    if (q.startsWith("edm") || q.startsWith("dance"))
        return SongTemplate::Edm;
    if (q.startsWith("hiphop") || q.startsWith("rap"))
        return SongTemplate::HipHop;
    return std::nullopt;
}

std::optional<AutomationTarget> parseAutomationTarget(const juce::String& text)
{
    const auto q = compact(text);
    for (int t = 0; t < static_cast<int>(AutomationTarget::numTargets); ++t)
        if (compact(automationTargetName(static_cast<AutomationTarget>(t))) == q)
            return static_cast<AutomationTarget>(t);
    return std::nullopt;
}

int parseLoop(const juce::String& text)
{
    const auto q = text.trim().toUpperCase();
    if (q.length() != 1)
        return -1;
    if (q[0] >= 'A' && q[0] < 'A' + numPatterns)
        return static_cast<int>(q[0] - 'A');
    if (q[0] >= '0' && q[0] < '0' + numPatterns)
        return static_cast<int>(q[0] - '0');
    return -1;
}

// ---- Sound resolution ----------------------------------------------------------------
std::optional<PickerEntry> resolveSound(const juce::String& text, const std::vector<juce::String>& library,
                                        juce::StringArray* suggestions)
{
    const auto entries = buildPickerEntries(library);
    auto q = lower(text);
    // Tolerate the labels the UI shows: "Synth: Super Saw", "Sampler: Kick 01".
    for (const char* prefix : { "synth:", "patch:", "sampler:", "sample:", "instrument:" })
        if (q.startsWith(prefix))
            q = q.fromFirstOccurrenceOf(":", false, false).trim();
    if (q.isEmpty())
        return std::nullopt;
    // 1. Exact name, preferring instruments over patches over samples.
    for (auto kind : { PickerEntry::Kind::Instrument, PickerEntry::Kind::Patch, PickerEntry::Kind::Sample })
        for (const auto& entry : entries)
            if (entry.kind == kind && entry.name.toLowerCase() == q)
                return entry;
    // 2. A sample by file name without folder or extension, or an instrument
    //    with its "(editable)" qualifier dropped.
    for (const auto& entry : entries)
    {
        if (entry.kind == PickerEntry::Kind::Sample
            && sampleDisplayName(entry.name).toLowerCase() == q)
            return entry;
        if (entry.index == 0 && entry.kind == PickerEntry::Kind::Instrument
            && (q == "sonora synth" || q == "synth" || q == "sine keys"))
            return entry;
    }
    // 3. Every word matches; best rank wins, shorter names first.
    const auto words = juce::StringArray::fromTokens(q, " \t", "");
    const PickerEntry* best = nullptr;
    int bestScore = 1 << 20;
    std::vector<std::pair<int, juce::String>> ranked;
    for (const auto& entry : entries)
    {
        const int score = pickerMatchScore(entry, words);
        if (score < 0)
            continue;
        const int weighted = score * 100 + entry.name.length();
        ranked.emplace_back(weighted, entry.name);
        if (weighted < bestScore)
        {
            bestScore = weighted;
            best = &entry;
        }
    }
    if (best != nullptr)
        return *best;
    if (suggestions != nullptr)
    {
        // Nothing matched every word: offer entries that match any word.
        std::vector<std::pair<int, juce::String>> loose;
        for (const auto& entry : entries)
            for (const auto& word : words)
                if (entry.name.toLowerCase().contains(word) || entry.family.toLowerCase().contains(word))
                {
                    loose.emplace_back(entry.name.length(), entry.name);
                    break;
                }
        std::sort(loose.begin(), loose.end());
        for (std::size_t i = 0; i < loose.size() && i < 4; ++i)
            suggestions->add(loose[i].second);
    }
    return std::nullopt;
}

// ---- JSON -> actions ---------------------------------------------------------------------
namespace
{
bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

int clampInt(const juce::var& v, int lo, int hi)
{
    const double d = static_cast<double>(v);
    if (!std::isfinite(d))
        return lo;
    return static_cast<int>(std::clamp(d, static_cast<double>(lo), static_cast<double>(hi)));
}

juce::String text(const juce::DynamicObject& o, const char* key, int maxChars = 200)
{
    const auto v = o.getProperty(key);
    return v.isString() ? cleanName(v.toString(), maxChars) : juce::String();
}

std::optional<double> numberField(const juce::DynamicObject& o, const char* key)
{
    const auto v = o.getProperty(key);
    if (!isNumber(v))
        return std::nullopt;
    const double d = static_cast<double>(v);
    return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
}

std::optional<bool> boolField(const juce::DynamicObject& o, const char* key)
{
    const auto v = o.getProperty(key);
    if (v.isBool())
        return static_cast<bool>(v);
    return std::nullopt;
}

// A track/loop-like integer field: absent -> default, present but not a number -> error.
bool intField(const juce::DynamicObject& o, const char* key, int lo, int hi, int& out, juce::String& error)
{
    if (!o.hasProperty(key))
        return true;
    const auto v = o.getProperty(key);
    if (!isNumber(v))
    {
        error = juce::String("\"") + key + "\" must be a number";
        return false;
    }
    out = clampInt(v, lo, hi);
    return true;
}

bool loopField(const juce::DynamicObject& o, const char* key, int& out, juce::String& error)
{
    if (!o.hasProperty(key))
        return true;
    const auto v = o.getProperty(key);
    out = isNumber(v) ? clampInt(v, 0, numPatterns - 1) : parseLoop(v.toString());
    if (out < 0)
    {
        error = juce::String("\"") + key + "\" must be A, B, C or D";
        return false;
    }
    return true;
}
} // namespace

bool parseAction(const juce::var& json, Action& out, juce::String& error)
{
    const auto* o = json.getDynamicObject();
    if (o == nullptr)
    {
        error = "action is not an object";
        return false;
    }
    Action a;
    a.op = lower(text(*o, "op", 40));
    if (a.op.isEmpty())
    {
        error = "action has no \"op\"";
        return false;
    }
    if (!intField(*o, "track", 0, maxTracks - 1, a.track, error) || !loopField(*o, "loop", a.loop, error)
        || !loopField(*o, "toLoop", a.toLoop, error) || !intField(*o, "section", 0, maxSections - 1, a.section, error)
        || !intField(*o, "to", 0, maxSections - 1, a.to, error) || !intField(*o, "take", 0, 2147483647, a.take, error))
        return false;
    a.value = numberField(*o, "value");
    a.value2 = numberField(*o, "value2");
    a.on = boolField(*o, "on");
    a.latch = boolField(*o, "latch");
    a.text = text(*o, "text");
    a.param = text(*o, "param", 80);
    a.name = text(*o, "name", 80);
    a.kind = text(*o, "kind", 20);
    a.instrument = text(*o, "instrument", 120);
    a.key = text(*o, "key", 20);
    a.scale = text(*o, "scale", 40);
    a.part = text(*o, "part", 40);
    a.target = text(*o, "target", 40);
    a.view = text(*o, "view", 40);
    a.arp = text(*o, "arp", 20);
    a.rate = text(*o, "rate", 20);
    a.chordType = text(*o, "chordType", 20);
    if (const auto* notes = o->getProperty("notes").getArray())
    {
        a.hasNotes = true;
        for (const auto& item : *notes)
        {
            const auto* n = item.getDynamicObject();
            if (n == nullptr || !isNumber(n->getProperty("start")) || !isNumber(n->getProperty("pitch")))
                continue;
            Note note;
            note.start = clampInt(n->getProperty("start"), 0, patternTicks - 1);
            note.duration = isNumber(n->getProperty("duration")) ? clampInt(n->getProperty("duration"), 30, patternTicks)
                                                                  : stepTicks;
            note.duration = std::min(note.duration, patternTicks - note.start);
            note.pitch = clampInt(n->getProperty("pitch"), lowestPitch, highestPitch);
            note.velocity = isNumber(n->getProperty("velocity")) ? clampInt(n->getProperty("velocity"), 1, 127) : 100;
            a.notes.push_back(note);
            if (a.notes.size() >= 1024)
                break;
        }
    }
    if (const auto* hits = o->getProperty("hits").getArray())
    {
        a.hasHits = true;
        for (const auto& item : *hits)
        {
            const auto* h = item.getDynamicObject();
            if (h == nullptr || !isNumber(h->getProperty("pad")) || !isNumber(h->getProperty("step")))
                continue;
            Hit hit;
            hit.pad = clampInt(h->getProperty("pad"), 0, drumPads - 1);
            hit.step = clampInt(h->getProperty("step"), 0, gridSteps - 1);
            hit.velocity = isNumber(h->getProperty("velocity")) ? clampInt(h->getProperty("velocity"), 1, 127) : 100;
            a.hits.push_back(hit);
            if (a.hits.size() >= 2048)
                break;
        }
    }
    if (const auto* points = o->getProperty("points").getArray())
    {
        a.hasPoints = true;
        for (const auto& item : *points)
        {
            const auto* pt = item.getDynamicObject();
            if (pt == nullptr || !isNumber(pt->getProperty("tick")) || !isNumber(pt->getProperty("value")))
                continue;
            const double value = static_cast<double>(pt->getProperty("value"));
            if (!std::isfinite(value))
                continue;
            a.points.push_back({ clampInt(pt->getProperty("tick"), 0, patternTicks), static_cast<float>(value) });
            if (a.points.size() >= 256)
                break;
        }
    }
    if (const auto* sections = o->getProperty("sections").getArray())
    {
        a.hasSections = true;
        for (const auto& item : *sections)
        {
            const auto* s = item.getDynamicObject();
            if (s == nullptr)
                continue;
            SectionSpec spec;
            const auto part = parseSongPart(text(*s, "part", 40));
            spec.part = part.value_or(SongPart::Section);
            if (const auto* tracks = s->getProperty("tracks").getArray())
                for (const auto& entry : *tracks)
                {
                    const auto* t = entry.getDynamicObject();
                    if (t == nullptr || !isNumber(t->getProperty("track")))
                        continue;
                    const auto loopValue = t->getProperty("loop");
                    const int loop = isNumber(loopValue) ? clampInt(loopValue, 0, numPatterns - 1)
                                                         : parseLoop(loopValue.toString());
                    if (loop < 0)
                        continue; // "off" or unreadable: the track stays silent
                    spec.tracks.emplace_back(clampInt(t->getProperty("track"), 0, maxTracks - 1), loop);
                }
            const auto chordKey = text(*s, "chordKey", 20);
            if (chordKey.isNotEmpty())
                if (const auto root = parsePitchClass(chordKey))
                    spec.chord = { *root, parseChordType(text(*s, "chordType", 20)).value_or(ChordType::Major) };
            a.sections.push_back(std::move(spec));
            if (a.sections.size() >= static_cast<std::size_t>(maxSections))
                break;
        }
    }
    out = std::move(a);
    return true;
}

std::vector<Action> parseActions(const juce::var& list, juce::StringArray& errors)
{
    std::vector<Action> actions;
    const auto* array = list.getArray();
    if (array == nullptr)
        return actions;
    int index = 0;
    for (const auto& item : *array)
    {
        ++index;
        if (static_cast<int>(actions.size()) >= maxActions)
        {
            errors.add("Skipped " + juce::String(array->size() - index + 1) + " actions beyond the limit of "
                       + juce::String(maxActions) + ".");
            break;
        }
        Action action;
        juce::String error;
        if (parseAction(item, action, error))
            actions.push_back(std::move(action));
        else
            errors.add("Action " + juce::String(index) + ": " + error + ".");
    }
    return actions;
}

// ---- Applying ------------------------------------------------------------------------------
namespace
{
enum class Need { Any, Synth, Drums };

int editDistance(const juce::String& a, const juce::String& b)
{
    std::vector<int> previous(static_cast<std::size_t>(b.length()) + 1), current(previous.size());
    for (std::size_t j = 0; j < previous.size(); ++j)
        previous[j] = static_cast<int>(j);
    for (int i = 1; i <= a.length(); ++i)
    {
        current[0] = i;
        for (int j = 1; j <= b.length(); ++j)
            current[idx(j)] = std::min({ previous[idx(j)] + 1, current[idx(j - 1)] + 1,
                                         previous[idx(j - 1)] + (a[i - 1] == b[j - 1] ? 0 : 1) });
        std::swap(previous, current);
    }
    return previous[static_cast<std::size_t>(b.length())];
}

// The parameters a misspelled name was most likely meant to be.
juce::StringArray closestParams(const juce::String& wanted)
{
    const auto q = lower(wanted);
    const auto leaf = q.fromLastOccurrenceOf(".", false, false);
    std::vector<std::pair<int, juce::String>> ranked;
    for (const auto& spec : parameters())
    {
        const auto name = spec.name.toLowerCase();
        const auto specLeaf = name.fromLastOccurrenceOf(".", false, false);
        int distance = std::min(editDistance(q, name), editDistance(leaf, specLeaf) + 1);
        if (leaf.length() >= 3 && name.contains(leaf))
            distance = std::min(distance, 1);
        if (distance <= std::max(3, q.length() / 4))
            ranked.emplace_back(distance, spec.name);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& x, const auto& y) { return x.first < y.first; });
    juce::StringArray names;
    for (std::size_t i = 0; i < ranked.size() && i < 4; ++i)
        names.add(ranked[i].second);
    return names;
}

struct Applier
{
    ProjectState& project;
    const Context& context;
    Report& report;
    int ordinal = 0;

    int track(const Action& a, Need need, juce::String& error)
    {
        const int t = a.track >= 0 ? a.track : context.selectedTrack;
        if (!usableTrack(project, t))
        {
            error = "there is no track " + juce::String(t);
            return -1;
        }
        if (need == Need::Synth && project.tracks[idx(t)].kind != TrackKind::Synth)
        {
            error = trackLabel(project, t) + " is a drum track; this needs an instrument track";
            return -1;
        }
        if (need == Need::Drums && project.tracks[idx(t)].kind != TrackKind::Drums)
        {
            error = trackLabel(project, t) + " is an instrument track; this needs a drum track";
            return -1;
        }
        return t;
    }

    int loopOf(const Action& a, int t) const
    {
        return a.loop >= 0 ? a.loop : std::clamp(context.loopSlot[idx(t)], 0, numPatterns - 1);
    }

    static juce::String loopName(int loop) { return juce::String::charToString(static_cast<juce::juce_wchar>('A' + loop)); }

    void applySound(Track& state, const PickerEntry& entry)
    {
        switch (entry.kind)
        {
            case PickerEntry::Kind::Instrument:
                if (isGeneratedTrackName(state.trackName()))
                    state.setTrackName(instrumentDisplayName(entry.index) == "Sonora Synth (editable)"
                                           ? juce::String("Sine Keys")
                                           : instrumentDisplayName(entry.index));
                state.instrumentPreset = entry.index;
                break;
            case PickerEntry::Kind::Patch:
                state.synth = synthPatches()[idx(entry.index)].params;
                state.instrumentPreset = 0;
                if (isGeneratedTrackName(state.trackName()))
                    state.setTrackName(entry.name);
                break;
            case PickerEntry::Kind::Sample:
            {
                const bool changed = state.samplerFileName() != entry.sample;
                state.instrumentPreset = samplerInstrument;
                state.setSamplerFileName(entry.sample);
                if (changed)
                {
                    state.sampler.start = 0.0f;
                    state.sampler.end = 1.0f;
                    state.sampler.loopStart = 0.0f;
                    state.sampler.loopEnd = 1.0f;
                }
                if (isGeneratedTrackName(state.trackName()))
                    state.setTrackName(sampleDisplayName(entry.sample).substring(0, 40));
                break;
            }
        }
    }

    // Each returns an error message, or empty on success (and logs "done").
    juce::String run(const Action& a)
    {
        ++ordinal;
        const auto& op = a.op;
        juce::String error;

        if (op == "set_tempo")
        {
            if (!a.value)
                return "set_tempo needs a value (BPM)";
            project.bpm = std::clamp(*a.value, 40.0, 240.0);
            report.done.add("Tempo set to " + number(project.bpm) + " BPM.");
            return {};
        }
        if (op == "set_key")
        {
            const auto root = parsePitchClass(a.key);
            if (!root)
                return "set_key needs a key such as C, F#, Bb";
            // Validate everything before touching the project: a request that is
            // reported as failed must not have changed anything.
            std::optional<MusicScale> scale;
            if (a.scale.isNotEmpty())
            {
                scale = parseScale(a.scale);
                if (!scale)
                    return "unknown scale \"" + a.scale + "\"";
            }
            project.musicKey = *root;
            if (scale)
                project.musicScale = *scale;
            report.done.add(juce::String("Key set to ") + keyName(project.musicKey) + " " + scaleName(project.musicScale) + ".");
            return {};
        }
        if (op == "set_param")
        {
            const auto* spec = findParam(a.param);
            if (spec == nullptr)
            {
                const auto close = closestParams(a.param);
                return "unknown parameter \"" + a.param + "\"" + (close.isEmpty() ? juce::String()
                                                                                : " (did you mean " + close.joinIntoString(", ") + "?)");
            }
            float value = 0.0f;
            if (a.value)
                value = static_cast<float>(*a.value);
            else if (a.on)
                value = *a.on ? 1.0f : 0.0f;
            else if (a.text.isNotEmpty())
            {
                bool matched = false;
                for (std::size_t i = 0; i < spec->choices.size(); ++i)
                    if (lower(spec->choices[i]) == lower(a.text))
                    {
                        value = static_cast<float>(i);
                        matched = true;
                    }
                if (!matched)
                {
                    const auto word = parseBoolWord(a.text);
                    if (!word || !spec->isBool)
                        return "\"" + a.text + "\" is not a valid value for " + spec->name;
                    value = *word ? 1.0f : 0.0f;
                }
            }
            else
                return spec->name + " needs a value";
            int t = 0;
            if (spec->perTrack)
            {
                const bool synthOnly = spec->name.startsWith("synth.") || spec->name.startsWith("sampler.");
                t = track(a, synthOnly ? Need::Synth : Need::Any, error);
                if (t < 0)
                    return error;
            }
            spec->set(project, t, value);
            if (spec->name.startsWith("sampler."))
                normalizeRegions(project.tracks[idx(t)].sampler);
            juce::String note;
            if (spec->name.startsWith("synth.") && project.tracks[idx(t)].instrumentPreset != 0)
                note = " (audible when the track uses the Sonora Synth)";
            if (spec->name.startsWith("sampler.") && !isSamplerInstrument(project.tracks[idx(t)].instrumentPreset))
                note = " (audible when the track uses the Sampler)";
            report.done.add((spec->perTrack ? trackLabel(project, t) + ": " : juce::String()) + spec->name + " = "
                            + formatParam(*spec, spec->get(project, t)) + "." + note);
            return {};
        }
        if (op == "add_track")
        {
            int free = -1;
            for (int i = 0; i < maxTracks && free < 0; ++i)
                if (project.tracks[idx(i)].kind == TrackKind::None)
                    free = i;
            if (free < 0)
                return "the project already has 8 tracks";
            const auto kind = lower(a.kind);
            if (kind != "synth" && kind != "drums" && kind != "instrument" && kind != "keys")
                return "add_track needs kind \"synth\" or \"drums\"";
            const bool drums = kind == "drums";
            Track made;
            made.id = nextTrackId(project);
            made.kind = drums ? TrackKind::Drums : TrackKind::Synth;
            made.icon = static_cast<std::uint8_t>(drums ? 1 : 0);
            made.setTrackName(drums ? "Drums " + juce::String(free + 1) : "Keys " + juce::String(free + 1));
            juce::String soundNote;
            if (!drums && a.instrument.isNotEmpty())
            {
                juce::StringArray suggestions;
                if (const auto entry = resolveSound(a.instrument, context.library, &suggestions))
                {
                    applySound(made, *entry);
                    // A new track is named for its sound rather than "Keys 3"
                    // (an explicit name below still wins).
                    made.setTrackName(entry->kind == PickerEntry::Kind::Sample
                                          ? sampleDisplayName(entry->sample).substring(0, 40)
                                          : entry->index == 0 && entry->kind == PickerEntry::Kind::Instrument
                                              ? juce::String("Sine Keys")
                                              : entry->name);
                    soundNote = " with " + describeTrackInstrument(made);
                }
                else
                    soundNote = " (no sound matches \"" + a.instrument + "\"; left on the default synth)";
            }
            const auto name = cleanName(a.name);
            if (name.isNotEmpty())
                made.setTrackName(name);
            project.tracks[idx(free)] = made;
            report.done.add("Added " + juce::String(drums ? "drum" : "instrument") + " track \""
                            + made.trackName() + "\" as track " + juce::String(free) + soundNote + ".");
            return {};
        }
        if (op == "remove_track")
        {
            const int t = track(a, Need::Any, error);
            if (t < 0)
                return error;
            int usable = 0;
            for (const auto& state : project.tracks)
                usable += state.kind == TrackKind::None ? 0 : 1;
            if (usable <= 1)
                return "a project needs at least one track";
            const auto label = trackLabel(project, t);
            project.tracks[idx(t)] = Track {};
            report.done.add("Removed " + label + ".");
            return {};
        }
        if (op == "rename_track")
        {
            const int t = track(a, Need::Any, error);
            if (t < 0)
                return error;
            const auto name = cleanName(a.name.isNotEmpty() ? a.name : a.text);
            if (name.isEmpty())
                return "rename_track needs a name";
            const auto old = trackLabel(project, t);
            project.tracks[idx(t)].setTrackName(name);
            report.done.add("Renamed " + old + " to \"" + name + "\".");
            return {};
        }
        if (op == "move_track")
        {
            const int t = track(a, Need::Any, error);
            if (t < 0)
                return error;
            if (a.to < 0 || !moveTrackState(project, t, a.to))
                return "move_track needs a destination (\"to\") that is another existing track";
            report.done.add("Swapped track " + juce::String(t) + " with track " + juce::String(a.to) + ".");
            return {};
        }
        if (op == "set_instrument")
        {
            const int t = track(a, Need::Synth, error);
            if (t < 0)
                return error;
            juce::StringArray suggestions;
            const auto wanted = a.instrument.isNotEmpty() ? a.instrument : a.name.isNotEmpty() ? a.name : a.text;
            const auto entry = resolveSound(wanted, context.library, &suggestions);
            if (!entry)
                return "no sound matches \"" + wanted + "\""
                    + (suggestions.isEmpty() ? juce::String() : " (closest: " + suggestions.joinIntoString(", ") + ")");
            applySound(project.tracks[idx(t)], *entry);
            report.done.add(trackLabel(project, t) + " now plays " + describeTrackInstrument(project.tracks[idx(t)]) + ".");
            return {};
        }
        if (op == "set_kit")
        {
            const int t = track(a, Need::Drums, error);
            if (t < 0)
                return error;
            const auto kit = parseKit(a.name.isNotEmpty() ? a.name : a.text);
            if (!kit)
            {
                juce::StringArray names;
                for (const auto& n : factoryKitNames())
                    names.add(n);
                return "unknown kit \"" + a.name + "\" (available: " + names.joinIntoString(", ") + ")";
            }
            project.tracks[idx(t)].kitVariant = *kit;
            report.done.add(trackLabel(project, t) + " uses the " + juce::String(kitVariantName(*kit)) + " kit.");
            return {};
        }
        if (op == "write_melody" || op == "write_drums" || op == "clear_loop" || op == "copy_loop"
            || op == "transpose_loop" || op == "quantize_loop" || op == "humanize_loop" || op == "velocity_ramp")
            return runPattern(a, error);
        if (op == "set_song" || op == "set_cell" || op == "set_part" || op == "set_chord" || op == "add_section"
            || op == "duplicate_section" || op == "remove_section" || op == "move_section" || op == "apply_template")
            return runArrangement(a);
        if (op == "set_automation" || op == "clear_automation")
            return runAutomation(a, error);
        if (op == "set_live_fx")
            return runLiveFx(a, error);
        if (op == "set_take")
            return runTake(a);
        return runApp(a);
    }

    juce::String runPattern(const Action& a, juce::String& error)
    {
        const bool wantsDrums = a.op == "write_drums";
        const bool melodic = a.op == "write_melody" || a.op == "transpose_loop" || a.op == "quantize_loop"
            || a.op == "velocity_ramp";
        const int t = track(a, wantsDrums ? Need::Drums : melodic ? Need::Synth : Need::Any, error);
        if (t < 0)
            return error;
        auto& state = project.tracks[idx(t)];
        const int loop = loopOf(a, t);
        const bool drums = state.kind == TrackKind::Drums;
        const auto where = trackLabel(project, t) + " loop " + loopName(loop);
        auto& melody = state.melodies[idx(loop)];
        auto& grid = state.drumPatterns[idx(loop)];
        if (a.op == "write_melody")
        {
            if (!a.hasNotes)
                return "write_melody needs a \"notes\" list";
            Pattern pattern;
            packNotes(pattern, a.notes);
            melody = pattern;
            report.done.add(where + ": wrote " + juce::String(pattern.count) + " notes"
                            + (pattern.count == 0 ? " (cleared)." : "."));
            return {};
        }
        if (a.op == "write_drums")
        {
            if (!a.hasHits)
                return "write_drums needs a \"hits\" list";
            DrumPattern pattern;
            for (const auto& hit : a.hits)
            {
                auto& cell = pattern.steps[idx(hit.pad)][idx(hit.step)];
                cell = static_cast<std::uint8_t>(std::max<int>(cell, hit.velocity));
            }
            grid = pattern;
            report.done.add(where + ": wrote " + juce::String(pattern.hitCount()) + " hits"
                            + (pattern.hitCount() == 0 ? " (cleared)." : "."));
            return {};
        }
        if (a.op == "clear_loop")
        {
            melody = {};
            grid = {};
            report.done.add(where + " cleared.");
            return {};
        }
        if (a.op == "copy_loop")
        {
            if (a.toLoop < 0 || a.toLoop == loop)
                return "copy_loop needs a different destination loop (\"toLoop\")";
            state.melodies[idx(a.toLoop)] = melody;
            state.drumPatterns[idx(a.toLoop)] = grid;
            report.done.add(trackLabel(project, t) + ": copied loop " + loopName(loop) + " to loop "
                            + loopName(a.toLoop) + ".");
            return {};
        }
        if (a.op == "transpose_loop")
        {
            if (!a.value)
                return "transpose_loop needs a value (semitones)";
            const int shift = static_cast<int>(std::clamp(std::lround(*a.value), -48L, 48L));
            std::vector<Note> shifted(melody.notes.begin(), melody.notes.begin() + melody.count);
            for (auto& note : shifted)
                note.pitch = std::clamp(note.pitch + shift, lowestPitch, highestPitch);
            packNotes(melody, shifted);
            report.done.add(where + ": transposed " + juce::String(shift >= 0 ? "+" : "") + juce::String(shift)
                            + " semitones.");
            return {};
        }
        if (a.op == "quantize_loop")
        {
            if (drums)
                return "drum hits already sit on the grid";
            const float strength = static_cast<float>(std::clamp(a.value.value_or(1.0), 0.0, 1.0));
            quantizePattern(melody, strength);
            report.done.add(where + ": quantized at " + number(strength * 100.0) + "%.");
            return {};
        }
        if (a.op == "humanize_loop")
        {
            const auto seed = static_cast<std::uint32_t>(0x5EED0000u + static_cast<std::uint32_t>(ordinal));
            const float timing = static_cast<float>(std::clamp(a.value.value_or(0.3), 0.0, 1.0));
            const float velocity = static_cast<float>(std::clamp(a.value2.value_or(0.3), 0.0, 1.0));
            if (drums)
                humanizeDrums(grid, velocity, seed);
            else
                humanizePattern(melody, timing, velocity, seed);
            report.done.add(where + ": humanized.");
            return {};
        }
        // velocity_ramp
        if (!a.value || !a.value2)
            return "velocity_ramp needs value (start velocity) and value2 (end velocity)";
        applyVelocityRamp(melody, static_cast<int>(std::lround(*a.value)), static_cast<int>(std::lround(*a.value2)));
        report.done.add(where + ": velocity ramp " + number(*a.value) + " to " + number(*a.value2) + ".");
        return {};
    }

    juce::String runArrangement(const Action& a)
    {
        auto& song = project.song;
        const auto section = [&](int value, const char* what) -> juce::String {
            if (value < 0 || value >= song.sections)
                return juce::String(what) + " needs a section between 0 and " + juce::String(song.sections - 1);
            return {};
        };
        if (a.op == "set_song")
        {
            if (!a.hasSections || a.sections.empty() || static_cast<int>(a.sections.size()) > maxSections)
                return "set_song needs between 1 and 16 sections";
            Arrangement arrangement;
            arrangement.sections = static_cast<int>(a.sections.size());
            for (auto& row : arrangement.trackOn)
                row.fill(false);
            int ignored = 0;
            for (std::size_t s = 0; s < a.sections.size(); ++s)
            {
                arrangement.parts[s] = a.sections[s].part;
                arrangement.chords[s] = a.sections[s].chord;
                for (const auto& [t, loop] : a.sections[s].tracks)
                {
                    if (!usableTrack(project, t))
                    {
                        ++ignored;
                        continue;
                    }
                    arrangement.trackOn[s][idx(t)] = true;
                    arrangement.slots[s][idx(t)] = static_cast<std::uint8_t>(loop);
                }
            }
            project.song = arrangement;
            report.done.add("Arranged the song: " + juce::String(arrangement.sections) + " sections ("
                            + juce::String(arrangement.sections * 4) + " bars)"
                            + (ignored > 0 ? ", ignoring " + juce::String(ignored) + " entries for missing tracks." : "."));
            return {};
        }
        if (a.op == "apply_template")
        {
            const auto tpl = parseTemplate(a.name.isNotEmpty() ? a.name : a.text);
            if (!tpl)
                return "unknown template \"" + a.name + "\" (simple, pop, edm, hip-hop)";
            project.song = buildSongFromTemplate(project, *tpl);
            report.done.add("Built a " + juce::String(songTemplateName(*tpl)).upToFirstOccurrenceOf("  (", false, false)
                            + " arrangement (" + juce::String(project.song.sections) + " sections).");
            return {};
        }
        if (a.op == "add_section")
        {
            const int at = a.section >= 0 ? std::min(a.section + 1, song.sections) : song.sections;
            if (!song.insertSection(at))
                return "the song already has 16 sections";
            report.done.add("Added an empty section at position " + juce::String(at + 1) + ".");
            return {};
        }
        if (a.op == "duplicate_section")
        {
            if (const auto e = section(a.section, "duplicate_section"); e.isNotEmpty())
                return e;
            if (!song.duplicateSection(a.section))
                return "the song already has 16 sections";
            report.done.add("Duplicated section " + juce::String(a.section + 1) + ".");
            return {};
        }
        if (a.op == "remove_section")
        {
            if (const auto e = section(a.section, "remove_section"); e.isNotEmpty())
                return e;
            if (!song.removeSection(a.section))
                return "a song needs at least one section";
            report.done.add("Removed section " + juce::String(a.section + 1) + ".");
            return {};
        }
        if (a.op == "move_section")
        {
            if (const auto e = section(a.section, "move_section"); e.isNotEmpty())
                return e;
            if (a.to < 0 || !song.moveSection(a.section, a.to))
                return "move_section needs a different destination section (\"to\") inside the song";
            report.done.add("Moved section " + juce::String(a.section + 1) + " to position " + juce::String(a.to + 1) + ".");
            return {};
        }
        if (const auto e = section(a.section, a.op.toRawUTF8()); e.isNotEmpty())
            return e;
        const auto s = idx(a.section);
        if (a.op == "set_part")
        {
            const auto part = parseSongPart(a.part.isNotEmpty() ? a.part : a.name);
            if (!part)
                return "unknown part \"" + a.part + "\" (Intro, Verse, Pre-Chorus, Chorus, Bridge, Break, Build, Drop, Outro)";
            song.parts[s] = *part;
            report.done.add("Section " + juce::String(a.section + 1) + " is now a " + songPartName(*part) + ".");
            return {};
        }
        if (a.op == "set_chord")
        {
            if (a.key.isEmpty() || lower(a.key) == "none" || lower(a.key) == "off")
            {
                song.chords[s] = {};
                report.done.add("Section " + juce::String(a.section + 1) + ": chord cleared.");
                return {};
            }
            const auto root = parsePitchClass(a.key);
            if (!root)
                return "set_chord needs a key such as C, F#, Bb or \"none\"";
            const auto type = a.chordType.isEmpty() ? std::optional<ChordType>(ChordType::Major) : parseChordType(a.chordType);
            if (!type)
                return "unknown chord type \"" + a.chordType + "\"";
            song.chords[s] = { *root, *type };
            report.done.add("Section " + juce::String(a.section + 1) + " follows " + chordLabel(song.chords[s]) + ".");
            return {};
        }
        // set_cell
        juce::String error;
        const int t = track(a, Need::Any, error);
        if (t < 0)
            return error;
        const bool on = a.on.value_or(true);
        if (a.loop >= 0)
            song.slots[s][idx(t)] = static_cast<std::uint8_t>(a.loop);
        song.trackOn[s][idx(t)] = on;
        report.done.add("Section " + juce::String(a.section + 1) + ": " + trackLabel(project, t)
                        + (on ? " plays loop " + loopName(song.slots[s][idx(t)]) + "." : juce::String(" is silent.")));
        return {};
    }

    juce::String runAutomation(const Action& a, juce::String& error)
    {
        const int t = track(a, Need::Any, error);
        if (t < 0)
            return error;
        const int loop = loopOf(a, t);
        auto& lanes = project.tracks[idx(t)].automation[idx(loop)];
        const auto where = trackLabel(project, t) + " loop " + loopName(loop);
        if (a.op == "clear_automation")
        {
            if (a.target.isEmpty())
            {
                for (auto& lane : lanes)
                    lane = {};
                report.done.add(where + ": cleared all automation.");
                return {};
            }
            const auto target = parseAutomationTarget(a.target);
            if (!target)
                return "unknown automation target \"" + a.target + "\"";
            lanes[idx(static_cast<int>(*target))] = {};
            report.done.add(where + ": cleared " + automationTargetName(*target) + " automation.");
            return {};
        }
        const auto target = parseAutomationTarget(a.target);
        if (!target)
            return "unknown automation target \"" + a.target
                + "\" (Volume, Pan, Send delay, Send reverb, Drive, Delay mix)";
        if (!a.hasPoints)
            return "set_automation needs a \"points\" list of {tick, value}";
        const auto [lo, hi] = automationRange(*target);
        std::vector<AutomationPoint> points = a.points;
        for (auto& point : points)
            point.value = std::clamp(point.value, lo, hi);
        std::stable_sort(points.begin(), points.end(),
                         [](const AutomationPoint& x, const AutomationPoint& y) { return x.tick < y.tick; });
        AutomationLane lane;
        for (const auto& point : points)
            if (lane.count < maxAutomationPoints)
                lane.points[idx(lane.count++)] = point;
        lanes[idx(static_cast<int>(*target))] = lane;
        report.done.add(where + ": " + automationTargetName(*target) + " automation with " + juce::String(lane.count)
                        + " points" + (static_cast<int>(points.size()) > lane.count ? " (extra points dropped)." : "."));
        return {};
    }

    juce::String runLiveFx(const Action& a, juce::String& error)
    {
        const int t = track(a, Need::Synth, error);
        if (t < 0)
            return error;
        auto fx = project.tracks[idx(t)].liveFx;
        if (a.arp.isNotEmpty())
        {
            const auto mode = parseArpMode(a.arp);
            if (!mode)
                return "unknown arp mode \"" + a.arp + "\" (Off, Up, Down, Up-down, Random)";
            fx.arp = *mode;
        }
        if (a.rate.isNotEmpty())
        {
            const auto rate = parseArpRate(a.rate);
            if (!rate)
                return "unknown arp rate \"" + a.rate + "\" (1/8, 1/16, 1/8T, 1/16T)";
            fx.rate = *rate;
        }
        if (a.value)
            fx.octaves = static_cast<int>(std::clamp(std::lround(*a.value), 1L, 3L));
        if (a.latch)
            fx.latch = *a.latch;
        if (a.chordType.isNotEmpty())
        {
            if (lower(a.chordType) == "off" || lower(a.chordType) == "none")
                fx.chordOn = false;
            else if (const auto type = parseChordType(a.chordType))
            {
                fx.chordOn = true;
                fx.chord = *type;
            }
            else
                return "unknown chord type \"" + a.chordType + "\"";
        }
        project.tracks[idx(t)].liveFx = fx;
        report.done.add(trackLabel(project, t) + ": live " + (fx.arp != ArpMode::Off ? juce::String("arp ") + arpModeName(fx.arp) + " " + arpRateName(fx.rate) : juce::String("arp off"))
                        + (fx.chordOn ? juce::String(", chord ") + chordName(fx.chord) : juce::String()) + ".");
        return {};
    }

    juce::String runTake(const Action& a)
    {
        AudioTakeMeta* take = nullptr;
        for (int i = 0; i < project.takeCount; ++i)
            if (static_cast<int>(project.takes[idx(i)].id) == a.take)
                take = &project.takes[idx(i)];
        if (take == nullptr)
            return "there is no take " + juce::String(a.take);
        const auto param = lower(a.param);
        if (param == "mute" || param == "solo")
        {
            const bool on = a.on.value_or(a.value.value_or(1.0) >= 0.5);
            (param == "mute" ? take->mute : take->solo) = on;
        }
        else if (param == "gain")
        {
            if (!a.value)
                return "set_take gain needs a value (0 to 2)";
            take->gain = static_cast<float>(std::clamp(*a.value, 0.0, 2.0));
        }
        else if (param == "stretch")
        {
            if (!a.value)
                return "set_take stretch needs a value (0.5 to 2)";
            take->stretch = static_cast<float>(std::clamp(*a.value, 0.5, 2.0));
        }
        else
            return "set_take param must be mute, solo, gain or stretch";
        report.done.add("Take " + juce::String(a.take) + ": " + param + " set.");
        return {};
    }

    // Things only the running app can do. They become effects the app performs
    // with its own buttons and dialogs after the project edit lands.
    juce::String runApp(const Action& a)
    {
        using K = UiEffect::Kind;
        auto effect = [&](K kind, const juce::String& message, int number = -1, const juce::String& text = {}) {
            report.effects.push_back({ kind, number, text });
            report.done.add(message);
            return juce::String();
        };
        const auto& op = a.op;
        if (op == "play")
            return effect(K::Play, a.section >= 0 ? "Playing from section " + juce::String(a.section + 1) + "." : "Playing.",
                          a.section);
        if (op == "stop")
            return effect(K::Stop, "Stopped.");
        if (op == "panic")
            return effect(K::Panic, "Silenced every voice.");
        if (op == "set_view")
        {
            const auto view = compact(a.view);
            static const std::vector<juce::String> known { "loop", "song", "mixer", "soundeditor", "kiteditor",
                                                            "automation" };
            if (std::find(known.begin(), known.end(), view) == known.end())
                return "set_view needs view loop, song, mixer, sound editor, kit editor or automation";
            return effect(K::SetView, "Showing the " + a.view + " view.", -1, view);
        }
        if (op == "select_track")
        {
            const int t = a.track >= 0 ? a.track : context.selectedTrack;
            if (!usableTrack(project, t))
                return "there is no track " + juce::String(t);
            return effect(K::SelectTrack, "Selected " + trackLabel(project, t) + ".", t);
        }
        if (op == "edit_section")
        {
            if (a.section < 0 || a.section >= project.song.sections)
                return "edit_section needs a section inside the song";
            return effect(K::EditSection, "Editing section " + juce::String(a.section + 1) + " in the loop view.", a.section);
        }
        if (op == "save_project")
            return effect(K::Save, "Saving the project.");
        if (op == "export_audio")
            return effect(K::Export, "Opened the export panel.");
        if (op == "record_take")
            return effect(K::ToggleRecord, "Toggled recording.");
        if (op == "take_to_sampler")
        {
            if (a.take < 0)
                return "take_to_sampler needs a take id";
            return effect(K::TakeToSampler, "Sending take " + juce::String(a.take) + " to a new sampler track.", a.take);
        }
        if (op == "delete_take")
        {
            if (a.take < 0)
                return "delete_take needs a take id";
            return effect(K::DeleteTake, "Deleting take " + juce::String(a.take) + ".", a.take);
        }
        if (op == "undo" || op == "redo" || op == "new_project" || op == "open_project")
        {
            if (!soleAction)
                return a.op + " must be the only action in a request";
            const K kind = op == "undo" ? K::Undo : op == "redo" ? K::Redo : op == "new_project" ? K::NewProject : K::OpenProject;
            return effect(kind, op == "undo" ? "Undid the last change."
                                : op == "redo" ? "Redid the last change."
                                : op == "new_project" ? "Starting a new project (you will be asked to confirm)."
                                                      : "Opening the file chooser.");
        }
        return "unknown action \"" + op + "\"";
    }

    bool soleAction = false;
};
} // namespace

Report applyActions(ProjectState& project, const std::vector<Action>& actions, const Context& context)
{
    Report report;
    const auto backup = std::make_unique<ProjectState>(project);
    Applier applier { project, context, report };
    applier.soleAction = actions.size() == 1;
    for (const auto& action : actions)
    {
        const auto error = applier.run(action);
        if (error.isNotEmpty())
            report.failed.add(action.op + ": " + error + ".");
    }
    if (!project.valid())
    {
        // A sanitizing gap must never corrupt the project: roll everything back.
        project = *backup;
        report.done.clear();
        report.effects.clear();
        report.failed.add("The changes could not be applied safely, so nothing was changed.");
        report.changed = false;
        return report;
    }
    report.changed = !(project == *backup);
    return report;
}
} // namespace sonora::agent
