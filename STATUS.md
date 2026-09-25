# Handoff status — 0.13.0

## Completed this milestone

- Added pattern instances: sections sharing a slot letter play one pattern, so
  one edit updates them all; Ctrl-click detaches a section into a free slot
  copy (make-unique) and jumps the editor there for immediate editing.
- Sharing tooltips on every section block report exactly which sections share
  each slot ("shared by sections 1, 3" vs. "unique to this section").
- Refusals explain themselves (already unique, or every slot in use); refused
  detaches provably mutate nothing.
- No project-format change (v8 still): only slot indices move, so all existing
  v8 files, saves, and migrations work untouched.

## Verified

- `ctest` (Debug and Release): all 26 suites pass, zero warnings.
- Detach copies content and repoints; second detach refuses; out-of-range
  refuses; detached copies edit independently of sources.
- True full-library refusal (shared section, zero free slots) for melody and
  drums, with library immutability on refusal.
- Sharing queries drive the tooltip indicators.
- Screenshot-verified tabs, Dup, slot labels, and kit UI unaffected.

## Debugging notes (do not regress)

- `bool shared = false, free = -1` declares free as BOOL: slot search always
  "finds" slot 1 and full libraries clobber instead of refusing. Never name a
  variable `free`; the -Wbool-compare warning flagged it.
- Tests must assert presence per section, not just absence in muted ones —
  vacuous gating tests previously hid a fully silent sections 1+ (fixed 0.10).
- Seam off-events attribute to the previous section; only seam ons are leaks.

## Still to verify interactively

- Make-unique workflow by ear across a full song; Ctrl-click discoverability.
- Tooltip accuracy while rearranging sections.

## Next implementation step

Arch packaging (PKGBUILD, desktop entry, XDG dirs, PipeWire/JACK guidance),
then a full end-to-end acceptance pass on Omarchy hardware.

Build/run instructions, interactions, and realtime limitations are in README.md.
