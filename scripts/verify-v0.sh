#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repo_root/build/linux-synth"
output_dir="$repo_root/out/verification/v0"
pluginval="$repo_root/build/pluginval/pluginval_artefacts/Release/pluginval"
plugin="$build_dir/IupacSynth2_artefacts/Release/VST3/IUPAC Synth 2.vst3"
snapshot="$repo_root/data/panels/authored-synth/all-modules.snapshot.json"

cmake --preset linux-synth -S "$repo_root"
cmake --build --preset linux-synth --parallel "${IUPAC_BUILD_JOBS:-4}"
ctest --preset linux-synth --output-on-failure
mkdir -p "$output_dir"
"$build_dir/iupac-cli" > "$output_dir/product.json"
"$build_dir/iupac-cli" verify-panel --panel "$repo_root/data/panels/authored-synth/panel.json" --output-dir "$output_dir/authored"
"$build_dir/iupac-cli" benchmark --snapshot "$snapshot" --seconds "${IUPAC_V0_BENCHMARK_SECONDS:-60}" > "$output_dir/benchmark.json"

jq -e '.chemistryEnabled == false and .architecture == 3' "$output_dir/product.json" >/dev/null
jq -e '.results | length > 0 and all(.[]; .peak <= 0.891252 and (.dc | fabs) <= 0.005 and .guardHits == 0)' "$output_dir/authored/manifest.json" >/dev/null
jq -e '.voices == 16 and .structuralTransitions > 0 and .activeBanksMaximum == 2 and .renderRatio <= 0.5 and .p99BlockSeconds < (128/48000) and .residentBytesAfter <= 134217728 and .residentBytesAfter <= (.residentBytesSteady + 1048576)' "$output_dir/benchmark.json" >/dev/null
test "$(grep '^IUPAC_ENABLE_CHEMISTRY:BOOL=' "$build_dir/CMakeCache.txt")" = 'IUPAC_ENABLE_CHEMISTRY:BOOL=OFF'
if find "$build_dir" -type f \( -name '*.so' -o -perm -111 \) -print0 | xargs -0 strings | grep -Eiq 'rdkit|opsin|libpython|libjvm'; then
    echo 'chemistry runtime reference found in synth-only artifacts' >&2
    exit 1
fi
test -x "$pluginval"
xvfb-run -a "$pluginval" --validate "$plugin" --strictness-level 5 --output-dir "$output_dir/pluginval"

git rev-parse HEAD > "$output_dir/commit.txt"
