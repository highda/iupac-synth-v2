#!/usr/bin/env bash
# Native macOS arm64 validation of the built chemistry-OFF product (#41):
# pluginval strictness 5 on VST3 and AU, auval, Info.plist identity, editor smoke,
# bounded Standalone launch, ad-hoc signature check and retained artifacts/reports.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-$repo_root/build/mac-synth}
output_dir=${2:-$repo_root/out/macos}
artefacts="$build_dir/IupacSynth2_artefacts/Release"
vst3="$artefacts/VST3/IUPAC Synth 2.vst3"
component="$artefacts/AU/IUPAC Synth 2.component"
app="$artefacts/Standalone/IUPAC Synth 2.app"
cli="$build_dir/iupac-cli"
pluginval="$repo_root/build/pluginval/pluginval_artefacts/Release/pluginval.app/Contents/MacOS/pluginval"
components_dir="$HOME/Library/Audio/Plug-Ins/Components"

[[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]]
for path in "$vst3" "$component" "$app" "$cli" "$pluginval"; do test -e "$path"; done
mkdir -p "$output_dir"

plist() { /usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist"; }
for bundle in "$vst3" "$component" "$app"; do
    test "$(plist "$bundle" CFBundleIdentifier)" = space.highda.iupacsynth2
    test "$(plist "$bundle" CFBundleName)" = 'IUPAC Synth 2'
done
test "$(plist "$component" AudioComponents:0:type)" = aumu
test "$(plist "$component" AudioComponents:0:subtype)" = Iup2
test "$(plist "$component" AudioComponents:0:manufacturer)" = Iups
test "$(plist "$component" AudioComponents:0:name)" = 'HighDA: IUPAC Synth 2'
if grep -rIl --exclude='*.json' Iup1 "$artefacts" "$cli"; then echo 'v1 identifier Iup1 found in artifacts' >&2; exit 1; fi
if grep -a -c Iup1 "$vst3/Contents/MacOS/IUPAC Synth 2" "$component/Contents/MacOS/IUPAC Synth 2" "$app/Contents/MacOS/IUPAC Synth 2" "$cli" | grep -v ':0$'; then
    echo 'v1 identifier Iup1 found in binaries' >&2; exit 1
fi
for binary in "$vst3" "$component" "$app" "$cli"; do
    codesign --verify --verbose=2 "$binary" 2>&1 | tee -a "$output_dir/codesign.txt"
    codesign -dv "$binary" 2>&1 | grep -E '^(Identifier|Signature|TeamIdentifier)' | tee -a "$output_dir/codesign.txt"
done
grep -q 'Signature=adhoc' "$output_dir/codesign.txt"
! grep -q 'TeamIdentifier=[A-Z0-9]' "$output_dir/codesign.txt"
lipo -archs "$vst3/Contents/MacOS/IUPAC Synth 2" | tee "$output_dir/architectures.txt"
test "$(lipo -archs "$vst3/Contents/MacOS/IUPAC Synth 2")" = arm64
otool -l "$vst3/Contents/MacOS/IUPAC Synth 2" | grep -A2 LC_BUILD_VERSION | tee -a "$output_dir/architectures.txt"

"$pluginval" --version | tee "$output_dir/pluginval-version.txt"
"$pluginval" --validate "$vst3" --strictness-level 5 --output-dir "$output_dir/pluginval-vst3" 2>&1 | tee "$output_dir/pluginval-vst3.log"

mkdir -p "$components_dir"
rm -rf "$components_dir/IUPAC Synth 2.component"
cp -R "$component" "$components_dir/"
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v aumu Iup2 Iups 2>&1 | tee "$output_dir/auval.txt"
grep -q 'AU VALIDATION SUCCEEDED' "$output_dir/auval.txt"
"$pluginval" --validate "$components_dir/IUPAC Synth 2.component" --strictness-level 5 --output-dir "$output_dir/pluginval-au" 2>&1 | tee "$output_dir/pluginval-au.log"

"$build_dir/iupac-editor-tests" 2>&1 | tee "$output_dir/native-editor-smoke.log"

"$app/Contents/MacOS/IUPAC Synth 2" > "$output_dir/standalone.log" 2>&1 &
standalone_pid=$!
sleep 8
kill -0 "$standalone_pid"
kill -TERM "$standalone_pid"
wait "$standalone_pid" || true
printf 'standalone launched for 8 s and exited on SIGTERM\n' | tee -a "$output_dir/standalone.log"

"$cli" > "$output_dir/product.json"
jq -e '.product == "IUPAC Synth 2" and .architecture == 3 and .chemistryEnabled == false' "$output_dir/product.json" >/dev/null

rm -rf "$output_dir/artifacts"; mkdir -p "$output_dir/artifacts"
cp -R "$vst3" "$component" "$app" "$output_dir/artifacts/"
cp "$cli" "$output_dir/artifacts/iupac-cli"
(cd "$output_dir/artifacts" && find . -type f -print0 | sort -z | xargs -0 shasum -a 256 > ../artifacts.sha256)
git -C "$repo_root" rev-parse HEAD > "$output_dir/commit.txt"
{ sw_vers; uname -m; clang --version | head -1; } > "$output_dir/host.txt"
echo "macOS validation passed; reports in $output_dir"
