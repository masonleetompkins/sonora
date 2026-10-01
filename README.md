# Sonora

A native Linux synthesizer and DAW in development. Version 0.19.0 uses C++20,
JUCE 8.0.6, and CMake. Licensed GPL-3.0-only (see LICENSE); JUCE retains its
own license. Sampled instruments use the bundled GeneralUser GS 2.0.3 bank by
S. Christian Collins (free for private and commercial music; see
`assets/GeneralUser-GS-LICENSE.txt`) played by TinySoundFont (MIT). See
`third_party/README.md` for pinned sources.

Source and issue tracker: https://github.com/masonleetompkins/sonora

## Install on Arch / Omarchy

The packaged route builds `sonora-git` from source:

```sh
cd packaging/arch
makepkg -si
sonora
```

This installs `/usr/bin/Sonora`, a desktop launcher, the icon, AppStream
metadata, and the license. A git remote is required for redistribution; until
one exists, the PKGBUILD builds from a local clone (see the `_gitremote`
comment at its top). Full `makepkg` validation (a from-scratch network build)
is still pending on a machine with `cmake` and `ninja` installed.

## Build from source

On Arch, install `base-devel cmake ninja git alsa-lib freetype2 fontconfig libx11
libxext libxinerama libxrandr libxcursor` and a JACK provider (Omarchy normally
uses `pipewire-jack`). The first configuration downloads JUCE from GitHub.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/Sonora_artefacts/Debug/Sonora
```

For daily use, build the optimized Release binary instead of Debug. It is
roughly 7x smaller, runs the test suite about 3x faster, and renders the UI
with far less CPU. The UI also avoids full editor repaints: the playhead only
redraws its own strip, idle frames do no work, and transport/meter/section
indicators update only when their values change.

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel 12
ctest --test-dir build-release --output-on-failure
./build-release/Sonora_artefacts/Release/Sonora
```

## Audio setup (PipeWire / JACK / ALSA)

Sonora targets Omarchy's default stack: **PipeWire with its JACK
compatibility layer**. In **Audio / MIDI**, prefer the JACK backend for
low-latency routing through the desktop audio graph; use the ALSA backend as
a fallback or for direct hardware access.

- If JACK is not discoverable in settings, try launching through `pw-jack`,
  or confirm `pipewire-jack` is installed (it provides the JACK libraries
  PipeWire implements).
- Enable MIDI inputs in the same dialog; USB controllers auto-enable on
  plug-in and appear in the status bar, with a ● dot while messages arrive.
- For recording, select an input-capable device (built-in mic, USB interface)
  and verify the input meter moves in **03 Audio** before pressing REC.
  Sonora opens audio inputs lazily — plain playback never touches input
  hardware, so Bluetooth headset mics (hearing aids) stay asleep in their
  high-quality output profile until you actually record or monitor. Inputs
  open automatically when you visit the Audio tab, enable monitoring, or
  press REC; the audio view names the active input device.
- If you hear dropouts, raise the buffer size in Audio / MIDI; recording
  overruns are reported rather than silently dropped.
- Audio/MIDI setup (device, sample rate, buffer size, enabled MIDI inputs)
  persists in `~/.config/sonora/audio.xml` and restores on launch.
- The status bar reports CPU load, dropout (XRUN) count, and connected MIDI
  controllers. If a device fails mid-session, the transport parks safely and
  the status bar says how to recover.

## Current features

- Futuristic studio-console UI: transport strip, channel rack, matrix editor, and performance dock.
- Follows your Omarchy theme: reads the active theme's `colors.toml` on
  startup and repaints live within seconds when you run `omarchy theme set`.
  Backgrounds, text, accents, and track hues all track the theme; without
  Omarchy present it keeps the built-in neon look. The sun/moon button in the
  header forces the built-in Light (Daylight Paper) or Dark mode instead
  (Shift-click returns to the system theme); text also scales with your
  monitor scaling, overridable with `SONORA_UI_SCALE=1.5`.
- Cyan/violet track identity, custom buttons/rotary control, and animated transport/playhead/pad cues.
- Resizable dark native UI; Ctrl+1/Ctrl+2 switch melody/drums.
- MiniLab 3 knobs 1-8 control the selected instrument's sound, mapped per
  instrument family (e.g. guitar: Distortion, Tone, Mids, Compress, Chorus,
  Delay, Echoes, Reverb; synth: Cutoff, Resonance, Attack, Release, Drive,
  Chorus, Delay, Reverb). Works in the Arturia/User program (CC 74, 71, 76,
  77, 93, 18, 19, 16) and DAW mode (CC 86, 87, 89, 90, 110, 111, 116, 117).
  The performance strip shows the current map and values; drag the strips
  on screen with the mouse or turn the hardware knobs — each gesture is
  one undo step and is saved with the project.
