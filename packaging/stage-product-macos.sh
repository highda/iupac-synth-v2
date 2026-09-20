#!/usr/bin/env bash
# Stage the complete self-contained macOS arm64 preview into an explicit new
# prefix (#42): VST3, AUv2 and Standalone bundles each carrying the private
# chemistry payload in native nested-code layout, a CLI distribution directory
# carrying the same payload, factory patches/panels/notices, a shasum manifest,
# install-report.json, the bundle doctor and a compressed sibling with its hash.
# Usage: stage-product-macos.sh [chemistry-build-dir] [payload] <prefix>
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-$repo_root/build/mac-release}
runtime=${2:-$repo_root/out/iupac-chemistry-macos}
prefix=${3:?explicit install prefix required}
artefacts="$build/IupacSynth2_artefacts/Release"

[ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] || { echo 'stage natively on macOS arm64' >&2; exit 1; }
test -x "$build/iupac-cli"
test -d "$artefacts/VST3/IUPAC Synth 2.vst3"
test -d "$artefacts/AU/IUPAC Synth 2.component"
test -d "$artefacts/Standalone/IUPAC Synth 2.app"
test -f "$runtime/package-report.json"
test ! -e "$prefix" || { echo "install prefix already exists: $prefix" >&2; exit 2; }

# One flat distribution directory: the three bundles, the CLI, the doctor and the
# shared payload sit side by side, which is the layout the pre-release archive and
# the owner's install instructions already use.
mkdir -p "$prefix/resources" "$prefix/share/iupac-synth-2/factory" "$prefix/share/iupac-synth-2/notices"
install -m 0755 "$build/iupac-cli" "$prefix/iupac-cli"
install -m 0755 "$repo_root/packaging/product-doctor-macos.sh" "$prefix/iupac-product-doctor"
cp -R "$artefacts/VST3/IUPAC Synth 2.vst3" "$artefacts/AU/IUPAC Synth 2.component" \
    "$artefacts/Standalone/IUPAC Synth 2.app" "$prefix/"
cp -R "$runtime" "$prefix/resources/chemistry"

# Every format carries one complete private payload in its own resources, the same
# flat tree the CLI distribution directory uses. Mach-O code deliberately does not
# go in Contents/Helpers or Contents/Frameworks: codesign applies nested-code rules
# there and refuses a payload of this shape ("bundle format unrecognized, invalid,
# or unsuitable" at _internal/numpy/.dylibs, "code object is not signed at all" at
# java/lib/jvm.cfg), whereas this layout signs and passes `codesign --verify
# --strict`, which is what "permits later native signing" has to mean. Recorded as
# the D4 Placement clarification in docs/architecture/TOOLCHAIN.md.
stage_bundle_payload() {
    local bundle=$1
    mkdir -p "$bundle/Contents/Resources/chemistry"
    cp -R "$runtime/." "$bundle/Contents/Resources/chemistry/"
    # Inserting content breaks the bundle's own ad-hoc seal; re-seal it so the
    # loader and a later native signing pass both still accept the bundle. The
    # identity stays ad-hoc: Developer ID signing and notarization are outside
    # this preview.
    codesign --force --sign - --preserve-metadata=entitlements,flags "$bundle"
    codesign --verify --strict "$bundle"
    codesign --verify "$bundle/Contents/Resources/chemistry/helper/iupac-analysis-helper"
    codesign --verify "$bundle/Contents/Resources/chemistry/java/bin/java"
}
for bundle in "$prefix/IUPAC Synth 2.vst3" "$prefix/IUPAC Synth 2.component" \
    "$prefix/IUPAC Synth 2.app"; do
    stage_bundle_payload "$bundle"
done

cp "$repo_root/data/panels/authored-synth/all-modules.snapshot.json" \
    "$prefix/share/iupac-synth-2/factory/All Modules.iupacpatch"
