#include "AiAgent.h"
#include "AiShared.h"
#include "DrumSampler.h"
#include <algorithm>
#include <map>

namespace sonora::ai
{
using namespace detail;

namespace
{
juce::String loopLetter(int slot) { return juce::String::charToString(static_cast<juce::juce_wchar>('A' + slot)); }

// "Piano: Grand Piano, Bright Piano, ..." for every family, in table order.
juce::String soundCatalog()
{
    std::vector<juce::String> families;
    std::map<juce::String, juce::StringArray> names;
    for (std::size_t i = 1; i < instruments.size(); ++i)
    {
        if (isSamplerInstrument(static_cast<int>(i)))
            continue;
        const juce::String family = instruments[i].family;
        if (names.find(family) == names.end())
            families.push_back(family);
        names[family].add(instruments[i].name);
    }
    juce::String out;
    for (const auto& family : families)
        out << "- " << family << ": " << names[family].joinIntoString(", ") << "\n";
    juce::StringArray patches;
    for (const auto& patch : synthPatches())
        patches.add(patch.name);
    out << "- Sonora Synth patches (the editable synth; shape them further with the synth.* parameters): "
        << patches.joinIntoString(", ") << "\n";
    return out;
}

juce::String kitNames()
{
    juce::StringArray names;
    for (int i = 0; i < numKitVariants; ++i)
        names.add(kitVariantName(i));
    return names.joinIntoString(", ");
}
} // namespace

juce::String agentSystemPrompt()
{
    juce::String prompt;
    prompt << R"(You are the AI producer built into Sonora, a music studio. You do not just describe what to do: you operate the app. Everything a person can do with the mouse, keyboard or MIDI controller you can do with actions: write melodies and beats, arrange whole songs, choose and design instruments, play your own samples, mix, add effects, automate, and control playback. Do the whole request in one reply.

HOW YOU ANSWER
Reply with JSON: "reply" (2 to 4 short, friendly sentences of plain text saying what you did and anything the producer should know) and "actions" (an ordered list; they run top to bottom and land as ONE undo step). If the producer only asks a question, or you genuinely need to ask which of several things they mean, give an empty actions list. Otherwise act: do not ask permission for ordinary requests. Do exactly what was asked and nothing more: never touch tracks, loops or settings the producer did not mention. If part of a request cannot be done, do the rest and say what you skipped.

HOW SONORA IS ORGANIZED
- Up to 8 tracks, numbered 0 to 7. A track is either an instrument ("synth") track or a drum track. The current tracks are listed in the project message; only use track numbers listed there.
- Every track has 4 loops, A to D, each 4 bars long (16 beats, 64 sixteenth steps). Loop view plays the loops each track is showing; Song view plays the arrangement.
- The arrangement is a list of sections (each 4 bars). A section has a part type, one chosen loop (or silence) for every track, and optionally a chord that the instrument tracks transpose to follow.
- add_track puts the new track in the lowest free slot (the project message tells you which). Use that number in later actions of the same reply. A removed track frees its slot for the next add_track.
- If no track number is given, an action uses the track the producer has selected. If no loop is given, it uses the loop that track is showing.

)";
    prompt << "WRITING MUSIC\n" << melodyRulesText() << "\n" << drumRulesText() << R"(
- Write in the song key and scale shown in the project message unless asked otherwise, and follow section chords: chord tones on strong beats, passing tones in small doses.
- "Write N melodies" means N distinct loops (A, B, C, D) for one instrument that belong together: share a motif, then vary it (statement, answer, lift, resolution). Never repeat the same loop under two letters unless asked.
- Make loops that sound like music: rests and rhythmic variety, velocity shaping and accents, melodies that use the instrument's range, basslines anchored on chord roots, drums with ghost notes and fills, sparse intros and fuller choruses. Give each loop its own energy so an arrangement can build.
- Fit the genre and the tempo shown. A full song needs shape: a build into choruses, contrast between sections, a strong last chorus. 8 to 14 sections is typical; at most 16.
- To change what exists (a "simpler" version, a different feel), read the current loop in the project message and write the replacement with write_melody or write_drums, or use transpose_loop, quantize_loop, humanize_loop, velocity_ramp.
- A drum request goes to an existing drum track, or add_track kind drums if there is none. A melodic request goes to an instrument track, or add one.

SOUNDS
Choose sounds with set_instrument (or add_track with instrument). Names are matched loosely, so "alto sax" works.
)" << soundCatalog()
           << "- Sampler: plays one audio file chromatically (set_instrument \"Sampler\", or name a library sample). Library samples are listed in the project message; you cannot import files yourself, so say so if the producer needs a sound that is not there. Shape a sampler with the sampler.* parameters (loop, one-shot, reverse, trim, root key, envelope). Recorded takes can become instruments with take_to_sampler.\n"
           << "- Drum kits: " << kitNames() << " (set_kit).\n"
           << R"(Translating words into sound design (set_param): brighter = raise synth.cutoff or fx.eq.high; darker or warmer = lower synth.cutoff, fx.eq.high, add fx.chorus.mix; wider = fx.chorus.mix, mix.pan, mix.sendReverb; bigger or roomier = mix.sendReverb, fx.reverb.mix, longer synth.release; punchier = fast synth.attack, short synth.decay, fx.comp.ratio; dirtier = fx.drive.amount or synth.drive; softer = longer synth.attack and synth.release; plucky = synth.sustain near 0 with a short synth.decay; wobbly = synth.lfoFilter. Bank instruments (pianos, strings, ...) are fixed recordings: change their character with the fx.* and mix.* parameters, or pick a different instrument. synth.* only affects a track on the Sonora Synth; sampler.* only a track on the Sampler.

