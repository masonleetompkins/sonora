# Handoff status — 0.16.0

## Completed this milestone

- The UI now follows Omarchy themes: on startup and every few seconds it reads
  the active theme (`theme.name` → `themes/<name>/colors.toml`, falling back to
  the live `current/theme` copy) and repaints the whole studio when anything
  changes. Try `omarchy theme set <other>` while Sonora runs.
- Mapping: theme background/foreground become app bg/text; panel/raised/border
  derive by mixing so dark and light themes both stay readable; melody, drums,
  audio, danger, and warn follow the theme's cyan/magenta/blue/red/orange.
  Missing keys or a missing Omarchy install fall back to the neon defaults.
- Implementation: new juce_core-only `OmarchyTheme.h` (discovery, flat-TOML
  parse, hex colors, mapping, fingerprint) plus a runtime palette in
  `NeonTheme.h` with a LookAndFeel re-sync. No project-format change.

## Verified

- `ctest` (Debug and Release): all 28 suites pass, zero warnings.
- Hex parsing (6/3-digit, fallbacks), TOML edge cases (comments, sections,
  quotes, bare values), mason-like mapping, per-key fallbacks, light-mode flag.
- Live `loadOmarchyPalette()` runs against this machine's real mason theme.

## Debugging notes (do not regress)

- TOML values starting with `#` are hex colors, not comments; only strip `#`
  comments that follow other content.
- Nested-component method bodies must sit below the nested struct definitions
  in MainComponent.cpp, or the build fails on incomplete types.
- Sonora is single-instance: a second launch exits 0 silently while one copy
  runs. Headless smoke tests must check `pgrep` first (and never kill a live
  user session to test).

## Still to verify interactively

- Relaunch the app and confirm the mason look (pure-black bg, theme cyan
  accents, blue drums); then `omarchy theme set` something else and watch it
  follow live within seconds.
- Light-mode theme readability (no light Omarchy theme installed here).

## Next step

Human acceptance on Omarchy hardware, then AUR submission (needs a git
remote + a full `makepkg -si` host).

Build/run instructions, interactions, and realtime limitations are in README.md.