cp "$repo_root/data/panels/authored-synth/held-note.midi.json" "$prefix/share/iupac-synth-2/factory/held-note.midi.json"
cp -R "$repo_root/data/panels" "$prefix/share/iupac-synth-2/panels"
cp "$repo_root/docs/package-notices.md" "$repo_root/docs/USER-GUIDE.md" \
    "$repo_root/cmake/Dependencies.lock" "$prefix/share/iupac-synth-2/notices/"
cp -R "$repo_root/docs/images" "$prefix/share/iupac-synth-2/notices/images"

(cd "$prefix" && find . -type f ! -name install-manifest.sha256 -print0 | sort -z | xargs -0 shasum -a 256 > install-manifest.sha256)
/usr/bin/python3 - "$prefix" <<'PY'
import hashlib, json, sys
from pathlib import Path
root = Path(sys.argv[1])
lines = (root / "install-manifest.sha256").read_text().splitlines()
formats = {"VST3": "./IUPAC Synth 2.vst3/", "AUv2": "./IUPAC Synth 2.component/",
           "Standalone": "./IUPAC Synth 2.app/", "CLI": "./iupac-cli",
           "chemistry-payload": "./resources/chemistry/"}
report = {"schemaVersion": 1, "product": "IUPAC Synth 2", "bundleId": "space.highda.iupacsynth2",
          "manufacturerCode": "Iups", "pluginCode": "Iup2", "architecture": 3, "platform": "macos-arm64",
          "chemistryEnabled": True, "signing": "ad-hoc", "files": len(lines),
          "unpackedBytes": sum(x.stat().st_size for x in root.rglob("*") if x.is_file()), "formats": {}}
for name, relative in formats.items():
    selected = sorted(line for line in lines if line.split("  ", 1)[1].startswith(relative))
    digest = hashlib.sha256("\n".join(selected).encode()).hexdigest()
    target = root / relative[2:].rstrip("/")
    sized = [target] if target.is_file() else [x for x in target.rglob("*") if x.is_file()]
    report["formats"][name] = {"path": relative[2:].rstrip("/"), "files": len(selected), "contentSha256": digest,
                               "unpackedBytes": sum(x.stat().st_size for x in sized)}
(root / "install-report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
PY
# The report itself is part of delivery integrity.
(cd "$prefix" && shasum -a 256 install-report.json >> install-manifest.sha256)

# The doctor must run under the owner's bash 3.2 as well as /bin/sh.
/bin/sh "$prefix/iupac-product-doctor" "$prefix" >/dev/null
/bin/bash "$prefix/iupac-product-doctor" "$prefix"

# bsdtar keeps bundle structure, symlinks and executable bits; no AppleDouble or
# xattr side files. The ditto round trip proves the same through a macOS zip.
archive="$prefix.tar.gz"
rm -f "$archive" "$archive.sha256"
(cd "$(dirname "$prefix")" && COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs -czf "$(basename "$archive")" "$(basename "$prefix")")
(cd "$(dirname "$prefix")" && shasum -a 256 "$(basename "$archive")" > "$(basename "$archive").sha256" \
    && shasum -a 256 -c "$(basename "$archive").sha256")
roundtrip=$(mktemp -d "${TMPDIR:-/tmp}/iupac stage roundtrip XXXXXX")
trap 'rm -rf "$roundtrip"' EXIT
tar -C "$roundtrip" -xzf "$archive"
test -x "$roundtrip/$(basename "$prefix")/resources/chemistry/helper/iupac-analysis-helper"
test -x "$roundtrip/$(basename "$prefix")/IUPAC Synth 2.vst3/Contents/Resources/chemistry/java/bin/java"
ditto -c -k --sequesterRsrc --keepParent "$prefix" "$roundtrip/zip-check.zip"
ditto -x -k "$roundtrip/zip-check.zip" "$roundtrip/from-zip"
test -x "$roundtrip/from-zip/$(basename "$prefix")/IUPAC Synth 2.app/Contents/Resources/chemistry/helper/iupac-analysis-helper"
printf 'staged macOS arm64 product -> %s (archive %s)\n' "$prefix" "$archive"
