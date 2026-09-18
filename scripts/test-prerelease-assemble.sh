#!/usr/bin/env bash
# Production-path test of scripts/prerelease-assemble.sh with synthetic artifact directories shaped
# like the package-arm64 and macos-arm64 uploads: a consistent pair assembles 15 files plus a
# self-consistent SHA256SUMS; a tampered archive, a wrong commit or a lost executable bit aborts.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
assemble="$repo_root/scripts/prerelease-assemble.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
expect_fail() { if "$@" >/dev/null 2>&1; then echo "expected failure: $*" >&2; exit 1; fi; }
sha=0123456789abcdef0123456789abcdef01234567

make_fixture() {
    local root=$1
    rm -rf "$root"; mkdir -p "$root/linux/product" "$root/macos/artifacts"
    # Linux delivery as exported by ProductDockerfile + package-arm64.yml
    mkdir -p "$root/stage/IUPAC Synth 2 Preview/bin"
    printf 'linux product\n' > "$root/stage/IUPAC Synth 2 Preview/bin/iupac-cli"
    tar -C "$root/stage" -czf "$root/linux/product/IUPAC Synth 2 Preview.tar.gz" 'IUPAC Synth 2 Preview'
    (cd "$root/linux/product" && sha256sum 'IUPAC Synth 2 Preview.tar.gz' > 'IUPAC Synth 2 Preview.tar.gz.sha256')
    printf '{"schemaVersion":1,"product":"IUPAC Synth 2","architecture":3}\n' > "$root/linux/product/install-report.json"
    printf '{"schemaVersion":1,"status":"pass"}\n' > "$root/linux/v10-report.json"
    printf '{"schemaVersion":1,"commit":"%s","testedBy":"v10-report.json","archive":"IUPAC Synth 2 Preview.tar.gz","sha256":"%s"}\n' \
        "$sha" "$(sha256sum "$root/linux/product/IUPAC Synth 2 Preview.tar.gz" | cut -d ' ' -f 1)" > "$root/linux/delivery-report.json"
    # macOS validation output as written by scripts/macos-validate.sh + macos-arm64.yml
    local m="$root/macos"
    for bundle in 'IUPAC Synth 2.vst3' 'IUPAC Synth 2.component' 'IUPAC Synth 2.app'; do
        mkdir -p "$m/artifacts/$bundle/Contents/MacOS"
        printf '#!/bin/sh\necho %s\n' "$bundle" > "$m/artifacts/$bundle/Contents/MacOS/IUPAC Synth 2"
        chmod 755 "$m/artifacts/$bundle/Contents/MacOS/IUPAC Synth 2"
        printf 'plist\n' > "$m/artifacts/$bundle/Contents/Info.plist"
    done
    printf '#!/bin/sh\necho cli\n' > "$m/artifacts/iupac-cli"; chmod 755 "$m/artifacts/iupac-cli"
    (cd "$m/artifacts" && find . -type f -print0 | sort -z | xargs -0 sha256sum > ../artifacts.sha256)
    (cd "$m/artifacts" && tar -czf ../IUPAC-Synth-2-Preview-macos-arm64.tar.gz 'IUPAC Synth 2.vst3' 'IUPAC Synth 2.component' 'IUPAC Synth 2.app' iupac-cli)
    (cd "$m" && sha256sum IUPAC-Synth-2-Preview-macos-arm64.tar.gz > IUPAC-Synth-2-Preview-macos-arm64.tar.gz.sha256)
    printf 'AU VALIDATION SUCCEEDED.\n' > "$m/auval.txt"
    printf 'pluginval vst3\n' > "$m/pluginval-vst3.log"; printf 'pluginval au\n' > "$m/pluginval-au.log"
    printf '{"product":"IUPAC Synth 2","architecture":3,"chemistryEnabled":false}\n' > "$m/product.json"
    printf 'macOS 26 runner\n' > "$m/runner-identity.txt"
    printf '%s\n' "$sha" > "$m/head-commit.txt"; printf '%s\n' "$sha" > "$m/commit.txt"
    (cd "$m" && find . -type f -print0 | sort -z | xargs -0 sha256sum > ../manifest.sha256 && mv ../manifest.sha256 manifest.sha256)
}

