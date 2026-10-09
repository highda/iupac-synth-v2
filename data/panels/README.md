# Frozen chemistry panels

Issue #4 freezes the Analysis-v1 identity, broad-development, legacy-hard,
holdout and adversarial inputs before mapper calibration. Fixture annotations are
repository-authored under CC0-1.0. Per-record source fields identify either a
primary PubChem compound page, a minimal repository-authored structure, or the
pinned RDKit sequence generator; no descriptions or bulk database records are
copied.

`chemistry-manifest.json` records the generator/backend versions and SHA-256 of
every maintained input and expected-output file. Regenerate with
`./scripts/generate-chemistry-panels.py`. A change requires a scoped issue, an
independent structural correction, stable record IDs, old/new manifest hashes,
and affected V1 checks. Holdout inputs and `legacy-hard-pairs.json` must never be
removed or changed to improve mapping results. Stereo choices are explicit in
each assertion/annotation: specified biological or sequence stereochemistry is
named, and unmarked legacy structures are intentionally unspecified/racemic.
