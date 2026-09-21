#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# Linux canonical V0 uses linux-synth under Xvfb; the native macOS build uses mac-synth on a real window server.
if [[ "$(uname -s)" == Darwin ]]; then preset=${IUPAC_V0_PRESET:-mac-synth}; display=(); shared_suffix=.dylib
else preset=${IUPAC_V0_PRESET:-linux-synth}; display=(xvfb-run -a); shared_suffix=.so; fi
build_dir="$repo_root/build/$preset"
output_dir="$repo_root/out/verification/v0"
pluginval="$repo_root/build/pluginval/pluginval_artefacts/Release/pluginval"
[[ "$(uname -s)" == Darwin ]] && pluginval="$pluginval.app/Contents/MacOS/pluginval"
plugin="$build_dir/IupacSynth2_artefacts/Release/VST3/IUPAC Synth 2.vst3"
snapshot="$repo_root/data/panels/authored-synth/all-modules.snapshot.json"
# The fixed phase-4 worst-case patch VERIFICATION V8 names: every slot active, 48 edges, 40 rows,
# unisonVoices 7 on all three general source slots, both typed audio-rate ports cabled and the whole
# effects tail at non-zero mix. Kept in the tree and re-derived from the production catalog by
# `product-path-smoke`, so it cannot drift away from the graph the gate describes (#129).
worst_case="$repo_root/tests/fixtures/phase4-worst-case.snapshot.json"

cmake --preset "$preset" -S "$repo_root"
cmake --build --preset "$preset" --parallel "${IUPAC_BUILD_JOBS:-4}"
ctest --preset "$preset" --output-on-failure
mkdir -p "$output_dir"
"$build_dir/iupac-cli" > "$output_dir/product.json"
"$build_dir/iupac-cli" verify-panel --panel "$repo_root/data/panels/authored-synth/panel.json" --output-dir "$output_dir/authored"
"$build_dir/iupac-cli" benchmark --snapshot "$snapshot" --seconds "${IUPAC_V0_BENCHMARK_SECONDS:-60}" > "$output_dir/benchmark.json"
"$build_dir/iupac-cli" benchmark --snapshot "$worst_case" --seconds "${IUPAC_V0_BENCHMARK_SECONDS:-60}" > "$output_dir/benchmark-phase4-worst-case.json"

jq -e '.chemistryEnabled == false and .architecture == 3' "$output_dir/product.json" >/dev/null
jq -e '.results | length > 0 and all(.[]; .peak <= 0.891252 and (.dc | fabs) <= 0.005 and .guardHits == 0)' "$output_dir/authored/manifest.json" >/dev/null
jq -e '.voices == 16 and .structuralTransitions > 0 and .activeBanksMaximum == 2 and .renderRatio <= 0.5 and .p99BlockSeconds < (128/48000) and .residentBytesAfter <= 134217728 and .residentBytesAfter <= (.residentBytesSteady + 1048576)' "$output_dir/benchmark.json" >/dev/null
# D8 phase-4 budget gates (V8): the same ratio and p99 ceiling on the worst-case patch, plus the
# derived per-voice budget, with the once-per-block effects tail measured and reported apart from it.
jq -e '.voices == 16 and .structuralTransitions > 0 and .activeBanksMaximum == 2 and .renderRatio <= 0.5 and .p99BlockSeconds < (128/48000) and .effectsTailMeasured == true and .perVoiceBlockSeconds <= .perVoiceBudgetSeconds and .residentBytesAfter <= 134217728 and .residentBytesAfter <= (.residentBytesSteady + 1048576)' "$output_dir/benchmark-phase4-worst-case.json" >/dev/null
test "$(grep '^IUPAC_ENABLE_CHEMISTRY:BOOL=' "$build_dir/CMakeCache.txt")" = 'IUPAC_ENABLE_CHEMISTRY:BOOL=OFF'
if find "$build_dir" -type f \( -name "*$shared_suffix" -o -perm -111 \) -print0 | xargs -0 strings | grep -Eiq 'rdkit|opsin|libpython|libjvm'; then
    echo 'chemistry runtime reference found in synth-only artifacts' >&2
    exit 1
fi
test -x "$pluginval"
${display[@]+"${display[@]}"} "$pluginval" --validate "$plugin" --strictness-level 5 --output-dir "$output_dir/pluginval"

git rev-parse HEAD > "$output_dir/commit.txt"
