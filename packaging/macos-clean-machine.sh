#!/usr/bin/env bash
# Remove every removable external language runtime from an ephemeral macOS runner
# before the V10 analogue runs (#42). Destructive by design and therefore refused
# unless the caller is an ephemeral runner that opted in explicitly; on the
# owner's host the same isolation is obtained by denying read access in
# packaging/macos-isolation.sb, and what SIP prevents removing is reported as a
# residual limit instead of being hidden.
# Usage: IUPAC_DESTRUCTIVE_RUNNER=1 macos-clean-machine.sh <inventory>
set -euo pipefail
inventory=${1:?inventory path required}
[ "${IUPAC_DESTRUCTIVE_RUNNER:-0}" = 1 ] || { echo 'refusing: this removes host toolchains; runners only' >&2; exit 1; }
[ "${CI:-}" = true ] || { echo 'refusing: not an ephemeral CI runner' >&2; exit 1; }

: > "$inventory"
record() { printf '%s %s\n' "$1" "$2" >> "$inventory"; }
for target in \
    /Library/Frameworks/Python.framework \
    /Library/Java/JavaVirtualMachines \
    /opt/homebrew/opt/python@3.9 /opt/homebrew/opt/python@3.10 /opt/homebrew/opt/python@3.11 \
    /opt/homebrew/opt/python@3.12 /opt/homebrew/opt/python@3.13 /opt/homebrew/opt/openjdk \
    /opt/homebrew/opt/openjdk@17 /opt/homebrew/opt/openjdk@21 /opt/homebrew/Cellar/python@3.11 \
    /opt/homebrew/Cellar/openjdk /opt/homebrew/bin/python3 /opt/homebrew/bin/java \
    /usr/local/bin/python3 /usr/local/bin/java; do
    if [ -e "$target" ]; then
        if sudo rm -rf "$target" 2>/dev/null; then record removed "$target"; else record 'sip-protected' "$target"; fi
    else
        record absent "$target"
    fi
done
# SIP keeps these in place on every macOS host; the sandbox profile denies reading
# them instead. Never claim they were removed.
for target in /usr/bin/python3 /usr/bin/java /Library/Developer/CommandLineTools/Library/Frameworks/Python3.framework; do
    if [ -e "$target" ]; then record 'sip-protected' "$target"; else record absent "$target"; fi
done
printf 'clean-machine inventory written to %s\n' "$inventory"
