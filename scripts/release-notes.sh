#!/usr/bin/env bash
# Generate GitHub release notes for prerelease.yml (#59). All facts come from arguments and the
# assembled release directory; nothing is hand-written. Usage:
#   release-notes.sh <tag> <full-sha> <final:true|false> <release-dir> <prerelease-run-url> [operator-notes]
# <release-dir> must contain SHA256SUMS and macos-product.json produced by prerelease-assemble.sh.
# Source workflow run URLs are read from LINUX_RUN_URL / MACOS_RUN_URL (blank when the delivery was
# built inside the pre-release run itself, which is the workflow_call path).
set -euo pipefail

tag=${1:?tag required}
sha=${2:?full commit sha required}
final=${3:?final true|false required}
dir=${4:?release directory required}
run_url=${5:?pre-release workflow run url required}
notes=${6:-}
repo=${RELEASE_REPO:-highda/iupac-synth-v2}
linux_run=${LINUX_RUN_URL:-}
macos_run=${MACOS_RUN_URL:-}

[[ "$sha" =~ ^[0-9a-f]{40}$ ]] || { echo "release-notes: not a full sha: $sha" >&2; exit 1; }
case "$final" in
    true) [[ "$tag" =~ ^v2\.0\.0$ ]] || { echo "release-notes: final release tag must be v2.0.0, got $tag" >&2; exit 1; } ;;
    false) [[ "$tag" =~ ^v2\.0\.0-preview\.[0-9]+$ ]] || { echo "release-notes: pre-release tag must match v2.0.0-preview.N, got $tag" >&2; exit 1; } ;;
    *) echo "release-notes: final must be true or false" >&2; exit 1 ;;
esac
test -f "$dir/SHA256SUMS" || { echo "release-notes: missing $dir/SHA256SUMS" >&2; exit 1; }
test -f "$dir/macos-product.json" || { echo "release-notes: missing $dir/macos-product.json" >&2; exit 1; }
for name in IUPAC-Synth-2-Preview-linux-arm64.tar.gz IUPAC-Synth-2-Preview-macos-arm64.tar.gz; do
    grep -q " $name\$" "$dir/SHA256SUMS" || { echo "release-notes: $name absent from SHA256SUMS" >&2; exit 1; }
done
if jq -e '.chemistryEnabled == true' "$dir/macos-product.json" >/dev/null; then
    macos_chemistry='chemistry **ON** — private bundled chemistry payload included; name/SMILES analysis works offline'
elif jq -e '.chemistryEnabled == false' "$dir/macos-product.json" >/dev/null; then
    macos_chemistry='chemistry **OFF** — manual synthesizer only, no chemistry payload (the macOS payload arrives with [#42](https://github.com/'"$repo"'/issues/42)); the Linux archive is the only complete product in this release'
else
    echo 'release-notes: macos-product.json has no boolean chemistryEnabled' >&2; exit 1
fi
blob="https://github.com/$repo/blob/$sha"
ledger="$blob/docs/human-test-plan.md"

if [ "$final" = true ]; then
    printf '# IUPAC Synth 2 %s\n\n' "$tag"
    printf 'Release of the CI-tested Linux arm64 and macOS arm64 artifacts built at one commit.\n\n'
else
    printf '# IUPAC Synth 2 %s (pre-release)\n\n' "$tag"
    printf 'Preview for human acceptance testing only. Every file below is the exact CI-tested artifact for the commit named here; nothing in this pre-release is a finished or supported release.\n\n'
fi
printf 'Commit: %s\n' "$sha"
printf 'Architecture: 3\n'
printf 'Tag: `%s` (points at the commit above)\n\n' "$tag"

printf '## Source workflow runs\n\n'
printf -- '- Pre-release workflow (build, hash verification, publication): %s\n' "$run_url"
if [ -n "$linux_run" ]; then printf -- '- Linux arm64 `package-arm64` (delivery + V10 installed-runtime proof): %s\n' "$linux_run"; else printf -- '- Linux arm64 `package-arm64` (delivery + V10 installed-runtime proof): job `linux` of the run above\n'; fi
if [ -n "$macos_run" ]; then printf -- '- macOS arm64 `macos-arm64` (pluginval strictness 5, auval, editor and Standalone smoke): %s\n' "$macos_run"; else printf -- '- macOS arm64 `macos-arm64` (pluginval strictness 5, auval, editor and Standalone smoke): job `macos` of the run above\n'; fi
printf '\n'

