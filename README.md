# Sonora

A native Linux synthesizer and DAW in development. Version 0.14.0 uses C++20,
JUCE 8.0.6, and CMake. Licensed GPL-3.0-only (see LICENSE); JUCE retains its
own license. No third-party sound assets are bundled.

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
  plug-in and appear in the status bar.
- For recording, select an input-capable device (built-in mic, USB interface)
  and verify the input meter moves in **03 Audio** before pressing REC.
- If you hear dropouts, raise the buffer size in Audio / MIDI; recording
  overruns are reported rather than silently dropped.
- Audio settings are currently session-only (not persisted between launches).

## Current features

- Futuristic studio-console UI: transport strip, channel rack, matrix editor, and performance dock.
- Cyan/violet track identity, custom buttons/rotary control, and animated transport/playhead/pad cues.
- Resizable dark native UI; Ctrl+1/Ctrl+2 switch melody/drums.
- 16-voice sine instrument with velocity and ADSR envelope.
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
- Kit presets: three factory kits (Starter, Deep, Crisp — re-voiced generator
  recipes) plus your own named presets saved under
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
  SONG mode and are included in exports.
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

Save projects as `*.sonora.json`. Format v6 contains tempo, timing metadata, two
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
