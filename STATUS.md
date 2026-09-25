# Handoff status — 0.15.0

## Completed this milestone

- Persisted audio/MIDI setup (`~/.config/sonora/audio.xml`, XDG-aware):
  device, sample rate, buffer size, and enabled MIDI inputs restore on launch,
  save on every setup change, and save once more on clean exit.
- XRUN counter in the status bar alongside CPU load and MIDI devices.
- Device-error handling: a dead device parks the transport (finalizing any
  take first), silences stuck notes, and turns the status bar into a red
  recovery prompt; the next successful setup clears it.
- Headless end-to-end acceptance test: builds a reference song (two melody
  variations, two drum variations, four alternating sections, track FX, mixed
  levels, limiter, and a sung take), saves/loads the project file, bounces
  loop and song, and asserts durations, audibility, limiter ceiling, sample
  validity, vocal presence in the mix, and WAV headers.

## Verified

- `ctest` (Debug and Release): all 27 suites pass, zero warnings.
- Live on this machine: `audio.xml` written with the ALSA setup plus the
  auto-enabled Minilab3 MIDI/MCU/ALV inputs (DIN THRU excluded); status bar
  shows the MIDI summary; startup clean, no assertions.
- Reference song: song bounce longer than 3x the loop, both audible and
  unclipped, vocal energy confirmed in its punch-in window.

## Debugging notes (do not regress)

- `AudioAppComponent` is not an `AudioIODeviceCallback`; dead-device errors
  arrive via the callback interface, so the extra device callback doubles as
  the error sink. Errors arrive on the audio thread: always hop to the
  message thread before touching transport, takes, or UI.
- `AudioDeviceManager` is a ChangeBroadcaster: setup changes (including error
  recovery) arrive as change callbacks on the message thread — ideal for
  persisting settings and clearing error banners.
- `getXRunCount()` is cheap and noexcept; polling it in the slow status tick
  costs nothing.

## Still to verify interactively (needs hardware hands)

- Unplug the audio device mid-playback: transport parks, banner shows,
  replug + reselect recovers.
- Under-load XRUN counter climbs; buffer-size raise settles it.
- A restored setup across a real restart (device names stable on this machine).
- The full human acceptance pass: make an actual song start to finish.

## Next step

Human acceptance on Omarchy hardware, then AUR submission (needs a git
remote + a full `makepkg -si` host). The application itself is
feature-complete per the original plan.

Build/run instructions, interactions, and realtime limitations are in README.md.
