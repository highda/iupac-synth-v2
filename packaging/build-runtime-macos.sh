#!/usr/bin/env bash
# Build the private macOS arm64 chemistry payload natively (#42): PyInstaller
# onedir freeze of the production helper from the checked-in spec, a jlink
# OpenJDK 17 image from the pinned target-native JDK using the same jdeps
# closure as the Debian payload, the pinned OPSIN JAR, the offline discovery
# snapshot/index and notices taken from the wheels and the JDK distribution
# (there is no /usr/share/doc on macOS). The result is the flat payload layout
# that packaging/stage-product-macos.sh splits into bundles.
# Usage: build-runtime-macos.sh [output] [work]
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${1:-$repo_root/out/iupac-chemistry-macos}"
work="${2:-$repo_root/build/package-macos}"
python="${PYTHON:-/opt/homebrew/opt/python@3.11/bin/python3.11}"
jdk="${JAVA_HOME:-/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home}"

[ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] || { echo 'build natively on macOS arm64' >&2; exit 1; }
for tool in "$python" "$jdk/bin/jdeps" "$jdk/bin/jlink"; do
    test -x "$tool" || { echo "missing pinned prerequisite: $tool (see docs/BUILDING.md)" >&2; exit 1; }
done
test -d "$jdk/jmods" || { echo "pinned JDK ships no jmods: $jdk" >&2; exit 1; }
test ! -e "$output" || { echo "output already exists: $output" >&2; exit 2; }
mkdir -p "$output" "$work"

# A clean venv, not the provisioning venv: the payload interpreter closure is
# exactly the hashed wheels, so no host site-packages can leak into the freeze.
rm -rf "$work/venv"
"$python" -m venv "$work/venv"
"$work/venv/bin/pip" install --quiet --require-hashes \
    -r "$repo_root/packaging/pyinstaller-requirements.txt" \
    -r "$repo_root/packaging/macos-chemistry-requirements.txt"
(
    cd "$repo_root/packaging"
    "$work/venv/bin/pyinstaller" --noconfirm --clean \
        --distpath "$work/dist" --workpath "$work/pyinstaller" iupac-analysis-helper.spec
)
cp -a "$work/dist/helper" "$output/helper"

modules="$("$jdk/bin/jdeps" --ignore-missing-deps --multi-release 17 --print-module-deps "$repo_root/third_party/opsin/opsin-cli-2.8.0.jar")"
"$jdk/bin/jlink" --module-path "$jdk/jmods" --add-modules "$modules" --strip-debug --no-header-files --no-man-pages --output "$output/java"

mkdir -p "$output/resources/discovery" "$output/notices"
cp "$repo_root/third_party/opsin/opsin-cli-2.8.0.jar" "$output/resources/opsin-cli-2.8.0.jar"
cp "$repo_root/data/discovery/discovery-v2.sqlite3" "$repo_root/data/discovery/manifest-v2.json" "$output/resources/discovery/"

# The flat payload states its own layout so bundle staging only has to rewrite
# the declaration, never rely on a case-insensitive filesystem (#42, D4).
cat > "$output/helper/payload-layout.json" <<'LAYOUT'
{
  "schemaVersion": 1,
  "layout": "flat",
  "opsinJar": "../resources/opsin-cli-2.8.0.jar",
  "javaExecutable": "../java/bin/java",
  "discoveryIndex": "../resources/discovery/discovery-v2.sqlite3"
}
LAYOUT

cp "$jdk/release" "$output/notices/openjdk-release"
cp "$jdk/legal/java.base/LICENSE" "$output/notices/openjdk-copyright"
cp "$("$work/venv/bin/python3" -c 'import sysconfig,pathlib;print(pathlib.Path(sysconfig.get_paths()["stdlib"])/"LICENSE.txt")')" \
    "$output/notices/python-copyright"
"$work/venv/bin/python3" - "$work/venv" "$output/notices" <<'PY'
"""Collect wheel licences from the installed dist-info, the macOS analogue of
Debian's /usr/share/doc/*/copyright."""
import shutil, sys
from pathlib import Path
venv, notices = Path(sys.argv[1]), Path(sys.argv[2])
site = next(venv.glob("lib/python3.*/site-packages"))
wanted = {"rdkit", "numpy", "pyinstaller", "pyinstaller_hooks_contrib"}
found = set()
for info in sorted(site.glob("*.dist-info")):
    name = info.name.split("-")[0].lower().replace("-", "_")
    name = {"rdkit_pypi": "rdkit"}.get(name, name)  # RDKit's former distribution name (#154)
    if name not in wanted:
        continue
    roots = list(info.glob("LICENSE*")) + list(info.glob("COPYING*")) \
        + list(info.glob("licenses")) + list(info.glob("license_files"))
    files = sorted({item for root in roots for item in ([root] if root.is_file() else root.rglob("*")) if item.is_file()})
    if not files:
        raise SystemExit(f"no licence material shipped in {info.name}")
    for item in files:
        flattened = "-".join(item.relative_to(info).parts)
        shutil.copy(item, notices / f"{name}-{flattened}")
    found.add(name)
missing = wanted - found
if missing:
    raise SystemExit(f"missing licence material for {sorted(missing)}")
PY
cp "$repo_root/docs/package-notices.md" "$output/notices/README.md"

# The unfrozen reference form resolves OPSIN through the pinned JDK explicitly;
# Homebrew's openjdk@17 is keg-only and never on PATH.
export IUPAC_JAVA_EXECUTABLE="$jdk/bin/java"
"$work/venv/bin/python3" "$repo_root/packaging/verify-runtime.py" \
    --source "$repo_root" --payload "$output" --report "$output/package-report.json"
printf 'macOS arm64 chemistry payload built and verified -> %s\n' "$output"
