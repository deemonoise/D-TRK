# DICE Genre Lab experiment

Branch `feature/dice-hybrid` combines the Genre Lab catalogue from `feature/dice-cycles-guide` with six-part composition using the shared BASS/ARP generators. The previous standalone implementation remains in `feature/teensy`; there is no legacy switch.

Source: [user-supplied genre guide](https://claude.ai/artifact/UeVf5nnXrrpLsn3fHgb9xh).
The artifact actually identifies itself as **Model:Samples Genre Lab**, rather
than Model:Cycles. Snapshot retrieved on 2026-11-11: 54 groups, 317 styles,
16 variants per style. Source JavaScript SHA256:
`2470d87e2f8114cbde0e79460b2dbfc941f94c4d1d1e4f73ac5c8ce7000fdcc3`.

`dice_guide.json.gz` contains normalized musical facts, without the guide's prose
or JavaScript: tempo, swing, tonal centre, six voice roles, sound controls and
sparse events for all 5072 variants. Identical sequences and voices are shared.
The one-time browser extraction and import conversion are retained in the ignored
`.metrics/dice-cycles-guide-2026-11-11` working records. Firmware has no browser,
JavaScript engine, JSON parser, SD catalogue dependency or heap allocation in the
generation path.

Regenerate the flash tables:

```sh
python3 scripts/generate_dice_catalog.py
python3 scripts/generate_dice_catalog.py --check
```

## Device controls

PROJ → DICE: choose **Group → Style → Variant**. AUTO picks one of the 16
variants from the seed (the default); FIXED seed repeats the same selection.
Variants can also be selected explicitly by name, with
`01 CLASSIC` representing the source theme. Target and six locks show each
recipe's destination track roles. Percussion is packed into one KIT on track1,
followed by native pitched voices and complementary parts up to six tracks. The hybrid reuses the BASS/ARP generators, shares recipe harmony and selects numerical catalogue timbre recipes for missing roles. Ambient/noise profiles get sparse support, while six-pitched-voice recipes retain all their native voices. KIT children use I25–I32 exclusively; track instruments and the KIT container use I1–I24. Other tracks and patterns beyond
the displayed P01–P04/P06 range are preserved.

Scope selects tempo/key, sounds, notes or all. Full rolls can make Classic,
Develop or Arrange sections and a song chain. Notes NEW replaces the parts;
DYNAMICS and BLEND retain their bounded edit behavior. Targeted rolls and locks
preserve other parts and global timing/key.

Key AUTO chooses a seeded tonic and a compatible mode family for a full ALL or
TEMPO roll. NEW avoids the preceding tonic and mode; FIXED repeats the choice.
Key KEEP retains the project key. SOUNDS, PATTERNS and partial edits always keep
it. The result toast states NEW ALL/NOTES, FIXED, SOUNDS ONLY or DYNAMICS ONLY.

The guide supplies source rhythm/roles/character, not a fixed final composition.
Variety35 (default) writes fresh per-role16slot melodic contours from the seed,
with related responses and a bounded register. Higher amounts develop more notes.
Notes follow actual source chords, or the chosen scale without a chord part.
Harmony shifts are shared across pitched parts and respect held pad/chord attacks;
independently looping parts retain stable harmonic development. Repeated NEW rolls
check the actually played loop,avoiding identical consecutive melodic parts even
with Key KEEP. One percussion voice may add a quiet hit on a globally empty KIT
step. Sound identity,engine,machine and oscillators stay fixed. Variety0 retains
the source contour,adapted to the chosen key;use KEEP with the guide key/mode,
an explicit variant,Normal density and Feel0 to inspect source phrasing.

NEW/AUTO avoids the immediately preceding rhythm variant; FIXED is deterministic.
Full rolls audition MAIN while preserving transport state.

Preferences use `dg2*` NVS keys; destination Target/Locks use `dg3target` and
`dg3locks` because their meaning changed with KIT packing. Key uses `dg4key`; its byte occupies previously unused Prefs padding (size20,seed offset16 unchanged). Existing projects use the same file
format and synthesizer protocol. Old DML1 phrase files and upload/validation
utilities retain their original nine-group IDs independently of this catalogue;
MIDI/HYBRID is absent from the new generator.

## Adaptation to this instrument

This is synthesized interpretation, not sample or hardware emulation. Normalized
schema2 retains each voice's sound family, brightness, grit and chord character,
derived from the saved sample descriptions. `scripts/dice_timbres.py` constructs
complete numerical synthesis blueprints: engine and machine, oscillators/macros or
FM parameters, amplitude/filter envelopes, filter, sub, modulation, drive and level.
The firmware copies these blueprints into instruments; DICE performs no factory
preset lookup. Native voices use their exact blueprints; supplemental roles use
compatible blueprint palettes from the catalogue. 808/909 drums use DRUM; other percussion and
bells use FM, guitar/mallet use STRING, textures use CHIP noise, and bass/keys/pads
use SYNTH (metallic keys may use FM). Families include sine sub, detuned Reese,
resonant acid, organ, acoustic string and formant voice. The guide describes samples,
so oscillator architectures are our authored interpretation, not settings literally
specified by the source. Relative guide levels and blueprint headroom both apply.
Synthesized chord voices receive power compensation for three/four simultaneous
notes: a sample containing a chord must not become three/four full-level voices.
The default stab uses triangle/saw, a softer filter envelope and a short release.
All 5072 variants retain their own rhythms, velocities and controls.

Explicit relative notes, probability, A:B/FILL conditions, repeats, filter and
send locks map to tracker notes and FX. Melodic notes follow the guide's tonal
centre; pattern-only rolls transpose them to the current project root. Chord voices
use triads/sevenths or explicit major/minor seventh/ninth types from their character;
non-chord guitar and melodic parts remain single notes. Pattern length repeats/crops the source theme, and
resolution changes quantize attacks with bounded NDG compensation. Polymetric
lengths and supported speed ratios map to track timing.

Model:Samples cutoff is a bipolar LP/HP control; its curves, sample decay and
gain are approximations on these synthesized voices. LFO destinations/waves use the
nearest supported tempo division. Sample start/length/slicing, sample locks,
reverse audio and recorded vocals cannot be reproduced by synthesized voices.
Ambiguous text ranges/alternative conditions are resolved deterministically;
the guide itself includes algorithmic variants that were not all auditioned.
KIT steps share their FX. Conflicting per-lane conditions/probability are
resolved deterministically without gating an unconditional anchor; conflicting
ratchet/tone locks are not broadcast to the kit. ACC represents quieter hits,
and KIT delay/reverb sends average the source voice sends.
Up to six tracker FX fit per event, with probability/conditions/repeats and chord
articulation taking priority over timbral locks.

Output-format changes (KIT versus pitched notes) protect untouched patterns. A
sound-only format change with existing parts, or a change with parts outside the
rolled range, is skipped. Same-format KIT sound changes use safe instrument
slots, including lane instruments. Shared/locked instruments remain protected.
A `PARTIAL` result indicates protected tracks or unavailable slots.
