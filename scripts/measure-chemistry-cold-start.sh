#!/usr/bin/env bash
# Phase 1 of #97: per-stage cold/warm breakdown of the installed macOS chemistry
# payload. "Cold" means the payload tree has never been executed from its current
# inodes: every probe runs in a freshly created copy of the staged payload with
# extended attributes cleared, which is exactly what a fresh install looks like to
# the kernel's first-execution code evaluation. "Warm" is the second run in the
# same copy.
#
# Usage: measure-chemistry-cold-start.sh <staged-product-dir> [report.json]
# <staged-product-dir> is a tree produced by packaging/stage-product-macos.sh,
# i.e. it contains iupac-cli and resources/chemistry/.
set -euo pipefail
export LC_ALL=C

product="${1:?usage: measure-chemistry-cold-start.sh <staged-product-dir> [report.json]}"
report="${2:-}"
test -x "$product/iupac-cli" || { echo "not a staged product tree: $product" >&2; exit 2; }
test -d "$product/resources/chemistry" || { echo "no chemistry payload in: $product" >&2; exit 2; }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/iupac-cold-XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

now() { python3 -c 'import time;print(repr(time.time()))'; }
delta() { python3 -c "print('%.3f'%($2-$1))"; }

rows=""

# Run one probe twice inside its own pristine copy of the payload and print
# "label<TAB>cold<TAB>warm".
probe() {
    local label="$1" command="$2"
    local copy="$scratch/$label"
    mkdir -p "$copy"
    cp -R "$product/resources" "$product/iupac-cli" "$copy/"
    xattr -cr "$copy" 2>/dev/null || true
    local t0 t1 cold warm
    t0="$(now)"; ( cd "$copy" && eval "$command" ) >/dev/null 2>&1 || true; t1="$(now)"
    cold="$(delta "$t0" "$t1")"
    t0="$(now)"; ( cd "$copy" && eval "$command" ) >/dev/null 2>&1 || true; t1="$(now)"
    warm="$(delta "$t0" "$t1")"
    rm -rf "$copy"
    printf '%-26s cold %8s s   warm %8s s\n' "$label" "$cold" "$warm"
    rows="$rows$label $cold $warm"$'\n'
}

smiles='CN1C=NC2=C1C(=O)N(C)C(=O)N2C'

# Helper only: SMILES never touches the discovery index, OPSIN or the JVM, so this
# is the frozen-payload first-execution cost on its own.
probe helper-smiles "./iupac-cli analyze --mode smiles --text '$smiles'"
# Name that is absent from the offline index, so the full OPSIN/JVM path runs.
probe helper-name-opsin "./iupac-cli analyze --mode name --text caffeine"
# The private JVM on its own, without OPSIN.
probe jvm-start "./resources/chemistry/java/bin/java -version"
# The private JVM plus the OPSIN jar and one parse.
probe opsin-parse "printf 'caffeine\n' > n.txt; ./resources/chemistry/java/bin/java -Xmx256m -jar resources/chemistry/resources/opsin-cli-2.8.0.jar -o smi n.txt o.smi"
# Reading every payload byte without executing anything: isolates plain file I/O
# from first-execution evaluation.
probe payload-read-only "find resources/chemistry/helper -type f -exec cat {} + > /dev/null"

# Discovery index open and first query, measured directly against the shipped
# snapshot through the same code path chemistry/resolution.py uses.
index="$product/resources/chemistry/resources/discovery/discovery-v1.sqlite3"
discovery="$(python3 - "$index" <<'PY'
import json, pathlib, sqlite3, sys, time
path = pathlib.Path(sys.argv[1])
uri = path.resolve().as_uri() + "?mode=ro&immutable=1"
start = time.time(); database = sqlite3.connect(uri, uri=True); opened = time.time() - start
query = ("SELECT DISTINCT r.json FROM names n JOIN records r USING(record_id) "
         "WHERE n.normalized=? ORDER BY r.display_name,r.canonical_smiles,r.record_id LIMIT ?")
start = time.time(); database.execute(query, ("aspirin", 33)).fetchall(); first = time.time() - start
start = time.time(); database.execute(query, ("caffeine", 33)).fetchall(); second = time.time() - start
print(json.dumps({"indexBytes": path.stat().st_size, "openSeconds": opened,
                  "firstQuerySeconds": first, "secondQuerySeconds": second,
                  "records": database.execute("SELECT count(*) FROM records").fetchone()[0],
                  "names": database.execute("SELECT count(*) FROM names").fetchone()[0]}))