ACTIONS
Each action is an object with "op" plus the fields it uses (omit the rest). Fields: track, loop ("A".."D"), toLoop, section (numbered from 0), to, value, value2, text, param, name, kind, instrument, key, scale, part, target, view, arp, rate, chordType, take, on, latch, notes, hits, points, sections.
Project settings
- set_tempo: value = BPM (40 to 240)
- set_key: key (C, C#, D, Eb, ...), scale (Major, Natural minor, Harmonic minor, Dorian, Major pentatonic, Minor pentatonic, Blues)
- set_param: param (a name from PARAMETERS), value (a number), or text for named choices and on/off switches; track for per-track parameters
Tracks and sounds
- add_track: kind ("synth" or "drums"), optional name, optional instrument (instrument tracks only)
- remove_track: track
- rename_track: track, name
- move_track: track, to (swaps the two tracks' positions)
- set_instrument: track, instrument (instrument tracks only)
- set_kit: track, name (drum tracks only)
Patterns
- write_melody: track, loop, notes = [{start, duration, pitch, velocity}] (replaces the loop; an empty list clears it)
- write_drums: track, loop, hits = [{pad, step, velocity}] (replaces the loop)
- clear_loop: track, loop
- copy_loop: track, loop, toLoop
- transpose_loop: track, loop, value = semitones
- quantize_loop: track, loop, value = strength 0 to 1
- humanize_loop: track, loop, value = timing 0 to 1, value2 = velocity 0 to 1
- velocity_ramp: track, loop, value = first velocity, value2 = last velocity
Song
- set_song: sections = [{part, tracks: [{track, loop}], chordKey, chordType}] REPLACES the whole arrangement. A track not listed in a section is silent there. part is Intro, Verse, Pre-Chorus, Chorus, Bridge, Break, Build, Drop or Outro. chordKey and chordType are optional; chordType is Major, Minor, 7, Maj7, Min7, Sus4, Dim or Aug.
- set_cell: section, track, loop, on (false = silent)
- set_part: section, part
- set_chord: section, key (a note name, or "none"), chordType
- add_section (section: the new empty section goes after it; omit for the end), duplicate_section, remove_section, move_section (section, to)
- apply_template: name = simple, pop, edm or hip-hop (arranges the existing loops)
Automation, live play, takes
- set_automation: track, loop, target (Volume, Pan, Send delay, Send reverb, Drive, Delay mix), points = [{tick, value}] with tick 0 to 15360 across the 4-bar loop; clear_automation: track, loop, target (omit target to clear all)
- set_live_fx: track (instrument), arp (Off, Up, Down, Up-down, Random), rate (1/8, 1/16, 1/8T, 1/16T), value = octaves 1 to 3, latch (true/false), chordType (a chord name, or "off"). These shape what the producer plays on the keyboard or MIDI controller.
- set_take: take (id), param (mute, solo, gain 0 to 2, stretch 0.5 to 2), value or on
The app itself
- play (optional section to start from), stop, panic, set_view (view: loop, song, mixer, sound editor, kit editor, automation), select_track, edit_section, save_project, export_audio (opens the export dialog), take_to_sampler (take), delete_take (take)
- record_take: toggles audio recording. Use it ONLY if the producer explicitly asks to record.
- undo, redo, new_project, open_project: only ever as the single action in a reply. new_project and open_project ask the producer to confirm.

PARAMETERS (for set_param; ranges are enforced)
)" << agent::describeParameters();
    return prompt;
}

