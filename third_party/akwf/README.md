# Adventure Kid Waveforms (AKWF-FREE) — curated subset

Single-cycle waveforms by Kristoffer Ekstrand (Adventure Kid), from
<https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE> at the revision named in
`manifest.json`. Upstream dedicates the collection to the public domain under **CC0 1.0**
(`LICENSE.md`, copied unchanged). CC0 asks for no attribution; it is given here and in the user
guide anyway.

`waveforms/<table>/` holds the cycles of each sampled wavetable of the `wavetable` source (D12,
#166): up to sixteen files of one upstream folder, chosen by `scripts/build-wavetables.py --select`
(ordered by spectral centroid, evenly spaced). The files are byte-for-byte upstream; `manifest.json`
records each one's upstream path and SHA-256. `src/engine/WavetableData.cpp` is generated from
them and the `wavetable-data` test fails when it is stale.

Nothing else from the upstream repository is used.
