# Mapper version 1 calibration evidence

Issue #10 calibrates the
version-1 production mapper against the already-frozen Analysis fixture and V4
metric. The reproducible gate is `mapping-calibration` in the chemistry-enabled
CTest suite. It invokes `iupac-cli inspect` and `iupac-cli render`; NumPy only
reads those production WAVs. WAVs, complete traces and manifests are retained
under the ignored build/output directory rather than committed as binary data.

Inputs are Analysis fixture SHA-256
`e8da45b06fc515df93ba770239c681550a27e25902db2a49ef1c36f60dc964b5`
and legacy-pair manifest SHA-256
`4e66a2cb3ba59b1b1d364f3ab6d5618500efc69e9860fdda2db6818f1791443f`.
The overall chemistry manifest remains
`ccb0a6aabee1cdca062e7a25b22c30477dfba9e016124e463fcda8b9a8d8d694`.

Two bounded development attempts were used:

1. Starter mapping: broad pair pass fraction 0.235507; 14 required hard/holdout
   pairs collapsed because modal resonators had zero default mode levels; 29
   default renders were silent or below -60 dBFS.
2. Nonzero structural modal arrays repaired silence and raised broad coverage to
   0.992754. Three ordered peptide pairs still failed, at 0–0.006669, and three
   high-pass patches remained below -60 dBFS. The repair was driven by the hard
   peptide failures: motif placement now reaches resonator tuning in both modes,
   and filter cutoff is calibrated relative to filter mode.

The formulas in `MAPPING-POLICY.md` and mapper/projection version 1 were then
frozen. The final complete run, including holdout, made no subsequent mapping
change. Report SHA-256 is
`bbce4f63ce33e8f4c7d935c39210c45542a9b2f001bb49e20a16eb6a7fca8906`;
trace SHA-256 is
`16252edadbbe005b038d633013201fab2d2f1b7ee8c93c4797ca3a9b062665e5`.

Final objective results: 9/7/6 broad/hard/holdout graph signatures; broad V4
pass fraction 0.996377; minimum hard distance 0.118171; minimum required holdout
distance 0.125904. Default note-60 active RMS ranged -57.571 to -9.499 dBFS,
maximum peak was 0.661254, maximum DC magnitude was below 0.005, and hard-guard
hits were zero. These are anti-collapse and safety results, not evidence of
universal chemical uniqueness or musical quality.