juce::String agentSchema()
{
    const juce::String loopEnum = R"({"type":"string","enum":["A","B","C","D"]})";
    const juce::String intTrack = R"({"type":"integer","minimum":0,"maximum":7})";
    const juce::String intSection = R"({"type":"integer","minimum":0,"maximum":15})";
    const juce::String str = R"({"type":"string","maxLength":120})";
    const juce::String noteItem =
        R"({"type":"object","additionalProperties":false,"required":["start","duration","pitch","velocity"],"properties":{)"
        R"("start":{"type":"integer","minimum":0,"maximum":15359},"duration":{"type":"integer","minimum":1,"maximum":15360},)"
        R"("pitch":{"type":"integer","minimum":0,"maximum":127},"velocity":{"type":"integer","minimum":1,"maximum":127}}})";
    const juce::String hitItem =
        R"({"type":"object","additionalProperties":false,"required":["pad","step","velocity"],"properties":{)"
        R"("pad":{"type":"integer","minimum":0,"maximum":7},"step":{"type":"integer","minimum":0,"maximum":63},)"
        R"("velocity":{"type":"integer","minimum":1,"maximum":127}}})";
    const juce::String pointItem =
        R"({"type":"object","additionalProperties":false,"required":["tick","value"],"properties":{)"
        R"("tick":{"type":"integer","minimum":0,"maximum":15360},"value":{"type":"number"}}})";
    const juce::String sectionItem =
        R"({"type":"object","additionalProperties":false,"required":["part","tracks"],"properties":{)"
        R"("part":{"type":"string","maxLength":40},)"
        R"("tracks":{"type":"array","maxItems":8,"items":{"type":"object","additionalProperties":false,"required":["track","loop"],"properties":{)"
        R"("track":)" + intTrack + R"(,"loop":{"type":"string","enum":["A","B","C","D","off"]}}}},)"
        R"("chordKey":{"type":"string","maxLength":20},"chordType":{"type":"string","maxLength":20}}})";
    static const char* ops[] {
        "set_tempo", "set_key", "set_param", "add_track", "remove_track", "rename_track", "move_track",
        "set_instrument", "set_kit", "write_melody", "write_drums", "clear_loop", "copy_loop", "transpose_loop",
        "quantize_loop", "humanize_loop", "velocity_ramp", "set_song", "set_cell", "set_part", "set_chord",
        "add_section", "duplicate_section", "remove_section", "move_section", "apply_template", "set_automation",
        "clear_automation", "set_live_fx", "set_take", "play", "stop", "panic", "set_view", "select_track",
        "edit_section", "save_project", "export_audio", "record_take", "take_to_sampler", "delete_take", "undo",
        "redo", "new_project", "open_project" };
    juce::StringArray opNames;
    for (const char* op : ops)
        opNames.add("\"" + juce::String(op) + "\"");
    const juce::String action =
        R"({"type":"object","additionalProperties":false,"required":["op"],"properties":{"op":{"type":"string","enum":[)"
        + opNames.joinIntoString(",") + "]},"
        R"("track":)" + intTrack + R"(,"loop":)" + loopEnum + R"(,"toLoop":)" + loopEnum
        + R"(,"section":)" + intSection + R"(,"to":)" + intSection + R"(,"take":{"type":"integer","minimum":0},)"
        R"("value":{"type":"number"},"value2":{"type":"number"},"on":{"type":"boolean"},"latch":{"type":"boolean"},)"
        R"("text":)" + str + R"(,"param":{"type":"string","maxLength":60},"name":)" + str + R"(,"kind":{"type":"string","maxLength":20},)"
        R"("instrument":)" + str + R"(,"key":{"type":"string","maxLength":20},"scale":{"type":"string","maxLength":40},)"
        R"("part":{"type":"string","maxLength":40},"target":{"type":"string","maxLength":40},"view":{"type":"string","maxLength":40},)"
        R"("arp":{"type":"string","maxLength":20},"rate":{"type":"string","maxLength":20},"chordType":{"type":"string","maxLength":20},)"
        R"("notes":{"type":"array","maxItems":256,"items":)" + noteItem + R"(},)"
        R"("hits":{"type":"array","maxItems":512,"items":)" + hitItem + R"(},)"
        R"("points":{"type":"array","maxItems":64,"items":)" + pointItem + R"(},)"
        R"("sections":{"type":"array","maxItems":16,"items":)" + sectionItem + "}}}";
    return R"({"type":"object","additionalProperties":false,"required":["reply","actions"],"properties":{)"
           R"("reply":{"type":"string","maxLength":900},"actions":{"type":"array","maxItems":)"
        + juce::String(agent::maxActions) + R"(,"items":)" + action + "}}}";
}

