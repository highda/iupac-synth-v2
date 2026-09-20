#!/usr/bin/env bash
# Unit test for the verdict and evidence-preservation helpers of the macOS V10
# analogue (#94). It sources packaging/verify-installed-macos.sh in library mode
# and drives the real production functions against fabricated pluginval
# transcripts, so nothing here reimplements the logic under test.
# The gate itself needs a staged product, a console session and sandbox-exec;
# this covers the decision that misread pluginval's program-list warning.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# shellcheck source=packaging/verify-installed-macos.sh
IUPAC_VERIFY_INSTALLED_MACOS_LIB=1 . "$repo_root/packaging/verify-installed-macos.sh"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/iupac v10 test XXXXXX")
trap 'rm -rf "$tmp"' EXIT
failures=0

check() {
    if [ "$2" = "$3" ]; then
        printf 'ok   %s\n' "$1"
    else
        printf 'FAIL %s: expected %s, got %s\n' "$1" "$3" "$2"; failures=$((failures + 1))
    fi
}

verdict() {  # <log> <exit status> -> prints the helper's exit status
    local status=0
    pluginval_verdict AU "$1" "$2" > "$tmp/verdict-out.txt" 2>&1 || status=$?
    printf '%s' "$status"
}

# The transcript the owner actually captured on 2026-09-19: pluginval's standard
# notice for a plugin without a program list, then SUCCESS.
cat > "$tmp/warning-only.log" <<'LOG'
Starting tests in: pluginval / Plugin state...
!!! WARNING: Current program is -1... Is this correct?
Completed tests in pluginval / Plugin state
SUCCESS
LOG
check "warning-only transcript passes" "$(verdict "$tmp/warning-only.log" 0)" 0
printf '!!! WARNING:\n\t"strictnessLevel" is deprecated.\nSUCCESS\n' > "$tmp/deprecated.log"
check "deprecation warning is not a failure" "$(verdict "$tmp/deprecated.log" 0)" 0

# Genuine failures, in each shape pluginval emits.
cat > "$tmp/unit-test-failure.log" <<'LOG'
Starting tests in: pluginval / Editor...
!!! WARNING: Current program is -1... Is this correct?
!!! Test 3 failed: Editor was not deleted
FAILURE
LOG
check "unit-test failure fails" "$(verdict "$tmp/unit-test-failure.log" 1)" 1

printf '*** FAILED: Timeout after 2 minutes\n' > "$tmp/timeout.log"
check "timeout failure fails" "$(verdict "$tmp/timeout.log" 1)" 1

printf 'Starting tests...\n3 tests failed!!!\n' > "$tmp/console-fail.log"
check "console failure summary fails" "$(verdict "$tmp/console-fail.log" 1)" 1

# A transcript without SUCCESS is never a pass, and neither is an empty one.
printf 'Starting tests in: pluginval / Editor...\n' > "$tmp/truncated.log"
check "missing SUCCESS fails" "$(verdict "$tmp/truncated.log" 0)" 1
: > "$tmp/empty.log"
check "empty transcript fails" "$(verdict "$tmp/empty.log" 0)" 1

# Documented host quirk: SUCCESS printed, then a segfault while exiting.
check "SUCCESS then non-zero exit passes" "$(verdict "$tmp/warning-only.log" 139)" 0
grep -q 'then exited 139' "$tmp/verdict-out.txt"
printf 'ok   tolerated non-zero exit is reported\n'
# ... but a non-zero exit without SUCCESS is still a failure.
check "non-zero exit without SUCCESS fails" "$(verdict "$tmp/truncated.log" 139)" 1

# A failing run must leave the logs the owner needs behind.
evidence=$tmp/work/evidence
mkdir -p "$evidence" "$tmp/work/pluginval-vst3" "$tmp/work/pluginval-au"
cp "$tmp/warning-only.log" "$evidence/pluginval-vst3.log"
cp "$tmp/unit-test-failure.log" "$evidence/pluginval-au.log"
printf 'AU VALIDATION SUCCEEDED\n' > "$evidence/auval.txt"
printf '<report/>\n' > "$tmp/work/pluginval-au/report.xml"
preserve_evidence "$tmp/report dir/v10-failed-evidence" \
    "$evidence" "$tmp/work/pluginval-vst3" "$tmp/work/pluginval-au" 2>/dev/null
rm -rf "$tmp/work"
for relative in evidence/pluginval-vst3.log evidence/pluginval-au.log evidence/auval.txt \
    pluginval-au/report.xml pluginval-vst3; do
    if [ -e "$tmp/report dir/v10-failed-evidence/$relative" ]; then
        printf 'ok   preserved %s\n' "$relative"
    else
        printf 'FAIL preserved %s missing\n' "$relative"; failures=$((failures + 1))
    fi
done

if [ "$failures" -ne 0 ]; then printf '%s check(s) failed\n' "$failures" >&2; exit 1; fi
printf 'verify-installed-macos.sh verdict and evidence helpers: all checks passed\n'
