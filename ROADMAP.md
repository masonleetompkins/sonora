# Sonora roadmap — approved build order (easiest + most consequential first)

Build 1-8, skip the rest.

1. **Groove: swing, quantize, humanize** — per-track swing at schedule time,
   destructive quantize/humanize edits with undo. DONE (format v14).
2. **Piano roll key tools**: scale lock + chord stamps + velocity ramp.
   DONE (project key/scale, format v15, AI writes in key).
3. **Arpeggiator + chord MIDI FX** for live MiniLab playing.
   DONE (per-track arp/chord FX at song tempo, format v16).
4. **Take comping UI** (takes model already exists).
   DONE lanes (waveform lanes, solo audition, keep-as-comp, format v17).
   Section-swipe comping is a follow-up.
5. **Global chord track** feeding AI composer + transposition.
   DONE (per-section chords, follow-transpose in song playback, AI writes
   in-chord, format v18).
6. **Sends/returns + mixer view**.
7. **Automation lanes** (volume/pan/FX; format bump).
8. **Time-stretch for takes** (Rubber Band library integration).

Deferred (do not build): stem separation (ML model weight, giants do it
well), plugin hosting (sandbox/licensing complexity), Session Players clone
(Sonora's AI song composer already covers this ground better).
