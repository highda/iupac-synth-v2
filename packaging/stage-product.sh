#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-$repo_root/build/linux-release}
runtime=${2:-$repo_root/out/iupac-chemistry}
prefix=${3:?explicit install prefix required}

sha256=$(command -v sha256sum >/dev/null 2>&1 && echo sha256sum || echo 'shasum -a 256')
test -x "$build/iupac-cli"
test -x "$build/IupacSynth2_artefacts/Release/Standalone/IUPAC Synth 2"
test -d "$build/IupacSynth2_artefacts/Release/VST3/IUPAC Synth 2.vst3"
test -f "$runtime/package-report.json"
test ! -e "$prefix" || { echo "install prefix already exists: $prefix" >&2; exit 2; }

mkdir -p "$prefix/bin" "$prefix/lib/vst3" "$prefix/resources" "$prefix/share/iupac-synth-2/factory" \
  "$prefix/share/iupac-synth-2/notices"
install -m 0755 "$build/iupac-cli" "$prefix/bin/iupac-cli"
install -m 0755 "$build/IupacSynth2_artefacts/Release/Standalone/IUPAC Synth 2" "$prefix/bin/IUPAC Synth 2"
cp -a "$build/IupacSynth2_artefacts/Release/VST3/IUPAC Synth 2.vst3" "$prefix/lib/vst3/"
cp -a "$runtime" "$prefix/resources/chemistry"
cp -a "$runtime" "$prefix/lib/vst3/IUPAC Synth 2.vst3/Contents/Resources/chemistry"
cp "$repo_root/data/panels/authored-synth/all-modules.snapshot.json" \
  "$prefix/share/iupac-synth-2/factory/All Modules.iupacpatch"
cp -a "$repo_root/data/panels" "$prefix/share/iupac-synth-2/panels"
cp "$repo_root/data/panels/authored-synth/held-note.midi.json" "$prefix/share/iupac-synth-2/factory/held-note.midi.json"
cp "$repo_root/docs/package-notices.md" "$repo_root/docs/USER-GUIDE.md" \
  "$repo_root/cmake/Dependencies.lock" "$prefix/share/iupac-synth-2/notices/"
cp -a "$repo_root/docs/images" "$prefix/share/iupac-synth-2/notices/images"
cp "$repo_root/packaging/product-doctor.sh" "$prefix/bin/iupac-product-doctor"
chmod 0755 "$prefix/bin/iupac-product-doctor"

(cd "$prefix" && find . -type f ! -name install-manifest.sha256 -print0 | sort -z | xargs -0 $sha256 > install-manifest.sha256)
python3 - "$prefix" <<'PY'
import json, os, sys
from pathlib import Path
root=Path(sys.argv[1]); lines=(root/"install-manifest.sha256").read_text().splitlines()
report={"schemaVersion":1,"product":"IUPAC Synth 2","bundleId":"space.highda.iupacsynth2","manufacturerCode":"Iups","pluginCode":"Iup2","architecture":3,"files":len(lines),"unpackedBytes":sum(x.stat().st_size for x in root.rglob("*") if x.is_file())}
(root/"install-report.json").write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
PY
# The report itself is part of delivery integrity.
(cd "$prefix" && $sha256 install-report.json >> install-manifest.sha256)
"$prefix/bin/iupac-product-doctor" "$prefix"
tar -C "$(dirname "$prefix")" -czf "$prefix.tar.gz" "$(basename "$prefix")"
(cd "$(dirname "$prefix")" && $sha256 "$(basename "$prefix").tar.gz" > "$(basename "$prefix").tar.gz.sha256")