namespace
{
// One loop's contents, bounded so a dense project still fits the context.
juce::String describeLoop(const Track& track, int slot, int noteCap)
{
    const auto s = static_cast<std::size_t>(slot);
    if (track.kind == TrackKind::Drums)
        return describeDrums(track.drumPatterns[s]);
    const auto& pattern = track.melodies[s];
    if (pattern.count <= noteCap)
        return describeNotes(pattern);
    Pattern head = pattern;
    std::vector<Note> notes(pattern.notes.begin(), pattern.notes.begin() + pattern.count);
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    head.count = noteCap;
    for (int i = 0; i < noteCap; ++i)
        head.notes[static_cast<std::size_t>(i)] = notes[static_cast<std::size_t>(i)];
    return describeNotes(head) + "    (+" + juce::String(pattern.count - noteCap) + " more notes later in the loop)\n";
}
} // namespace

juce::String buildAgentMessage(const AgentRequest& request)
{
    const auto& project = request.project;
    const auto& song = project.song;
    const int selected = std::clamp(request.selectedTrack, 0, maxTracks - 1);
    juce::String m;
    m << "PROJECT\n";
    m << "Tempo " << juce::String(project.bpm, 0) << " BPM, 4/4. Key: " << keyName(project.musicKey) << " "
      << scaleName(project.musicScale) << ". View: " << (request.songView ? "Song" : "Loop")
      << (request.songView || request.editPart < 0 ? juce::String() : " (editing section " + juce::String(request.editPart + 1) + ")")
      << ". " << (request.playing ? "Playing." : "Stopped.") << "\n";
    if (project.tracks[static_cast<std::size_t>(selected)].kind != TrackKind::None)
        m << "Selected track: " << selected << " \""
          << cleanText(project.tracks[static_cast<std::size_t>(selected)].trackName(), 60) << "\".\n";
    juce::StringArray free;
    for (int t = 0; t < maxTracks; ++t)
        if (project.tracks[static_cast<std::size_t>(t)].kind == TrackKind::None)
            free.add(juce::String(t));
    m << (free.isEmpty() ? juce::String("All 8 track slots are used.")
                         : "The next add_track will be track " + free[0] + " (free slots: " + free.joinIntoString(", ") + ").")
      << "\n";
    m << "Send buses: delay " << juce::String(project.sends.delay.timeMs, 0) << " ms, feedback "
      << juce::String(project.sends.delay.feedback, 2) << ", return " << juce::String(project.sends.delayReturn, 2)
      << "; reverb size " << juce::String(project.sends.reverb.size, 2) << ", return "
      << juce::String(project.sends.reverbReturn, 2) << ". Master limiter ceiling "
      << juce::String(project.master.ceilingDb, 1) << " dB.\n\n";

    juce::String tracks;
    for (int index = 0; index < maxTracks; ++index)
    {
        const auto& track = project.tracks[static_cast<std::size_t>(index)];
        if (track.kind == TrackKind::None)
            continue;
        const bool focus = index == selected;
        tracks << "Track " << index << " \"" << cleanText(track.trackName(), 60) << "\" - "
               << (track.kind == TrackKind::Drums ? "drum track" : "instrument track") << ": "
               << (track.kind == TrackKind::Drums ? juce::String("kit ") + kitVariantName(track.kitVariant)
                                                  : instrumentDescription(track))
               << ". Showing loop " << loopLetter(std::clamp(request.loopSlot[static_cast<std::size_t>(index)], 0, numPatterns - 1))
               << (index == selected ? " (selected)" : "") << ".\n";
        const auto changed = agent::describeChangedParameters(project, index);
        tracks << "  Settings that differ from default: " << (changed.isEmpty() ? juce::String("none") : changed) << "\n";
        if (track.kind == TrackKind::Synth && (track.liveFx.arp != ArpMode::Off || track.liveFx.chordOn))
            tracks << "  Live play: arp " << arpModeName(track.liveFx.arp) << " " << arpRateName(track.liveFx.rate)
                   << (track.liveFx.chordOn ? juce::String(", chord ") + chordName(track.liveFx.chord) : juce::String()) << "\n";
        for (int slot = 0; slot < numPatterns; ++slot)
        {
            const bool empty = !trackSlotHasContent(track, slot);
            tracks << "  Loop " << loopLetter(slot) << " [" << describeLoopRole(track, slot) << "]"
                   << (empty ? " (empty)\n" : ":\n");
            if (!empty)
                tracks << describeLoop(track, slot, focus ? 128 : 48);
        }
    }
    if (tracks.length() > maxContextChars)
        tracks = tracks.substring(0, maxContextChars) + "\n  (context truncated)\n";
    m << "TRACKS\n" << tracks << "\n";

    m << "ARRANGEMENT (" << song.sections << " sections)\n";
    for (int s = 0; s < song.sections; ++s)
    {
        const auto si = static_cast<std::size_t>(s);
        juce::StringArray playing;
        for (int t = 0; t < maxTracks; ++t)
            if (project.tracks[static_cast<std::size_t>(t)].kind != TrackKind::None && song.trackOn[si][static_cast<std::size_t>(t)])
                playing.add("track " + juce::String(t) + " loop " + loopLetter(song.slots[si][static_cast<std::size_t>(t)]));
        const auto chord = song.chords[si];
        m << "  " << s << ". " << songPartName(song.parts[si])
          << (chord.set() ? " [chord " + chordLabel(chord) + "]" : juce::String()) << ": "
          << (playing.isEmpty() ? juce::String("(all silent)") : playing.joinIntoString(", ")) << "\n";
    }
    m << "\n";

    juce::String lanes;
    for (int t = 0; t < maxTracks; ++t)
        for (int slot = 0; slot < numPatterns; ++slot)
            for (int target = 0; target < static_cast<int>(AutomationTarget::numTargets); ++target)
            {
                const auto& lane = project.tracks[static_cast<std::size_t>(t)].automation[static_cast<std::size_t>(slot)]
                                       [static_cast<std::size_t>(target)];
                if (lane.count <= 0)
                    continue;
                juce::StringArray points;
                for (int i = 0; i < lane.count; ++i)
                    points.add(juce::String(lane.points[static_cast<std::size_t>(i)].tick) + ":"
                               + juce::String(lane.points[static_cast<std::size_t>(i)].value, 2));
                lanes << "  track " << t << " loop " << loopLetter(slot) << " "
                      << automationTargetName(static_cast<AutomationTarget>(target)) << ": " << points.joinIntoString(" ") << "\n";
            }
    m << "AUTOMATION\n" << (lanes.isEmpty() ? juce::String("  none\n") : lanes) << "\n";

    juce::String takes;
    for (int i = 0; i < project.takeCount; ++i)
    {
        const auto& take = project.takes[static_cast<std::size_t>(i)];
        takes << "  take " << juce::String(static_cast<juce::int64>(take.id)) << ": starts bar " << (take.startTick / (patternTicks / 4) + 1) << ", "
              << juce::String(take.frames / 48000.0, 1) << " s, " << (take.channels == 2 ? "stereo" : "mono")
              << ", gain " << juce::String(take.gain, 2) << ", stretch " << juce::String(take.stretch, 2)
              << (take.mute ? ", muted" : "") << (take.solo ? ", solo" : "") << "\n";
    }
    m << "RECORDED TAKES\n" << (takes.isEmpty() ? juce::String("  none\n") : takes) << "\n";

    juce::String library;
    int shown = 0;
    for (const auto& name : request.library)
    {
        if (shown++ >= 150)
        {
            library << "  (and " << (static_cast<int>(request.library.size()) - 150) << " more)\n";
            break;
        }
        library << "  " << cleanText(name, 100) << "\n";
    }
    m << "SAMPLE LIBRARY\n" << (library.isEmpty() ? juce::String("  empty (the producer can add sounds in the sampler)\n") : library)
      << "\n";

    const auto first = request.history.size() > static_cast<std::size_t>(maxHistoryTurns)
        ? request.history.size() - static_cast<std::size_t>(maxHistoryTurns) : 0;
    if (first < request.history.size())
    {
        m << "Conversation so far:\n";
        for (auto i = first; i < request.history.size(); ++i)
            m << (request.history[i].fromUser ? "Producer: " : "You: ")
              << cleanText(request.history[i].text, maxTurnChars).replace("\n", " ") << "\n";
        m << "\n";
    }
    m << "The producer says:\n\"\"\"\n" << cleanText(request.message, maxPromptChars) << "\n\"\"\"\n";
    return m;
}

juce::StringArray agentArguments() { return sandboxArguments(agentSystemPrompt(), agentSchema()); }

AgentResult parseAgentResponse(const juce::String& cliStdout)
{
    AgentResult result;
    const auto answer = extractAnswer(cliStdout, result.error);
    if (result.error.isNotEmpty())
        return result;
    const auto* object = answer.getDynamicObject();
    result.reply = cleanText(object->getProperty("reply").toString(), 900);
    result.actions = agent::parseActions(object->getProperty("actions"), result.unreadable);
    if (result.reply.isEmpty() && result.actions.empty())
        result.error = "Claude sent an empty reply.";
    return result;
}

AgentResult runAgent(const AgentRequest& request, const std::atomic<bool>* cancel, int timeoutMs)
{
    AgentResult result;
    const auto output = runSandboxedClaude(agentArguments(), buildAgentMessage(request), cancel, timeoutMs, result.error);
    if (result.error.isNotEmpty())
        return result;
    return parseAgentResponse(output);
}
} // namespace sonora::ai