- MiniLab 3 screen and pads: switch the MiniLab to its DAW program
  (Shift + Pad 3) and its screen shows the selected track and instrument,
  then the knob name and value while you turn (e.g. DISTORTION / 42%); pads
  light in the track colour. Sonora performs Arturia's DAW handshake itself;
  in the regular program it just shows a tip and the MiniLab works as before.
- Song view (LOOP | SONG switch at the top): an arrangement board with named,
  colour-coded parts across the top (Intro, Verse, Pre-Chorus, Chorus,
  Bridge, Break, Build, Drop, Outro) and one row per track. Drag a track's
  loop chips (A-D) onto parts, click cells on/off, drag across to paint,
  scroll or right-click to pick a loop, drag part headers to reorder, and
  right-click a part to name, duplicate, insert, or delete it. Click a part to
  play the song from there; the playhead follows along. Song structure
  templates (Simple, Pop, EDM, Hip-hop) build a full song from your loops in
  one step. Up to 16 parts (64 bars).
- "Working on" part selector in Loop view: the loop plays exactly that part
  (each track's loop for it, silent tracks muted) and pattern tabs choose
  which loop the track uses there.
- Per-track Drive (overdrive with tone control) and stereo Chorus effects, in
  a Drive -> EQ -> Compressor -> Chorus -> Delay -> Reverb chain.
- Groove per track: non-destructive swing on playback plus undoable quantize
  and seeded humanize edits, from the Groove button by the pattern tabs.
- Song key and scale (Key button): new notes snap to key, out-of-key rows
  dim in the piano roll, one-finger chord stamps (Chord button), velocity
  ramps (Ramp button), and the AI writes in key too.
- Live arp and chords (Arp button): the MiniLab plays an arpeggiator
  (up/down/up-down/random, triplets, 1-3 octaves, latch) and one-finger
  chords on the selected synth track, in time with the song tempo.
- Chord track: every song section can carry a chord (click the chord strip
  under a part in Song view). Chorded sections transpose synth loops to
  follow, and the AI composer writes new loops to fit the progression.
- Mixer (MIX button): per-track faders, pan, mute/solo, and post-fader
  delay/reverb sends into two shared return buses, all live during playback.
- Automation (AUTO button): per-loop curves for volume, pan, both sends,
  drive, and delay mix, drawn under the editor and followed in loop and
  song playback.
- Take time-stretch: the Stretch slider under a selected take changes its
  length without changing its pitch (Rubber Band), from half to double
  speed. Stretched takes play, export, and tune like any other take.
- AI agent sidebar (AI assistant button or Ctrl+I): one conversation with
  Claude that can operate the whole app, whatever view is showing. It sees
  every track, loop, setting, the arrangement, automation, takes, and your
  sample library, and answers with actions that Sonora applies as ONE undo
  step. It can write melodies and beats (for example "write four melodies on
  this track"), add, rename, reorder and remove tracks, pick or design
  instruments (any of 84 instruments, 26 synth patches, any of your
  samples, every synth and sampler knob), set the tempo and key, mix
  (faders, pan, sends, returns, master), add and tune effects, draw
  automation, set live arp and chords, arrange the entire song (sections,
  parts, chords, templates), edit takes, and control the app (play, stop,
  switch views, save, export). Asking "undo that" works too. It reads
  parameters from the same table the app uses, so every value is range
  checked; bad or unknown actions are skipped and listed, never half-applied.
  Docks beside the editor on wide windows and floats over it on narrow ones.
  See "AI melody and privacy" below.
- **Idea REC**, separate from normal audio-take REC: on a selected instrument
  or drum track, press Idea REC or MiniLab 3 Shift+Record (Shift+Pad 7), play
  an idea, then press Idea REC or Shift+Stop (Shift+Pad 5). Keys and pads are
  captured without starting song playback or creating an audio take; on a
  synth track you can also hum a single-note melody into an available mic.
  Sonora quantizes/fits the phrase to four bars and asks the local Claude
  assistant to develop it with the other tracks in mind. If Claude is
  unavailable, the quantized idea is still saved to the selected loop.
  Ctrl+Z restores its previous contents. On-screen REC continues to record
  regular song audio takes; MiniLab's Shift+Record is reserved for ideas.
- Editable synth (Sine Keys, "Edit sound"): two oscillators (sine/triangle/
  saw/square/pulse/noise, blend, semitone, fine), resonant low-pass filter
  with its own ADSR, amp ADSR, LFO vibrato/filter wobble, drive, stereo
  chorus, output level, a live waveform preview, and 26 factory patches
  (pads, leads, basses, plucks, brass, bells, keys, organ, chip, string
  machine, riser, wind and noise hits). Edits play instantly on held notes
  and are undoable; synth, FX, and volume tweaks never cut sounding notes.
- Sampler: any sound can be an instrument. Choose "Sampler" (or any sound in
  your library) for an instrument track, then play it chromatically on the
  keys, the piano roll, the arp, and the AI. Add sounds with "Add sounds..."
  (WAV, AIFF, FLAC, OGG, MP3, or a whole folder): they go into one library
  (`~/.local/share/sonora/samples`) that every project can use, and projects
  copy the sounds they use into their media folder on save. The editor shows
  the waveform with draggable trim and loop markers, plus root key (with
  "Detect root" from the sample's pitch), tune, loop, one-shot, reverse, key
  tracking on/off, and an amp envelope. Any recorded take becomes an
  instrument with "Play as instrument" on the Audio tab. Sampler sounds are
  included in exports.
- Instrument picker with search and favorites: the instrument button opens a
  searchable list of every sound (sampled instruments, synth patches, your
  library) grouped by family. Type to filter (every word must match), use
  Up/Down and Enter, click a star to keep favorites at the top. Favorites
  are saved in your config folder.
- Per-track instrument selector, grouped by family: the editable synth plus
  84 sampled instruments from the bundled GM bank. Piano (grand, bright,
  electric, honky-tonk, FM, harpsichord, clavinet), mallets (celesta, bells,
  vibes, marimba, xylophone, steel drums), organ and accordion, guitars
  (nylon, steel, jazz, clean, muted, overdriven, distortion), basses
  (acoustic, fingered, picked, fretless, slap, synth), strings (ensemble,
  violin, viola, cello, contrabass, tremolo, pizzicato, harp, synth),
  choir and voices, brass (trumpet, muted, trombone, horn, tuba, section,
  analog), woodwinds (four saxes, oboe, clarinet, bassoon, flute, piccolo,
  recorder, pan flute), synth leads and pads, world (harmonica, sitar,
  banjo, koto, kalimba, fiddle), and percussion (timpani, woodblock, taiko).
  Notes stay put when you switch; TRACK effects shape the chosen sound, and
  exports render the same instrument.
- On-screen/computer keyboard and external MIDI input.
- Output selection, sample-rate/buffer controls, CPU status, and panic button.
- Four-bar, 4/4 looping transport with 40-240 BPM tempo control and playhead.
- Two-octave piano roll (MIDI 48-71), sixteenth-note grid, up to 256 notes.
- Note drawing, movement, resizing, deletion, and velocity editing.
- Independent drum track with eight sampled pads and a 64-step/four-bar grid.
- Starter drum kit generated locally, velocity-sensitive hits, and hi-hat choking.
- Drum paint/erase gestures, velocity editing, pad audition, and first-bar repeat.
- Per-track volume, mute, and solo, plus a basic output peak/clip readout.
- Song arrangement: 1-8 four-bar sections with per-section melody/drum switches.
- Pattern library: 4 melody slots (A-D) and 4 drum slots per project, with tab
  switching, DuP duplication into the next slot, and per-section slot assignment
  (click a section block to toggle it, Shift-click to cycle its pattern).
- Shared instances: sections showing the same slot letter play the same pattern,
  so one edit updates them all. **Ctrl-click** a section to make it unique
  (copies its pattern into a free slot and jumps the editor there); section
  tooltips report exactly which sections share each slot.
- LOOP mode previews the editor-selected pattern slot; SONG mode plays the
  section arrangement in order, then stops.
- Custom drum samples: replace any of the 8 starter pads with your own
  WAV/AIFF/FLAC/MP3/OGG (up to 10 seconds) from the Kit panel. Files are mixed
  to mono, resampled, level-matched, and gathered into the project media folder
  on save; missing files fall back to the starter sound.
- Kit presets: six factory kits (Starter, Deep, Crisp, Tight, Boom, Warm —
  re-voiced generator recipes) plus your own named presets saved under
  `~/.local/share/sonora/kits/`. A preset stores the variant and its samples
  as a portable folder; loading one is undoable, and per-pad imports still
  override individual pads afterwards.
- WAV export: bounce the loop or full song to 16/24-bit stereo at 44.1/48/96 kHz,
  with optional normalization, ring-out tail, background rendering with progress,
  and a peak/clipping report. Exported audio matches live playback sample-for-sample.
- Insert FX per track (3-band EQ, compressor, tempo-style delay, reverb) plus a
  master brickwall limiter with gain-reduction readout. All realtime-safe, saved
  per project, and shared by live playback and export. Defaults are transparent,
  so older projects sound the same except for the louder master stage (the fixed
  -3.7 dB headroom pad was replaced by the limiter).
- Vocal/instrument recording: stereo inputs, Input 1/2/Stereo source select,
  software monitoring, live input meter, punch-in/out, up to 8 takes with mute,
  waveform inspection, and automatic input-latency compensation. Takes play in
  SONG mode and are included in exports. The Audio tab shows every take as a
  waveform lane: Solo auditions lanes while the song plays (winning over
  mute), and Keep mutes everything but one take as the comp choice.
- Vocal pitch correction: offline monophonic analysis (YIN detector), key/scale
  snapping (chromatic/major/minor), correction amount and retune speed, detected
  vs. target pitch display over the waveform, and nondestructive tuned-take
  rendering through PSOLA resynthesis.
- Starter melody/beat, selected-pattern clearing, and 100-step undo/redo history.
- Versioned JSON project save/open with validation and temporary-file replacement.
- Dirty-state confirmation before closing or replacing a project.
- Recovery snapshots approximately every two seconds after completed edits.

### Quick start

1. Click **Demo melody** to add a melody and bass line.
2. Select **02 Starter drums**, then **Demo beat**.
3. Press **Play**: both tracks loop together, regardless of which editor is visible.
4. Build variations: switch to pattern slot **B** with the A-D tabs, press
   **Dup** to copy slot A across, edit it into something new, then assign slots
   to sections with Shift-click on the section blocks and hear the song in
   **SONG** mode. Sections sharing a letter stay in sync; **Ctrl-click** a
   section to detach it into its own copy first.
5. Make the drums yours: open **Kit** in the drum editor, pick a factory kit
   from the preset menu or **Load** a sample onto any pad (**Play** previews
   it, **Clear** restores the starter). Name your creation in the preset box
   and press **Save** to keep it; save the project to collect the files into
   its media folder.
6. Adjust each track's volume, mute, or solo beside the track tabs.
7. Set tempo, then save the combined project with **Save** or Ctrl+S.
8. Press **Export** (or Ctrl+E) to bounce to WAV: pick loop or song range,
   sample rate, bit depth, normalization, and tail, then **Bounce** and choose
   a file. Rendering runs in the background with a progress bar; the completion
   report shows duration, peak level, and whether anything clipped.
9. Shape each track in the **INSERT FX** strip: pick MEL/DRM/MST, then
   EQ/CMP/DLY/VRB (master shows the limiter), and turn the knobs. Every effect
   has an ON/OFF bypass; all settings save with the project and undo with it.
10. Record vocals: open **03 Audio**, pick the input source, enable **Monitor**
   to hear yourself, and press **REC** (recording implies SONG mode so takes
   stay audible). Press REC again to punch out or Stop to finish; takes appear
   in the list with mute, delete, and waveform inspection. Recording requires
   an input-capable device selected in Audio / MIDI.
11. Fix vocal pitch: record or select a take (analysis starts automatically),
   pick a key and scale, set the correction amount and retune speed, watch the
   detected (cyan) vs. target (violet) curves, then **Tune take** to render a
   corrected copy alongside the original. Consonants and silence pass through
   untouched; small corrections sound most natural.

### MIDI controllers (MiniLab 3 ready)

Plug in any class-compliant USB MIDI controller — inputs enable automatically,
including hot-plug while the app runs. Connected devices appear in the status
bar. An Arturia MiniLab 3 works out of the box:

- Keys play the synth (velocity sensitive) on any channel except 10.
- Pads (channel 10, both banks) trigger the 8 drum voices in pad order.
- Pitch strip bends ±2 semitones; mod strip adds vibrato; a sustain/expression
  pedal on the control input holds notes (CC64, handled end to end).
- The MCU port's transport pads drive the app: play, stop, record, and
  loop/song toggle. Other MCU traffic is swallowed so faders never play notes.
- THRU ports are never auto-enabled, so external DIN gear can't double-trigger.

Generic controllers behave the same way, minus the Arturia pad ordering and
MCU transport (channel-10 drums follow the standard drum-note list instead).

**Melody:** click an empty piano-roll cell and drag right to draw a note. Drag a
note to move it; drag its right edge or Shift-drag to resize. Right-click deletes.
Scroll over a note to change its velocity.

**Drums:** click a cell to toggle it, or drag to paint/erase multiple cells in one
undo step. Right-drag always erases. Scroll over an active hit to change velocity.
**Repeat bar 1** copies the first drum bar into bars 2-4. Click a row name or a
bottom pad to audition it, even while the transport is stopped.

Space toggles playback when the editor has focus. Ctrl+Z and Ctrl+Shift+Z undo
and redo; Ctrl+O opens, Ctrl+N creates a project, and Ctrl+Shift+S saves as.
Computer-keyboard playing uses JUCE's default mapping when the keyboard has focus.
In the drum editor, Q/W/E/R/A/S/D/F audition kick/snare/closed hat/open hat/clap/
low tom/rim/shaker respectively. Audition respects track mute, solo, and volume.

External MIDI channel 10 plays drums using notes 36, 38, 42, 46, 39, 45, 37, and
82 in that pad order. Other MIDI channels play the synth.

Playback starts from bar one. Tempo changes preserve the musical playhead position.
Melody edits during playback reconstruct held notes at the next block; drum-grid
edits leave existing sample tails intact. Track volume changes are smoothed.
Same-pitch melody overlaps are disallowed. The current workspace has two fixed
tracks sharing one four-bar loop. Arbitrary tracks and arrangement are next.

The output stage reserves fixed headroom, but is not a limiter: turn down track
volumes if the output readout indicates clipping. The starter kit is built from
immutable sample buffers generated at startup, not external sample files; custom
sample import and kit browsing are still to come. See `ASSETS.md` for provenance.

### Project files and recovery

Save projects as `*.sonora.json`. Format v10 adds each synth track's
instrument preset, v11 adds each track's synth patch, and v12 adds per-track
drive and chorus, v13 adds named song parts and 16-part songs, and v14 adds
per-track swing; older projects open
with Sine Keys and the default sine patch. Format v15 adds the song-wide
key and scale (older projects open in C major), and v16 adds per-track
live arp/chord settings (older projects play MiniLab notes straight
through), v17 adds per-take solos (older projects open unsoloed),
and v18 adds the per-section chord track (older projects play as written).
Format v19 adds per-track pan/sends and the return buses (older projects
open centered and dry), v20 adds automation lanes (older projects
follow the knob values), v21 adds per-take stretch (older projects
play at speed), and v22 adds each track's sampler settings and sound name
(older projects open with an empty sampler). It contains tempo, timing metadata, two
stable track IDs/instrument identifiers, track mix settings, per-track effect
chains, master limiter settings, four melody patterns and four drum patterns
per track, per-pad custom sample filenames, the factory kit variant, audio take
metadata, transport mode, and the section arrangement (including per-section
pattern slots). Take audio lives in a
`<project>.media/` sidecar folder (gathered automatically on save); loading
validates the whole document before replacing the current project, and takes
whose files are missing load as offline placeholders. Older formats migrate
automatically (pre-v6 projects play slot A everywhere); subsequent saves use v6.

Recovery uses `$XDG_STATE_HOME/sonora/recovery.sonora.json`, or
`~/.local/state/sonora/recovery.sonora.json`. On startup, an existing snapshot can
be recovered as an unsaved project. Saving or deliberately discarding the project
clears the snapshot. Recovery does not replace normal project saves. Undo history
and audio-device settings are session-only.

### Local build tools

This machine's initial build used temporary, isolated CMake/Ninja packages:

```sh
/tmp/opencode/sonora-build-tools/bin/cmake --build build --parallel 2
/tmp/opencode/sonora-build-tools/bin/ctest --test-dir build --output-on-failure
```

The build cache references that temporary Ninja executable. After installing
system build tools, configure a fresh build directory or update
`CMAKE_MAKE_PROGRAM` to the system Ninja path.

## Architecture

- `Pattern.h`: fixed-capacity musical data and validation; 960 ticks per quarter note.
- `LoopScheduler.h`: callback-size-independent tick-to-sample scheduling and note chase.
- `SnapshotQueue.h`: bounded SPSC transfer of value snapshots from UI to audio thread.
- `NeonTheme.h`: futuristic JUCE styling, custom buttons/rotary control, shared palette/typography.
- `AudioEngine.*`: instrument ownership, live MIDI routing, shared transport, and mixing.
- `PianoRoll.*`: editing gestures and timeline display.
- `DrumSampler.*`: deterministic starter samples, fixed voice pool, resampling, and hat choke.
- `DrumSequencer.*`: step editing, velocity display, and pad audition.
- `ProjectIO.*`: versioned JSON and replacement saves.
- `Export.*`: offline bounce through the live engine graph, WAV writing.
- `Fx.*`: realtime-safe EQ/compressor/delay/reverb/limiter and track chains.
- `AudioTakes.*`: take metadata, background FIFO recorder, RCU take sets, loading.
- `PitchCorrect.*`: YIN detection, scale quantization, PSOLA pitch shifting.
- `MidiHardware.h`: Arturia pad map, Mackie transport notes, auto-connect logic.
- `KitSamples.*`: sample-bank loading (decode/mono/resample/normalize),
  factory variants, and user preset files.
- `OmarchyTheme.h`: active-theme discovery, colors.toml parsing, palette mapping.
- `MainComponent.*`: workspace, transport controls, history, file dialogs, recovery.
- `tests/CoreTests.cpp`: headless scheduler, persistence, concurrency, and actual DSP tests.

Live melodic MIDI is normalized to channel 1; the melody pattern uses channel 2
to avoid live note-offs cancelling sequenced notes. Channel 10 routes to the drum
sampler. Snapshot publication and UI drum auditions use bounded queues. Project
snapshots are retried by the UI if the queue fills. Sample generation and project
file I/O run outside the audio callback. Drum sampling uses linear interpolation
and a fixed 64-voice pool, with oldest-voice replacement if the pool is exhausted.

## Next milestones

1. Pattern instances at bar positions with make-unique (sections currently
   share whole 4-bar library patterns).
3. Effects, mixer, automation, and offline WAV export.
4. Worker-thread audio recording, nondestructive editing, and latency compensation.
5. Offline monophonic vocal analysis and pitch correction.
6. Arch packaging and end-to-end Omarchy hardware/desktop validation.

Realtime hardening: this bootstrap uses JUCE Synthesiser, MidiKeyboardState, and
MidiMessageCollector, which have internal synchronization. The MIDI buffer is
preallocated but not a bounded event queue. Replace/audit these paths before
claiming strict lock-free or allocation-free callback behavior. Timeline scheduling
and project snapshot transfer themselves require no locks or dynamic allocation.
Synth voice stealing is disabled until click-free stealing is implemented. Pitch
bend and sustain are not implemented yet. Melody edits and transport/mute changes
can still silence/reconstruct voices abruptly; seamless reconciliation is future work.

## AI melody and privacy

The AI assistant runs through the Claude Code CLI already installed and signed in on
your computer, so it uses your Claude subscription. Sonora never asks for,
reads, stores, or transmits credentials. Each request:

- runs `claude` as a child process with a fixed argument list (no shell) and
  sends the request on stdin;
- disables every Claude Code tool, MCP server, user/project setting, and hook,
  keeps no session history, and runs in an empty temporary folder, so Claude
  can only reply with text;
- removes `ANTHROPIC_API_KEY` and `ANTHROPIC_AUTH_TOKEN` from the child's
  environment so a stray key is never billed or exposed;
- shares only project text: tempo, key, track names and settings, the notes
  and drum steps in each loop, the arrangement, automation, take lengths,
  the names of files in your sample library, and the recent conversation.
  It never receives audio, file contents, or file paths;
- treats the reply as untrusted data: schema-constrained, then every action
  is parsed, clamped to the app's own ranges, and validated before it
  touches the project (pitches folded into range, overlaps trimmed, counts
  capped, parameters limited to their documented ranges). The whole reply
  lands as one undo step, and a result that would not validate is rolled
  back entirely;
- cannot import or read files, run commands, or reach the network. The only
  app-level requests it can make (play, stop, save, export, record, new or
  open project) go through the same buttons and confirmation prompts you
  use; "new project" and "open" always ask before discarding unsaved work.

Sonora looks for `claude` via `SONORA_CLAUDE_PATH` (absolute path), then
absolute `PATH` entries, then common per-user install locations.
