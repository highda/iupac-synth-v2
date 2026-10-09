# Authored Patch contract fixtures

Issue #5, repository-authored under CC0-1.0. `PatchTests.cpp` constructs the valid
silent and serial/parallel graph fixtures through production values and exercises
their production JSON round trips. It derives invalid fixtures by introducing one
named violation at a time: duplicate IDs/edges, dangling/source endpoints, cycles,
illegal matrix destinations, caps, missing/unknown fields and nonfinite values.

These are contract fixtures, not rendered goldens. The authored synth panel is in
`data/panels/authored-synth`. `chemistry-analysis-v1.json` is the generated,
versioned expected Analysis response set for issue #4. Its hashes and generator
are frozen in `data/panels/chemistry-manifest.json`; changes follow the maintenance
policy in `docs/architecture/VERIFICATION.md` and must retain stable record IDs.