PY
)"
printf 'discovery-index             %s\n' "$discovery"

# Mach-O inventory: what the payload ships against what one analysis actually
# maps. Only mapped files are evaluated on first execution.
copy="$scratch/inventory"
mkdir -p "$copy"
cp -R "$product/resources" "$copy/"
xattr -cr "$copy" 2>/dev/null || true
printf '{"protocolVersion":1,"requestId":"inventory","mode":"smiles","text":"%s"}' "$smiles" > "$scratch/request.json"
# DYLD_PRINT_TO_FILE, not stderr: chemistry/helper.py silences fd 2 across the RDKit
# import (#97), which would otherwise swallow the very load records being counted here.
DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_TO_FILE="$scratch/dyld.txt" \
    "$copy/resources/chemistry/helper/iupac-analysis-helper" \
    < "$scratch/request.json" > /dev/null 2>/dev/null || true
grep -o "$copy/resources/chemistry/helper/.*" "$scratch/dyld.txt" \
    | sed "s|$copy/resources/chemistry/helper/||" | sort -u > "$scratch/loaded.txt"
shipped_macho="$(find "$product/resources/chemistry/helper" -type f \
    \( -name '*.dylib' -o -name '*.so' -o -name Python -o -name iupac-analysis-helper \) | wc -l | tr -d ' ')"
shipped_files="$(find "$product/resources/chemistry" -type f | wc -l | tr -d ' ')"
loaded_macho="$(wc -l < "$scratch/loaded.txt" | tr -d ' ')"
loaded_bytes="$(cd "$product/resources/chemistry/helper" && while read -r f; do
    [ -f "$f" ] && stat -f%z "$f"; done < "$scratch/loaded.txt" | awk '{s+=$1}END{print s+0}')"
printf 'mach-o                     shipped %s of %s payload files, mapped by one analysis %s (%s bytes)\n' \
    "$shipped_macho" "$shipped_files" "$loaded_macho" "$loaded_bytes"

# Per-file first-execution cost: map each of those files individually in a pristine
# copy and time each mapping. This separates a per-file cost from a per-byte cost.
copy="$scratch/perfile"
mkdir -p "$copy"
cp -R "$product/resources" "$copy/"
xattr -cr "$copy" 2>/dev/null || true
perfile="$(python3 - "$copy/resources/chemistry/helper" "$scratch/loaded.txt" <<'PY'
import ctypes, json, os, sys, time
base, listing = sys.argv[1], sys.argv[2]
rows = []
for name in (line.strip() for line in open(listing)):
    path = os.path.join(base, name)
    if not name or not os.path.isfile(path):
        continue
    size = os.path.getsize(path)
    start = time.time()
    try:
        ctypes.CDLL(path, mode=ctypes.RTLD_LOCAL)
    except OSError:
        pass  # a symbol mismatch still maps and evaluates the file, which is the cost measured
    rows.append((time.time() - start, size, name))
total = sum(row[0] for row in rows)
size = sum(row[1] for row in rows)
rows.sort(reverse=True)
print(json.dumps({"files": len(rows), "totalSeconds": total, "totalBytes": size,
                  "medianMillisecondsPerFile": 1000 * sorted(r[0] for r in rows)[len(rows) // 2],
                  "millisecondsPerMegabyte": 1000 * total / (size / 1e6),
                  "slowest": [{"milliseconds": round(1000 * d), "bytes": s, "file": f} for d, s, f in rows[:10]]}))
PY
)"
printf 'per-file-mapping           %s\n' "$perfile"

if [ -n "$report" ]; then
    python3 - "$report" "$discovery" "$perfile" "$shipped_macho" "$shipped_files" "$loaded_macho" "$loaded_bytes" <<PY
import json, sys
stages = {}
for line in """$rows""".strip().splitlines():
    label, cold, warm = line.split()
    stages[label] = {"coldSeconds": float(cold), "warmSeconds": float(warm)}
report, discovery, perfile = sys.argv[1], json.loads(sys.argv[2]), json.loads(sys.argv[3])
document = {"schemaVersion": 1, "stages": stages, "discoveryIndex": discovery,
            "perFileMapping": perfile,
            "machO": {"shipped": int(sys.argv[4]), "payloadFiles": int(sys.argv[5]),
                      "mappedByOneAnalysis": int(sys.argv[6]), "mappedBytes": int(sys.argv[7])}}
with open(report, "w", encoding="utf-8") as stream:
    json.dump(document, stream, indent=2, sort_keys=True)
    stream.write("\n")
PY
    printf 'report -> %s\n' "$report"
fi