make_fixture "$work/good"
"$assemble" "$work/good/linux" "$work/good/macos" "$work/good/out" "$sha"
test "$(find "$work/good/out" -maxdepth 1 -type f | wc -l | tr -d ' ')" = 15
(cd "$work/good/out" && sha256sum -c --quiet SHA256SUMS)
test "$(wc -l < "$work/good/out/SHA256SUMS" | tr -d ' ')" = 14
cmp "$work/good/linux/product/IUPAC Synth 2 Preview.tar.gz" "$work/good/out/IUPAC-Synth-2-Preview-linux-arm64.tar.gz"
cmp "$work/good/macos/IUPAC-Synth-2-Preview-macos-arm64.tar.gz" "$work/good/out/IUPAC-Synth-2-Preview-macos-arm64.tar.gz"
(cd "$work/good/out" && sha256sum -c --quiet IUPAC-Synth-2-Preview-linux-arm64.tar.gz.sha256)
expect_fail "$assemble" "$work/good/linux" "$work/good/macos" "$work/good/out" "$sha"   # refuses an existing output dir

make_fixture "$work/tamper-linux"
printf 'x' >> "$work/tamper-linux/linux/product/IUPAC Synth 2 Preview.tar.gz"
expect_fail "$assemble" "$work/tamper-linux/linux" "$work/tamper-linux/macos" "$work/tamper-linux/out" "$sha"

make_fixture "$work/wrong-commit"
expect_fail "$assemble" "$work/wrong-commit/linux" "$work/wrong-commit/macos" "$work/wrong-commit/out" "$(printf 'f%.0s' {1..40})"

# Re-pack the macOS archive from an altered copy while the validated artifacts/ directory, sidecar
# and manifest stay self-consistent: only the archive-contents-versus-artifacts.sha256 check can catch it.
repack_macos() {
    local m=$1 alter=$2 copy
    copy=$(mktemp -d); cp -R "$m/artifacts/." "$copy/"
    (cd "$copy" && eval "$alter")
    (cd "$copy" && tar -czf "$m/IUPAC-Synth-2-Preview-macos-arm64.tar.gz" 'IUPAC Synth 2.vst3' 'IUPAC Synth 2.component' 'IUPAC Synth 2.app' iupac-cli)
    rm -rf "$copy" "$m/manifest.sha256"
    (cd "$m" && sha256sum IUPAC-Synth-2-Preview-macos-arm64.tar.gz > IUPAC-Synth-2-Preview-macos-arm64.tar.gz.sha256 \
        && find . -type f -print0 | sort -z | xargs -0 sha256sum > ../manifest.sha256 && mv ../manifest.sha256 manifest.sha256)
}
make_fixture "$work/tamper-macos"
repack_macos "$work/tamper-macos/macos" "printf x >> iupac-cli"
expect_fail "$assemble" "$work/tamper-macos/linux" "$work/tamper-macos/macos" "$work/tamper-macos/out" "$sha"

make_fixture "$work/no-exec"
repack_macos "$work/no-exec/macos" "chmod 644 iupac-cli"
if "$assemble" "$work/no-exec/linux" "$work/no-exec/macos" "$work/no-exec/out" "$sha" > "$work/no-exec.log" 2>&1; then exit 1; fi
grep -q 'executable bit lost in archive: iupac-cli' "$work/no-exec.log"

make_fixture "$work/v10-fail"
printf '{"schemaVersion":1,"status":"fail"}\n' > "$work/v10-fail/linux/v10-report.json"
expect_fail "$assemble" "$work/v10-fail/linux" "$work/v10-fail/macos" "$work/v10-fail/out" "$sha"
echo 'prerelease-assemble test passed'
