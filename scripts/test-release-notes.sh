#!/usr/bin/env bash
# Production-path test of scripts/release-notes.sh: pre-release and final (#61 `final: true`) notes
# from a fixture release directory; tag/argument validation; no hand-written or "stable" wording.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
gen="$repo_root/scripts/release-notes.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
expect_fail() { if "$@" >/dev/null 2>&1; then echo "expected failure: $*" >&2; exit 1; fi; }
absent() { if grep -q "$1" "$2"; then echo "unexpected '$1' in $2" >&2; exit 1; fi; }
sha=0123456789abcdef0123456789abcdef01234567
run=https://github.com/highda/iupac-synth-v2/actions/runs/1
mkdir -p "$work/rel"
printf '{"product":"IUPAC Synth 2","architecture":3,"chemistryEnabled":false}\n' > "$work/rel/macos-product.json"
printf '%s  IUPAC-Synth-2-Preview-linux-arm64.tar.gz\n%s  IUPAC-Synth-2-Preview-macos-arm64.tar.gz\n' \
    "$(printf 'a%.0s' {1..64})" "$(printf 'b%.0s' {1..64})" > "$work/rel/SHA256SUMS"

"$gen" v2.0.0-preview.1 "$sha" false "$work/rel" "$run" 'operator line' > "$work/pre.md"
grep -qx "Commit: $sha" "$work/pre.md"
grep -qx 'Architecture: 3' "$work/pre.md"
grep -q '^# IUPAC Synth 2 v2.0.0-preview.1 (pre-release)$' "$work/pre.md"
grep -q 'chemistry \*\*OFF\*\*' "$work/pre.md"
grep -q 'xattr -dr com.apple.quarantine' "$work/pre.md"
grep -q 'docs/human-test-plan.md' "$work/pre.md"
grep -q "| \`$(printf 'a%.0s' {1..64})\` | \`IUPAC-Synth-2-Preview-linux-arm64.tar.gz\` |" "$work/pre.md"
grep -q "$run" "$work/pre.md"
grep -q 'job `linux` of the run above' "$work/pre.md"
grep -q '^operator line$' "$work/pre.md"
if grep -qi 'stable' "$work/pre.md"; then echo 'unexpected stable wording' >&2; exit 1; fi
absent 'execution records' "$work/pre.md"

printf '{"product":"IUPAC Synth 2","architecture":3,"chemistryEnabled":true}\n' > "$work/rel/macos-product.json"
LINUX_RUN_URL=https://example.invalid/linux MACOS_RUN_URL=https://example.invalid/macos \
    "$gen" v2.0.0 "$sha" true "$work/rel" "$run" > "$work/final.md"
grep -q '^# IUPAC Synth 2 v2.0.0$' "$work/final.md"
absent 'pre-release' "$work/final.md"
grep -q 'chemistry \*\*ON\*\*' "$work/final.md"
grep -q 'execution records' "$work/final.md"
grep -q 'https://example.invalid/linux' "$work/final.md"
grep -q 'https://example.invalid/macos' "$work/final.md"
grep -qx "Commit: $sha" "$work/final.md"

expect_fail "$gen" v2.0.0 "$sha" false "$work/rel" "$run"
expect_fail "$gen" v2.0.0-preview.1 "$sha" true "$work/rel" "$run"
expect_fail "$gen" v2.0.0-preview.x "$sha" false "$work/rel" "$run"
expect_fail "$gen" v2.0.0-preview.1 abc false "$work/rel" "$run"
expect_fail "$gen" v2.0.0-preview.1 "$sha" maybe "$work/rel" "$run"
rm "$work/rel/macos-product.json"
expect_fail "$gen" v2.0.0-preview.1 "$sha" false "$work/rel" "$run"
echo 'release-notes generator test passed'
