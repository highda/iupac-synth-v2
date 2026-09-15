#!/usr/bin/env bash
set -euo pipefail
payload="${1:-/opt/iupac-chemistry}"
report="${2:-/tmp/clean-runtime-report.json}"
test ! -e /usr/bin/python3
test ! -e /usr/bin/java
test ! -e /usr/bin/javac
test ! -e /usr/bin/pip
test ! -d /workspace
test ! -d /src
test -f "$payload/resources/discovery/discovery-v1.sqlite3"
export PATH=/no-external-runtime PYTHONHOME=/no/python PYTHONPATH=/no/modules JAVA_HOME=/no/java CLASSPATH=/no/jar LD_LIBRARY_PATH=/no/libs
name='{"protocolVersion":1,"requestId":"name","mode":"name","text":"2,2,2-trifluoroethan-1-ol"}'
smiles='{"protocolVersion":1,"requestId":"smiles","mode":"smiles","text":"CCN(CC)C(=O)c1ccc(Cl)cc1"}'
name_result="$(printf '%s' "$name" | "$payload/helper/iupac-analysis-helper")"
smiles_result="$(printf '%s' "$smiles" | "$payload/helper/iupac-analysis-helper")"
case "$name_result" in *'"status":"ok"'*) ;; *) echo "real IUPAC analysis failed: $name_result" >&2; exit 1;; esac
case "$name_result" in *'OCC(F)(F)F'*) ;; *) echo "real IUPAC identity was not resolved: $name_result" >&2; exit 1;; esac
case "$smiles_result" in *'"status":"ok"'*) ;; *) echo "unseen SMILES analysis failed: $smiles_result" >&2; exit 1;; esac
case "$smiles_result" in *'CCN(CC)C(=O)c1ccc(Cl)cc1'*) ;; *) echo "unseen SMILES identity was not resolved: $smiles_result" >&2; exit 1;; esac
printf '{"schemaVersion":1,"network":"disabled-by-runner","externalPython":false,"externalJava":false,"sourceTree":false,"name":%s,"smiles":%s}\n' "$name_result" "$smiles_result" > "$report"
