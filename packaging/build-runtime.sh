#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${1:-$repo_root/out/package}"
work="${2:-$repo_root/build/package}"
python="${PYTHON:-python3}"
jdk="${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-$(dpkg --print-architecture)}"

test ! -e "$output" || { echo "output already exists: $output" >&2; exit 2; }
mkdir -p "$output" "$work"
"$python" -m venv --system-site-packages "$work/venv"
"$work/venv/bin/pip" install --require-hashes -r "$repo_root/packaging/pyinstaller-requirements.txt"
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
cp "$repo_root/data/discovery/discovery-v1.sqlite3" "$repo_root/data/discovery/manifest-v1.json" "$output/resources/discovery/"
cp "$jdk/release" "$output/notices/openjdk-release"
cp /usr/share/doc/openjdk-17-jre-headless/copyright "$output/notices/openjdk-copyright"
cp /usr/share/doc/python3.11-minimal/copyright "$output/notices/python-copyright"
cp /usr/share/doc/python3-rdkit/copyright "$output/notices/rdkit-copyright"
cp /usr/share/doc/python3-numpy/copyright "$output/notices/numpy-copyright"
cp "$work/venv/lib/python3.11/site-packages/pyinstaller-6.22.3.dist-info/licenses/COPYING.txt" "$output/notices/pyinstaller-copying.txt"
cp "$work/venv/lib/python3.11/site-packages/pyinstaller_hooks_contrib-2026.7.dist-info/licenses/LICENSE" "$output/notices/pyinstaller-hooks-license.txt"
cp "$repo_root/docs/package-notices.md" "$output/notices/README.md"

"$repo_root/packaging/verify-runtime.py" --source "$repo_root" --payload "$output" --report "$output/package-report.json"
