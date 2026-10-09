# Mapping policy — mapper version 3

Ground-up rework ([D13](DECISIONS.md), #168, phase 5 #165). Mapper version **3**, SonicIntent version **2**. It replaces the seven-axis mapper (versions 1 and 2), whose text is in git history. Analysis v1 and the helper are unchanged. Coefficients are versioned policy: retune them with before/after snapshot and panel evidence and a version bump, not a new decision per number.

**Why it was replaced.** Over 4,957 ranked snapshot molecules, mapper 2 chose the additive source as primary for 86.7%, put a modal resonator on 54.2% and combined additive + FM + modal resonator on 32.4% — one recipe, and it was a bell. Mapper 3 on the same sample: classic oscillator primary 44.3%, wavetable 41.0%, FM 11.5%, additive 3.3%; modal resonator 2.7%; no voice leads more than 18.3%.

## Three stages

All three are inspectable (`iupac-cli inspect --stage sonic|mapping`).

1. **Profile** (`Profile.cpp`). Structural facts computed from the Analysis graph, about seventy of them: size and composition (element kinds, halogen period, metals), bonding (aromatic and sp3 fractions, alkene and triple bonds, largest conjugated system), rings (ring-atom fraction, fusion, smallest ring per atom), shape (fragments, diameter, elongation, branching, longest acyclic carbon chain, symmetry classes by Morgan refinement), charge, isotopes and stereo (R/S balance, E/Z balance), polarity (TPSA, logP, H-bond donors and acceptors per atom), functional groups (amide, amine, hydroxyl, phenol, ether, carboxyl, ester, nitro, nitrile, sulfonyl, thio, phosphate) and **placement** — graph centrality of heteroatoms, branches and rings, and nearest-neighbour distances between nitrogen, oxygen, branches and rings. Every field is a count, a fraction or a topological distance; none is a hash.
2. **Traits** (`project()`). About fifty values in [0, 1] (`handed` and `cisTrans` in [−1, 1]). Continuous profile fields are rescaled through the quartiles of the discovery snapshot so they spread across real molecules; counts saturate; a functional group counts by presence and by its share of the molecule. `bright` (polarity, unsaturation, halogens, minus lipophilicity) and `placement` are the two blends.
3. **Patch** (`generate()`), below.

## Voices

The primary source is the best-scoring of nineteen **voices**; scores are sums of the traits a voice stands for, and all are traced.

| Voice | Module | Answers to |
| --- | --- | --- |
| saw | osc | alkene/alkyne bonds, non-aromatic conjugation, long chains |
| square | osc | carbocyclic aromatic rings |
| pulse | osc | heteroaromatic rings; width from the hetero share and placement |
| triangle | osc | saturated rings |
| sine | osc | the smallest molecules |
| wt-voice | wavetable `voice` / `formant` | amides |
| wt-organ | `eorgan` / `organ` | fused ring systems |
| wt-strings | `cello` / `violin` | long flexible molecules |
| wt-wind | `flute` / `clarinet` / `oboe` / `altosax` | hydroxyl, ether, phenol |
| wt-keys | `piano` / `epiano` / `clavinet` | carboxyl, ester |
| wt-plucked | guitars and basses | amines |
| wt-chip | `chip` / `vgame` | light halogens |
| wt-edge | `distorted` / `sync` / `digital` / `pwm` | nitro, nitrile, charge, ring strain |
| wt-grain | `granular` | salts |
| wt-complex | `handdrawn` / `blended` / `overtone` / `fmsynth` / `harmonics` | many element kinds, low symmetry, isotopes |
| wt-reed | `theremin` | thiols and sulfides |
| fm-harmonic | fm, whole-number ratios | sulfonyl, phosphorus |
| fm-metal | fm, irrational ratio | metals, heavy halogens — the only inharmonic voice |
| additive | harmonic, exact harmonic series | polar, element-diverse molecules |

A wavetable's `position` is `bright` shifted by placement (sampled tables are ordered dark to bright). A **layer** — the second-best voice — is added when the molecule is complex enough (low symmetry, size, element diversity) and always for a salt; its interval is a unison, octave, fifth or twelfth chosen by build.

## The rest of the patch

- **Envelope.** Seven archetypes (perc, pluck, keys, lead, organ, swell, pad) are weighted by build — small acyclic rigid molecules are struck, ring systems hold, large flexible ones swell — plus a bias from the voice; the weights are cubed and the ADSR is their blend, times in the log domain. Placement stretches the decay.
- **Unison, drift, register.** Thickness from size, flexibility and conjugation, thinner for struck sounds; detune from flexibility and asymmetry. Only the largest lipophilic molecules drop an octave. Isotopes lower `fine` by a few cents.
- **Noise** for halogens, charge and the smallest non-polar molecules. **Sub** for large lipophilic molecules.
- **Filter.** Cutoff from `bright`, placement and branching; resonance from carbonyls, strain and thio groups; struck sounds are opened by the filter envelope. Mode by character: notch for zwitterions, 24 dB ladder for large lipophilic molecules, high-pass for tiny polar ones, low-pass otherwise.
- **Drive.** Present when charge, nitro, strain, halogens or conjugated unsaturation is strong; the cause picks the curve.
- **Resonator: the exception.** A comb for strained or caged skeletons, a modal bank only for inorganic compounds with two or more metal atoms.
- **Effects.** Chorus for very flexible molecules, tempo-synced delay for repeating units (amide links, chains), reverb scaled by size, width for stereocentres, E/Z bonds and salts.
- **Stereo.** R and S, and E and Z, lean the mixer pan, unison spread and delay spread opposite ways.
- **Modulation.** At most twelve rows: velocity to level and tone, L1 to the voice's one "motion" parameter (wavetable position, pulse width, FM index or vibrato), E3 sweeping a wavetable, E2 biting an FM index, mod wheel, and four macros labelled Tone, Motion, Edge and Space.

Stable role ids (`primary`, `secondary`, `noise`, `sub`, `resonator`, `shaper`, `filter`, `mix`, `chorus`, `delay`, `reverb`, `width`), one fixed noise seed, no RNG.

## Evidence at `mapper 3`

- **Snapshot sample (4,957 molecules, every sixth by rank):** 0 generation errors; primary voices saw 18.3%, square 13.8%, fm-metal 9.8%, wt-wind 9.6%, wt-organ 6.6%, wt-voice 5.5%, triangle 4.9%, pulse 4.3%, the other eleven 1.0–3.6% each; 54% of patches layered; dominant envelope archetype pad 29.5%, pluck 17.1%, organ 14.1%, swell 12.5%, perc 11.8%, lead 8.4%, keys 6.6%; 494 distinct module sets, the largest 5.1%.
- **Frozen panels (`mapping-calibration`):** 16 / 11 / 7 broad / hard / holdout graph signatures; broad V4 pass fraction 0.996; minimum hard pair 0.085, minimum holdout pair 0.116; peak ≤ 0.785 and no guard hits at note 60.
- **Render sweep (248 snapshot molecules, note 60):** active RMS −35.7 to −10.6 dBFS, median −20.4; 2 patches touched the safety guard; nothing non-finite.
- **The holdout was visible during this calibration**, so for mapper 3 it is a second development set, not an untouched holdout.
- These are spread and safety measurements. Whether the sounds are good is a listening judgment.

## Coverage

`data/mapping-coverage.json` is generated from traces by `tools/build_mapping_coverage.py`: `mapped` (assigned on the frozen panel), `offPanel` (assigned only for structures the panel lacks — the resonator), `unmapped` (left at the catalog default). At mapper 3: 84 / 7 / 21. Curated beats complete (D13): an unmapped parameter is allowed and must say so.
