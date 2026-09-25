# Handoff status — 0.14.0

## Completed this milestone

- Added Arch packaging: `sonora-git` PKGBUILD (Release build, test stage,
  explicit file installs), desktop entry, SVG icon, AppStream metainfo, and
  the GPL-3.0-only license text.
- CMake installs the binary, launcher, icon, metadata, and docs. The PKGBUILD
  deliberately avoids `cmake --install` because JUCE offers no install toggle
  and would stage all of its own headers into the package.
- Initialized git version control for the project (v0.13.0 tagged) so the
  `-git` package has a real source; build trees stay ignored.
- README now documents packaged install, source builds, and a full PipeWire /
  JACK / ALSA setup and troubleshooting section.

## Verified

- `ctest` (Debug and Release): all suites pass, zero warnings (no code changed
  this round beyond the version bump).
- `cmake --install` to a test prefix: binary, desktop file, icon, metainfo,
  license, and README land in the right FHS locations.
- `desktop-file-validate`: clean.
- `makepkg --printsrcinfo`: metadata parses (deps, license, provides).
- `package()` file layout simulated against the real Release binary: exact
  five install targets verified.

## Known packaging gaps (need a full build host)

- A from-scratch `makepkg -si` was not run here: this machine lacks system
  `cmake`/`ninja` (builds use an isolated venv) and sudo for makedeps.
- No git remote exists yet; the PKGBUILD builds from a local clone path with
  a one-line `_gitremote` switch documented at its top. Set a remote, push,
  and point it at the network URL before AUR submission.
- `namcap` and AppStream validation (`appstreamcli validate`) were not
  available; run both before submitting anywhere.

## Next implementation step

The end-to-end acceptance pass from the original plan: build the reference
song (drums, bass, melody, arrangement, vocal, pitch, mix, export) on Omarchy
hardware, then remaining hardening (device-loss behavior, xrun reporting,
persisted audio settings).

Build/run instructions, interactions, and realtime limitations are in README.md.
