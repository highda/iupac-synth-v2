#!/usr/bin/env bash
# Round-trip check of a published pre-release (#59), run on a Linux arm64 host with Docker and gh:
# download every asset from the Releases page, verify SHA256SUMS, run the product doctor and an
# offline name analysis on the Linux archive inside the minimal runtime image (no product baked in),
# and confirm the macOS archive still carries the validated bundle bytes and executable bits.
# Usage: prerelease-roundtrip.sh <tag> <full-sha> [work-dir]
set -euo pipefail

tag=${1:?tag required}
sha=${2:?full commit sha required}
work=${3:-$PWD/out/roundtrip}
repo=${RELEASE_REPO:-highda/iupac-synth-v2}
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
fail() { echo "prerelease-roundtrip: $*" >&2; exit 1; }

test ! -e "$work" || fail "work directory already exists: $work"
mkdir -p "$work/download" "$work/linux" "$work/macos"
work=$(cd "$work" && pwd)   # docker -v needs an absolute host path
gh release download "$tag" --repo "$repo" --dir "$work/download"
gh release view "$tag" --repo "$repo" --json targetCommitish,isPrerelease,isDraft,assets > "$work/release.json"
cat "$work/release.json"
jq -e '.isDraft == false' "$work/release.json" >/dev/null || fail 'release is a draft'
tag_sha=$(gh api "repos/$repo/git/ref/tags/$tag" --jq '.object.sha')
test "$tag_sha" = "$sha" || fail "tag $tag points at $tag_sha, expected $sha"

expected='IUPAC-Synth-2-Preview-linux-arm64.tar.gz
IUPAC-Synth-2-Preview-linux-arm64.tar.gz.sha256
IUPAC-Synth-2-Preview-macos-arm64.tar.gz
IUPAC-Synth-2-Preview-macos-arm64.tar.gz.sha256
SHA256SUMS
linux-delivery-report.json
linux-install-report.json
linux-v10-report.json
macos-artifacts.sha256
macos-auval.txt
macos-manifest.sha256
macos-pluginval-au.log
macos-pluginval-vst3.log
macos-product.json
macos-runner-identity.txt'
test "$(cd "$work/download" && LC_ALL=C ls -1)" = "$expected" || fail 'downloaded asset set differs from the expected attachment list'
(cd "$work/download" && sha256sum -c SHA256SUMS) || fail 'SHA256SUMS mismatch on downloaded files'
(cd "$work/download" && sha256sum -c IUPAC-Synth-2-Preview-linux-arm64.tar.gz.sha256 && sha256sum -c IUPAC-Synth-2-Preview-macos-arm64.tar.gz.sha256)
test "$(sha256sum "$work/download/IUPAC-Synth-2-Preview-linux-arm64.tar.gz" | cut -d ' ' -f 1)" = "$(jq -r .sha256 "$work/download/linux-delivery-report.json")" \
    || fail 'downloaded Linux archive differs from the V10-tested delivery hash'
jq -e --arg sha "$sha" '.commit == $sha' "$work/download/linux-delivery-report.json" >/dev/null || fail 'delivery report commit'

# Linux: product doctor and offline analysis inside the minimal runtime image, mounted read-only.
tar -xzf "$work/download/IUPAC-Synth-2-Preview-linux-arm64.tar.gz" -C "$work/linux"
test -d "$work/linux/IUPAC Synth 2 Preview" || fail 'Linux archive layout'
chmod -R a+rX "$work/linux"
docker build --platform linux/arm64 --target runtime-base -t iupac-runtime-base:roundtrip -f "$repo_root/packaging/ProductDockerfile" "$repo_root"
docker run --rm --network none --read-only --tmpfs /tmp:rw,noexec,nosuid,size=128m --user 65534:65534 \
    -v "$work/linux/IUPAC Synth 2 Preview:/opt/IUPAC Synth 2 Preview:ro" iupac-runtime-base:roundtrip \
    sh -c 'set -eu; test ! -e /usr/bin/python3; test ! -e /usr/bin/java;
           "/opt/IUPAC Synth 2 Preview/bin/iupac-product-doctor" "/opt/IUPAC Synth 2 Preview";
           "/opt/IUPAC Synth 2 Preview/bin/iupac-cli" analyze --mode name --text 2,2,2-trifluoroethan-1-ol' \
    | tee "$work/linux-roundtrip.log"
grep -q '"status":"ok"' "$work/linux-roundtrip.log" || fail 'product doctor did not report ok'
grep -q 'OCC(F)(F)F' "$work/linux-roundtrip.log" || fail 'offline name analysis did not resolve'

# macOS: archive bytes are the validated bundles; structure and executable bits survive the round trip.
tar -xzf "$work/download/IUPAC-Synth-2-Preview-macos-arm64.tar.gz" -C "$work/macos"
(cd "$work/macos" && sha256sum -c --quiet ../download/macos-artifacts.sha256) || fail 'macOS archive contents differ from validated artifacts'
for exe in 'IUPAC Synth 2.vst3/Contents/MacOS/IUPAC Synth 2' 'IUPAC Synth 2.component/Contents/MacOS/IUPAC Synth 2' \
           'IUPAC Synth 2.app/Contents/MacOS/IUPAC Synth 2' iupac-cli; do
    test -x "$work/macos/$exe" || fail "executable bit lost: $exe"
done
grep -q 'AU VALIDATION SUCCEEDED' "$work/download/macos-auval.txt"
printf '{"schemaVersion":1,"tag":"%s","commit":"%s","assets":%s,"sha256sums":"pass","linuxDoctor":"pass","linuxOfflineAnalysis":"pass","macosArchiveBytes":"pass","macosExecutableBits":"pass"}\n' \
    "$tag" "$sha" "$(jq '.assets | length' "$work/release.json")" > "$work/roundtrip-report.json"
cat "$work/roundtrip-report.json"
