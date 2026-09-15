#!/usr/bin/env bash
set -euo pipefail
payload="${1:?payload directory required}"
artifact="${2:?artifact directory required}"
destination="${3:-Resources/IUPAC Chemistry}"
test -f "$payload/package-report.json"
test ! -e "$artifact/$destination" || { echo "payload destination already exists" >&2; exit 2; }
mkdir -p "$artifact/$(dirname "$destination")"
cp -a "$payload" "$artifact/$destination"
