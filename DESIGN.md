# Sonora UI design directions

## Active: Daylight Paper + refined dark, one-tap toggle (shipped)
Light mode (warm paper, ink text, deep teal accent) and a refined dark mode,
switched by the sun/moon button in the header (click toggles Light/Dark,
Shift-click returns to the System/Omarchy theme; choice persists in
`~/.config/sonora/ui.json`). The piano roll and drum grid stay dark insets
in both modes. Built-in palettes in `OmarchyTheme.h` (`sonoraLightPalette`,
`sonoraDarkPalette`); all adaptive rendering keys off the palette's `dark`
flag via `ui::uiDark`. Verified in the snapshot tool (see
`arrangement-light.png`).
Ideas kept for later: auto evening-dim companion, per-part-tinted chips.

## Retained: Deep Gradient Glow (subtle, both modes)
Indigo wash + ambient blooms (dark) / cool paper glow (light), glass top
edges with contrasting highlight, luminous hover/press/selected edges,
selection bloom, cyan-to-violet playhead, glowing hot knob chips. Implemented
in `NeonTheme` (`glow`, `surface`, `drawButtonBackground`),
`MainComponent::paint` background, `ArrangementView` headers,
`PianoRoll` playhead, and `paintKnobStrip`. All effects derive from the live
palette, so theme-following keeps working. Deliberately restrained so glow
means something.