printf '## What each platform contains\n\n'
printf -- '- **Linux arm64** (`IUPAC-Synth-2-Preview-linux-arm64.tar.gz`, byte-identical to the tested `IUPAC Synth 2 Preview.tar.gz` named in `linux-delivery-report.json`): the full chemistry-ON product — Standalone, VST3, `iupac-cli`, product doctor and the private bundled Python/RDKit and Java/OPSIN runtimes. V10 was proven on this exact archive in the isolated minimal Debian 12 runtime with networking disabled (`linux-v10-report.json`). Tested OS: Debian 12 arm64.\n'
printf -- '- **macOS arm64** (`IUPAC-Synth-2-Preview-macos-arm64.tar.gz`): VST3, AUv2 component, Standalone app and `iupac-cli`, ad-hoc signed, arm64 only, deployment target macOS 12; %s. pluginval strictness 5 (VST3 and AU) and `auval -v aumu Iup2 Iups` passed on the GitHub `macos-26` runner (`macos-pluginval-*.log`, `macos-auval.txt`). Not notarized, no installer.\n\n' "$macos_chemistry"

printf '## Install paths\n\n'
printf '| Platform | Format | Where |\n| --- | --- | --- |\n'
printf '| Linux | Standalone | extract, run `IUPAC Synth 2 Preview/bin/IUPAC Synth 2` |\n'
printf '| Linux | VST3 | copy `IUPAC Synth 2 Preview/lib/vst3/IUPAC Synth 2.vst3` (whole bundle, it carries its chemistry payload) into `~/.vst3/` |\n'
printf '| Linux | CLI / doctor | `IUPAC Synth 2 Preview/bin/iupac-cli`, `IUPAC Synth 2 Preview/bin/iupac-product-doctor` |\n'
printf '| macOS | VST3 | `IUPAC Synth 2.vst3` → `~/Library/Audio/Plug-Ins/VST3/` |\n'
printf '| macOS | AUv2 | `IUPAC Synth 2.component` → `~/Library/Audio/Plug-Ins/Components/` (then `auval -v aumu Iup2 Iups`) |\n'
printf '| macOS | Standalone | `IUPAC Synth 2.app` → `/Applications/` or any folder |\n'
printf '| macOS | CLI | `iupac-cli` anywhere on `PATH`; run without arguments for product identity |\n\n'

printf '## Unsigned download on macOS\n\n'
printf 'The macOS bundles are ad-hoc signed and not notarized, so Gatekeeper marks the download as quarantined. After extracting, clear the quarantine attribute before the first launch or plugin scan:\n\n'
printf '```sh\ntar -xzf IUPAC-Synth-2-Preview-macos-arm64.tar.gz\nxattr -dr com.apple.quarantine "IUPAC Synth 2.vst3" "IUPAC Synth 2.component" "IUPAC Synth 2.app" iupac-cli\n```\n\n'

printf '## Verify the download\n\n'
printf '```sh\ngh release download %s --repo %s --dir iupac-%s\ncd iupac-%s && sha256sum -c SHA256SUMS   # macOS: shasum -a 256 -c SHA256SUMS\n```\n\n' "$tag" "$repo" "$tag" "$tag"
printf '| SHA-256 | File |\n| --- | --- |\n'
while read -r hash name; do printf '| `%s` | `%s` |\n' "$hash" "$name"; done < "$dir/SHA256SUMS"
printf '\n'

printf '## Human acceptance\n\n'
printf 'Human ledger: %s — record `%s` and the `SHA256SUMS` hash as the candidate build identifier before executing a row.\n' "$ledger" "$tag"
if [ "$final" = true ]; then
    printf 'Human ledger execution records for this release (appended per case under the shared setup): %s#shared-setup-and-evidence-record\n' "$ledger"
fi
if [ -n "$notes" ]; then printf '\n## Operator note\n\n%s\n' "$notes"; fi
