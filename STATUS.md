# Handoff status — 0.17.0

## Completed this milestone

- Fixed the hearing-aid grab: Sonora no longer requests audio inputs at
  startup (output-only by default), so Bluetooth headset mics stay asleep in
  their high-quality output profile. Inputs open lazily when the user opens
  the Audio tab, enables monitoring, or presses REC — and a restored setup
  keeps previously opted-in inputs. This also removes the startup device
  renegotiation that was stalling MiniLab connection.
- MIDI activity dot (●) in the status bar, live for 2 s after any message, so
  controller connection is verifiable at a glance.
- Audio view names the active input device and explains inputs open on demand.

## Verified

- `ctest` (Debug and Release): all 25 suites pass, zero warnings, no code-path
  changes to DSP (UI/device layer only).
- Could not smoke-launch: the owner's live session holds the single-instance
  lock (correctly left undisturbed). Visual check of the new strings deferred
  to the next restart.

## Debugging notes (do not regress)

- Never request input channels speculatively on Linux: it activates headset
  mics and forces system-wide Bluetooth profile switches.
- `AudioAppComponent` restores nothing; explicit `initialise()` with saved XML
  plus `setAudioChannels()` to match is the correct startup sequence.
- Single-instance guard exits 0 silently — smoke scripts must check `pgrep`
  first and must never kill a live user session.

## Still to verify interactively

- Restart with hearing aids connected: no mic activation during playback;
  inputs open on REC with the right device.
- MiniLab connects promptly on a fresh launch now that startup is output-only.
- MIDI ● flashes on key/pad hits.

Build/run instructions, interactions, and realtime limitations are in README.md.
