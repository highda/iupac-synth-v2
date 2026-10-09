#!/usr/bin/env bash
# macOS analogue of the Linux V10 installed-formats gate (#42): drive the
# installed CLI, helper, Standalone and plugins of a staged prefix with no
# network and no reachable external interpreter, audit every Mach-O for
# developer-path and interpreter leakage, prove relocation to a read-only path
# with spaces, and record cold start, sizes and per-format hashes.
# AU hosting (auval, pluginval on the .component) resolves through the console
# session's AudioComponentRegistrar, so it is only executed when this account owns
# the console session; otherwise it is reported pending, never passed.
# Usage: verify-installed-macos.sh <prefix> <report> <pluginval>
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

# ---------------------------------------------- verdict and evidence helpers
# Defined before any argument handling so packaging/test-verify-installed-macos.sh
# can source this file (IUPAC_VERIFY_INSTALLED_MACOS_LIB=1) and exercise the real
# production functions instead of a copy of them.

# pluginval's own failure markers, taken from the pinned source:
#   "!!! Test <n> failed[: ...]"     juce::UnitTestRunner::addFail
#   "*** FAILED[: Timeout after ...]" Validator.cpp timeout / CommandLine.cpp child exit
#   "FAILURE"                         Validator.cpp runTests verdict on any failure
#   "<n> tests failed!!!"             juce::ConsoleApplication::fail
# Everything else pluginval prefixes with "!!!" is a warning, including
#   "!!! WARNING: Current program is -1... Is this correct?"
# which it emits for every plugin without a program list -- the AU path prints it
# and the VST3 path does not, which is the whole of #94. Warnings never fail the
# gate; a "!!!" line that is not a warning still does.
pluginval_failure_lines() {
    grep -nE '^\*\*\* FAILED|^FAILURE$|^!!!|tests failed!!!' "$1" | grep -vE '^[0-9]+:!!! WARNING' || true
}

# The transcript is the verdict: pluginval on this host has been seen printing
# SUCCESS and then segfaulting while exiting (#85 gotcha), so a non-zero exit is a
# failure unless the log carries ^SUCCESS with no failure marker, and that
# tolerated exit is reported rather than hidden.
pluginval_verdict() {
    local label=$1 log=$2 status=$3 failures
    if [ ! -s "$log" ]; then
        echo "pluginval wrote no transcript for the installed $label (exit $status)" >&2
        return 1
    fi
    failures=$(pluginval_failure_lines "$log")
    if [ -n "$failures" ]; then
        echo "pluginval reported a failure on the installed $label:" >&2
        printf '%s\n' "$failures" >&2
        return 1
    fi
    if ! grep -q '^SUCCESS' "$log"; then
        echo "pluginval did not report SUCCESS on the installed $label (exit $status)" >&2
        return 1
    fi
    if [ "$status" != 0 ]; then
        echo "note: pluginval reported SUCCESS on the installed $label then exited $status" >&2
    fi
    return 0
}

# Copy the collected evidence somewhere durable before the work directory's EXIT
# trap removes it, so a failing run leaves the owner the auval and pluginval logs.
preserve_evidence() {
    local dest=$1; shift
    rm -rf "$dest"
    mkdir -p "$dest" || return 0
    local src
    for src in "$@"; do
        [ -e "$src" ] || continue
        cp -R "$src" "$dest/" 2>/dev/null || true
    done
    echo "evidence preserved in $dest" >&2
}

if [ "${IUPAC_VERIFY_INSTALLED_MACOS_LIB:-0}" = 1 ]; then
    return 0
fi

prefix=${1:?installed prefix required}
report=${2:?report path required}
pluginval=${3:-$repo_root/build/pluginval/pluginval_artefacts/Release/pluginval.app/Contents/MacOS/pluginval}
provisioning=${IUPAC_PROVISIONING_ROOT:-/Users/Shared/iupac-macos-build}

[ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ]
test -x "$pluginval"
# The isolation profile denies the whole checkout, and realpath cannot even
# traverse a denied ancestor, so an in-tree prefix could never be exercised
# honestly. Stage outside the checkout (the workflow uses RUNNER_TEMP).
case "$prefix/" in "$repo_root"/*) echo "stage the product outside the checkout: $prefix" >&2; exit 2;; esac
mkdir -p "$(dirname "$report")"
report_dir=$(cd "$(dirname "$report")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/iupac v10 macos XXXXXX")
mkdir -p "$work/evidence"
evidence=$work/evidence
on_exit() {
    local status=$?
    if [ "$status" -ne 0 ]; then
        preserve_evidence "$report_dir/v10-failed-evidence" \
            "$evidence" "$work/pluginval-vst3" "$work/pluginval-au"
    fi
    rm -rf "$work"
    exit "$status"
}
trap on_exit EXIT

# The isolation profile denies reading the source checkout, so everything the
# sandboxed body needs lives outside it.
cp "$pluginval" "$work/pluginval"
cp "$repo_root/packaging/macos-isolation.sb" "$work/isolation.sb"

component="$prefix/IUPAC Synth 2.component"
app="$prefix/IUPAC Synth 2.app"

# ---------------------------------------------------------------- Mach-O audit
# Only the OS baseline and package-relative references are allowed. Runs outside
# the sandbox because it reads the package, not the product's runtime behaviour.
: > "$evidence/otool.txt"
while IFS= read -r binary; do
    printf '=== %s\n' "${binary#"$prefix"/}" >> "$evidence/otool.txt"
    otool -L "$binary" | tail -n +2 >> "$evidence/otool.txt"
done < <(find "$prefix" -type f -perm -u+x -exec sh -c 'file -b "$1" | grep -q Mach-O && echo "$1"' sh {} \;)
if grep -E '^\s+(/|@)' "$evidence/otool.txt" \
    | grep -vE '^\s+(/usr/lib/|/System/Library/|@rpath/|@loader_path/|@executable_path/)' > "$evidence/otool-violations.txt"; then
    echo 'Mach-O references a path outside the OS baseline and the package:' >&2
    cat "$evidence/otool-violations.txt" >&2
    exit 1
fi
# Named files only: the doctor script necessarily contains these patterns itself.
if grep -al -e /opt/homebrew -e /Users/Shared -e "$repo_root" \
    "$prefix/iupac-cli" "$prefix/install-report.json" \
    "$prefix/resources/chemistry/helper/payload-layout.json" >/dev/null 2>&1; then
    echo 'developer, Homebrew or provisioning path recorded in the delivery' >&2; exit 1
fi

# ------------------------------------------------------------- sandboxed proof
cat > "$work/inner.sh" <<'INNER'
#!/bin/sh
# POSIX shell only, and nothing outside /bin, /usr/bin, the package and $work:
# the profile denies Homebrew, /usr/local, the checkout and the provisioning venv.
set -eu
# C locale: /usr/bin/time -p writes a decimal comma under a comma locale.
export LC_ALL=C LANG=C
prefix=$1; evidence=$2; work=$3
export TMPDIR="$work/tmp"; mkdir -p "$TMPDIR"
"$prefix/iupac-product-doctor" "$prefix" > "$evidence/doctor.json"
# First name analysis of this package inside this sandbox. It is only a genuine
# first-ever execution when the package was freshly written; a package already
# exercised on this host reports the warm figure, so the report names it precisely
# and carries the measured first-write cost as a stated limit instead.
/usr/bin/time -p "$prefix/iupac-cli" analyze --mode name --text 2,2,2-trifluoroethan-1-ol \
    > "$evidence/name.json" 2> "$evidence/cold-start.txt"
DYLD_PRINT_LIBRARIES=1 "$prefix/iupac-cli" analyze --mode smiles --text 'CCN(CC)C(=O)c1ccc(Cl)cc1' \
    > "$evidence/unseen-analysis.json" 2> "$evidence/dyld-print-libraries.txt"
"$prefix/iupac-cli" generate --mode smiles --text 'CCN(CC)C(=O)c1ccc(Cl)cc1' > "$work/unseen.iupacpatch"
cp "$work/unseen.iupacpatch" "$evidence/unseen.iupacpatch"
"$prefix/iupac-cli" verify-panel --panel "$prefix/share/iupac-synth-2/panels/authored-synth/panel.json" \
    --output-dir "$work/panel" > "$evidence/panel.json"
"$prefix/iupac-cli" render --snapshot "$work/unseen.iupacpatch" \
    --midi "$prefix/share/iupac-synth-2/factory/held-note.midi.json" --sample-rate 48000 --block-size 128 \
    --output "$work/render.wav" > "$evidence/render.json"
# Save and restore: the generated snapshot round-trips through the shipped State
# path and renders identically.
"$prefix/iupac-cli" inspect --snapshot "$work/unseen.iupacpatch" --stage effective > "$evidence/effective.json"
"$prefix/iupac-cli" render --snapshot "$work/unseen.iupacpatch" \
    --midi "$prefix/share/iupac-synth-2/factory/held-note.midi.json" --sample-rate 48000 --block-size 128 \
    --output "$work/restored.wav" > "$evidence/restored.json"
/usr/bin/shasum -a 256 "$work/render.wav" "$work/restored.wav" > "$evidence/render-hashes.txt"
"$prefix/iupac-cli" > "$evidence/product.json"
# Bounded Standalone launch through the installed app, with its own payload.
"$app_main" > "$evidence/standalone.log" 2>&1 &
standalone=$!
/bin/sleep 8
/bin/kill -0 "$standalone"
/bin/kill -TERM "$standalone"
wait "$standalone" || true
# Exit status is captured, not asserted here: the outer script is the judge.
pluginval_status=0
"$work/pluginval" --validate "$prefix/IUPAC Synth 2.vst3" --strictness-level 5 \
    --output-dir "$work/pluginval-vst3" > "$evidence/pluginval-vst3.log" 2>&1 || pluginval_status=$?
printf '%s\n' "$pluginval_status" > "$evidence/pluginval-vst3.status"
printf 'sandboxed installed-format proof complete\n'
INNER
chmod +x "$work/inner.sh"

app_main="$app/Contents/MacOS/IUPAC Synth 2"
export app_main work
# Run from $work: the profile denies the checkout, so even the shell's own cwd
# must sit outside it.
(cd "$work" && sandbox-exec -f "$work/isolation.sb" -D "source-tree=$repo_root" \
    -D "provisioning=$provisioning" \
    /bin/sh "$work/inner.sh" "$prefix" "$evidence" "$work") | tee "$evidence/sandbox.log"

grep -q 'OCC(F)(F)F' "$evidence/name.json"
grep -q 'CCN(CC)C(=O)c1ccc(Cl)cc1' "$evidence/unseen.iupacpatch"
grep -q '"status": *"ok"' "$evidence/doctor.json" || grep -q '"status":"ok"' "$evidence/doctor.json"
grep -q '"chemistryEnabled": *true' "$evidence/product.json" || grep -q '"chemistryEnabled":true' "$evidence/product.json"
vst3_pluginval_status=$(cat "$evidence/pluginval-vst3.status")
pluginval_verdict VST3 "$evidence/pluginval-vst3.log" "$vst3_pluginval_status"
# No external interpreter or Homebrew library was ever mapped during a real analysis.
if grep -Ei 'homebrew|/usr/local/|Python\.framework|Python3\.framework|libjvm|/Library/Java' "$evidence/dyld-print-libraries.txt"; then
    echo 'a real analysis mapped an external interpreter or Homebrew library' >&2; exit 1
fi
# Save and restore produced bit-identical audio through the shipped paths.
test "$(awk '{print $1}' "$evidence/render-hashes.txt" | sort -u | wc -l | tr -d ' ')" = 1
awk '/^real/ {print $2}' "$evidence/cold-start.txt" > "$evidence/cold-start-seconds.txt"

# ------------------------------------------- relocation: read-only, with spaces
relocated=$(mktemp -d "${TMPDIR:-/tmp}/iupac relocated read only XXXXXX")
moved="$relocated/IUPAC Synth 2 installed elsewhere"
cp -R "$prefix" "$moved"
chmod -R a-w "$moved"
mkdir -p "$work/relocated-tmp"
cat > "$work/relocated.sh" <<'INNER'
#!/bin/sh
set -eu
export LC_ALL=C LANG=C
moved=$1; evidence=$2; work=$3
export TMPDIR="$work/relocated-tmp"
"$moved/iupac-product-doctor" "$moved" > "$evidence/relocated-doctor.json"
"$moved/iupac-cli" analyze --mode name --text 2,2,2-trifluoroethan-1-ol > "$evidence/relocated-name.json"
"$moved/iupac-cli" generate --mode smiles --text 'CCN(CC)C(=O)c1ccc(Cl)cc1' > "$evidence/relocated-unseen.iupacpatch"
INNER
chmod +x "$work/relocated.sh"
(cd "$work" && sandbox-exec -f "$work/isolation.sb" -D "source-tree=$repo_root" \
    -D "provisioning=$provisioning" \
    /bin/sh "$work/relocated.sh" "$moved" "$evidence" "$work")
grep -q 'OCC(F)(F)F' "$evidence/relocated-name.json"
diff <(sed 's/[0-9]\{8,\}/N/g' "$evidence/unseen.iupacpatch") <(sed 's/[0-9]\{8,\}/N/g' "$evidence/relocated-unseen.iupacpatch")
chmod -R u+w "$moved"; rm -rf "$relocated"

# ------------------------------------------------------------- AU-hosted checks
au_status=pending-owner-console-session
if [ "$(stat -f%Su /dev/console)" = "$(id -un)" ]; then
    components="$HOME/Library/Audio/Plug-Ins/Components"
    mkdir -p "$components"; rm -rf "$components/IUPAC Synth 2.component"
    cp -R "$component" "$components/"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    auval_status=0
    auval -v aumu Iup2 Iups > "$evidence/auval.txt" 2>&1 || auval_status=$?
    if ! grep -q 'AU VALIDATION SUCCEEDED' "$evidence/auval.txt"; then
        echo "auval failed on the installed AU (exit $auval_status)" >&2; exit 1
    fi
    au_pluginval_status=0
    "$work/pluginval" --validate "$components/IUPAC Synth 2.component" --strictness-level 5 \
        --output-dir "$work/pluginval-au" > "$evidence/pluginval-au.log" 2>&1 || au_pluginval_status=$?
    pluginval_verdict AU "$evidence/pluginval-au.log" "$au_pluginval_status"
    au_status=pass
fi

# ------------------------------------------------------------------- the report
archive="$prefix.tar.gz"
python3 - "$prefix" "$archive" "$evidence" "$report" "$au_status" \
    "$vst3_pluginval_status" "${au_pluginval_status-}" <<'PY'
import json, sys
from pathlib import Path
prefix, archive, evidence, report, au_status = (Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]),
                                               Path(sys.argv[4]), sys.argv[5])
# Recorded, not hidden: pluginval sometimes exits non-zero after printing SUCCESS.
exit_status = {"vst3": int(sys.argv[6]), "au": int(sys.argv[7]) if sys.argv[7] else None}
installed = json.loads((prefix / "install-report.json").read_text())
cold = float((evidence / "cold-start-seconds.txt").read_text().strip().replace(",", "."))
document = {
    "schemaVersion": 1, "status": "pass", "platform": "macos-arm64", "gate": "V10 analogue (#42)",
    "network": "denied-by-sandbox-profile", "externalPython": False, "externalJava": False,
    "sourceTreeReachable": False, "installedCli": True, "installedVst3": True, "installedStandalone": True,
    "panelParity": True, "renderSaveRestore": True, "readOnlyRelocationWithSpaces": True,
    "machOBaselineOnly": True, "dyldTranscript": "evidence/dyld-print-libraries.txt",
    "firstSandboxedNameAnalysisSeconds": cold, "auHosted": au_status,
    "pluginvalExitStatus": exit_status,
    "unpackedBytes": installed["unpackedBytes"], "files": installed["files"],
    "compressedBytes": archive.stat().st_size if archive.is_file() else None,
    "formats": installed["formats"],
    "residualIsolationLimits": [
        "SIP prevents removing /usr/bin/python3, /usr/bin/java and the Command Line Tools "
        "Python3.framework; the sandbox profile denies reading them instead.",
        "sandbox-exec denies network and the external interpreter, Homebrew, /usr/local, "
        "checkout and provisioning-venv paths; it is not a separate machine image.",
        "auval and pluginval on the .component need the console session's AudioComponentRegistrar, "
        "so they are executed only in the owner's login session.",
        "pluginval prints '!!! WARNING: Current program is -1...' for any plugin without a program "
        "list; it is a notice, not a failure, and only pluginval's own failure markers "
        "('!!! Test <n> failed', '*** FAILED', 'FAILURE') fail this gate (#94).",
        "Artifacts are ad-hoc signed only: a downloaded copy carries the quarantine attribute and "
        "needs it cleared for this preview.",
        "firstSandboxedNameAnalysisSeconds is a first analysis of this package in this sandbox, not "
        "necessarily a first-ever execution. A freshly written payload measured 15.8 s on the frozen "
        "helper's first execution and 7.9 s on the first OPSIN run (25.5 s end to end inside the "
        "sandbox) against 0.2 s and 5.3 s warm, which is why the request deadline is 45 s.",
    ],
}
if au_status != "pass":
    document["status"] = "pass-with-pending-au-hosted-checks"
Path(report).write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
print(json.dumps({"status": document["status"], "auHosted": au_status,
                  "firstSandboxedNameAnalysisSeconds": cold}))
PY
