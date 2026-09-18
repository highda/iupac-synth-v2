#!/usr/bin/env bash
# Assemble the release attachments for prerelease.yml (#59) from the retained Linux arm64 and
# macOS arm64 workflow artifacts built at one commit. Every file is hash-verified against the
# tested reports/manifests before it is copied; any mismatch aborts. Release asset names carry no
# spaces because GitHub rewrites spaces in asset names, so the tested Linux archive is attached
# byte-identical under a dash-separated name with a sidecar regenerated for that name.
set -euo pipefail

linux_in=${1:?linux artifact directory required}
macos_in=${2:?macos artifact directory required}
out=${3:?output directory required}
sha=${4:?expected full commit sha required}

sha256() { sha256sum "$1" | cut -d ' ' -f 1; }
fail() { echo "prerelease-assemble: $*" >&2; exit 1; }
[[ "$sha" =~ ^[0-9a-f]{40}$ ]] || fail "not a full sha: $sha"
test ! -e "$out" || fail "output directory already exists: $out"
mkdir -p "$out"
# Absolute inputs: later checks run inside subshells that have changed directory.
linux_in=$(cd "$linux_in" && pwd)
macos_in=$(cd "$macos_in" && pwd)

# --- Linux arm64: exact tested delivery (package-arm64.yml) ---
linux_archive="$linux_in/product/IUPAC Synth 2 Preview.tar.gz"
test -f "$linux_archive" || fail "missing $linux_archive"
(cd "$linux_in/product" && sha256sum -c 'IUPAC Synth 2 Preview.tar.gz.sha256') || fail 'Linux sidecar mismatch'
jq -e --arg sha "$sha" '.commit == $sha' "$linux_in/delivery-report.json" >/dev/null || fail 'delivery-report.json commit differs from release commit'
jq -e '.testedBy == "v10-report.json" and .archive == "IUPAC Synth 2 Preview.tar.gz"' "$linux_in/delivery-report.json" >/dev/null || fail 'delivery-report.json shape'
jq -e '.status == "pass"' "$linux_in/v10-report.json" >/dev/null || fail 'v10-report.json is not a pass'
linux_hash=$(sha256 "$linux_archive")
test "$linux_hash" = "$(jq -r .sha256 "$linux_in/delivery-report.json")" || fail 'Linux archive hash differs from delivery-report.json'
cp "$linux_archive" "$out/IUPAC-Synth-2-Preview-linux-arm64.tar.gz"
cp "$linux_in/delivery-report.json" "$out/linux-delivery-report.json"
cp "$linux_in/v10-report.json" "$out/linux-v10-report.json"
cp "$linux_in/product/install-report.json" "$out/linux-install-report.json"
(cd "$out" && sha256sum IUPAC-Synth-2-Preview-linux-arm64.tar.gz > IUPAC-Synth-2-Preview-linux-arm64.tar.gz.sha256)
test "$(sha256 "$out/IUPAC-Synth-2-Preview-linux-arm64.tar.gz")" = "$linux_hash" || fail 'Linux copy changed'

# --- macOS arm64: exact validated bundles (macos-arm64.yml) ---
(cd "$macos_in" && sha256sum -c --quiet manifest.sha256) || fail 'macOS manifest.sha256 mismatch'
test "$(tr -d '[:space:]' < "$macos_in/head-commit.txt")" = "$sha" || fail 'macOS head-commit.txt differs from release commit'
# head-commit.txt is the release commit the run was asked for; commit.txt is the HEAD of the
# checkout that actually built these bundles. macos-arm64.yml checks out the release commit, so
# both carry it. The #85 local stand-in builds from a `git archive` export of that commit whose
# one-commit history is synthetic (scripts/local-lib.sh), so there commit.txt records that export
# commit and head-commit.txt alone names the release commit. runner-identity.txt tells the two
# apart and is covered by the manifest verified immediately above.
macos_commit=$(tr -d '[:space:]' < "$macos_in/commit.txt")
if grep -q '^local-recipe=' "$macos_in/runner-identity.txt"; then
    [[ "$macos_commit" =~ ^[0-9a-f]{40}$ ]] || fail 'macOS commit.txt is not a commit sha'
else
    test "$macos_commit" = "$sha" || fail 'macOS commit.txt differs from release commit'
fi
(cd "$macos_in/artifacts" && sha256sum -c --quiet ../artifacts.sha256) || fail 'macOS artifacts.sha256 mismatch'
macos_archive="$macos_in/IUPAC-Synth-2-Preview-macos-arm64.tar.gz"
test -f "$macos_archive" || fail "missing $macos_archive"
(cd "$macos_in" && sha256sum -c --quiet IUPAC-Synth-2-Preview-macos-arm64.tar.gz.sha256) || fail 'macOS sidecar mismatch'
grep -q " ./IUPAC-Synth-2-Preview-macos-arm64.tar.gz\$" "$macos_in/manifest.sha256" || fail 'macOS archive absent from manifest'
grep -q 'AU VALIDATION SUCCEEDED' "$macos_in/auval.txt" || fail 'auval did not succeed'
jq -e '.product == "IUPAC Synth 2" and .architecture == 3' "$macos_in/product.json" >/dev/null || fail 'macOS product.json identity'
# The archive contents must be the validated bundles byte for byte.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
tar -xzf "$macos_archive" -C "$scratch"
(cd "$scratch" && sha256sum -c --quiet "$macos_in/artifacts.sha256") || fail 'macOS archive contents differ from artifacts.sha256'
for exe in 'IUPAC Synth 2.vst3/Contents/MacOS/IUPAC Synth 2' 'IUPAC Synth 2.component/Contents/MacOS/IUPAC Synth 2' \
           'IUPAC Synth 2.app/Contents/MacOS/IUPAC Synth 2' iupac-cli; do
    test -x "$scratch/$exe" || fail "executable bit lost in archive: $exe"
done
cp "$macos_archive" "$macos_archive.sha256" "$out/"
cp "$macos_in/manifest.sha256" "$out/macos-manifest.sha256"
cp "$macos_in/artifacts.sha256" "$out/macos-artifacts.sha256"
cp "$macos_in/pluginval-vst3.log" "$out/macos-pluginval-vst3.log"
cp "$macos_in/pluginval-au.log" "$out/macos-pluginval-au.log"
cp "$macos_in/auval.txt" "$out/macos-auval.txt"
cp "$macos_in/product.json" "$out/macos-product.json"
cp "$macos_in/runner-identity.txt" "$out/macos-runner-identity.txt"

# --- one manifest over every attached file ---
(cd "$out" && LC_ALL=C sha256sum -- * > SHA256SUMS && sha256sum -c --quiet SHA256SUMS)
echo "assembled $(find "$out" -maxdepth 1 -type f | wc -l) release files in $out"
