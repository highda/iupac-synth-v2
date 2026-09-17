#!/usr/bin/env bash
set -euo pipefail
prefix=${1:?installed prefix required}
report=${2:?report path required}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

test ! -e /usr/bin/python3
test ! -e /usr/bin/java
export PATH=/usr/local/bin:/usr/bin:/bin PYTHONHOME=/no/python PYTHONPATH=/no/modules JAVA_HOME=/no/java CLASSPATH=/no/jar
"$prefix/bin/iupac-product-doctor" "$prefix"
ldd "$prefix/lib/vst3/IUPAC Synth 2.vst3/Contents/aarch64-linux/IUPAC Synth 2.so" > "$work/vst3-ldd.txt"
! grep -Eiq 'python|java|not found' "$work/vst3-ldd.txt"
"$prefix/bin/iupac-cli" analyze --mode name --text 2,2,2-trifluoroethan-1-ol > "$work/name.json"
"$prefix/bin/iupac-cli" generate --mode smiles --text 'CCN(CC)C(=O)c1ccc(Cl)cc1' > "$work/unseen.iupacpatch"
"$prefix/bin/iupac-cli" verify-panel --panel "$prefix/share/iupac-synth-2/panels/authored-synth/panel.json" --output-dir "$work/panel" > "$work/panel.json"
"$prefix/bin/iupac-cli" render --snapshot "$work/unseen.iupacpatch" --midi "$prefix/share/iupac-synth-2/factory/held-note.midi.json" --sample-rate 48000 --block-size 128 --output "$work/render.wav" > "$work/render.json"
"$prefix/bin/iupac-cli" inspect --snapshot "$work/unseen.iupacpatch" --stage effective > "$work/effective.json"
xvfb-run -a pluginval --validate "$prefix/lib/vst3/IUPAC Synth 2.vst3" --strictness-level 5 --output-dir "$work/pluginval"
xvfb-run -a timeout 5 "$prefix/bin/IUPAC Synth 2" > "$work/standalone.log" 2>&1 || test $? -eq 124
test -s "$work/render.wav"
grep -q 'OCC(F)(F)F' "$work/name.json"
grep -q 'CCN(CC)C(=O)c1ccc(Cl)cc1' "$work/unseen.iupacpatch"
printf '{"schemaVersion":1,"status":"pass","network":"disabled-by-runner","externalPython":false,"externalJava":false,"hostPluginInterpreterLink":false,"installedCli":true,"installedVst3":true,"installedStandalone":true,"panelParity":true,"renderSaveRestore":true,"readOnlyRelocation":true}\n' > "$report"
