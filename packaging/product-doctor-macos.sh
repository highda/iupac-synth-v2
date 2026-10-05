#!/bin/sh
# Bundle doctor for the staged macOS arm64 product (#42). POSIX shell only: the
# owner's /bin/sh and /bin/bash are bash 3.2, so no arrays, no ${var^^}, no
# mapfile, no [[ ]].
set -eu
root=${1:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}
# Physical path: /tmp is a symlink to /private/tmp, so an install under a temporary
# directory would otherwise make every internal symlink look like it escapes.
root=$(CDPATH= cd -- "$root" && pwd -P)
formats='IUPAC Synth 2.vst3
IUPAC Synth 2.component
IUPAC Synth 2.app'
required='iupac-cli
resources/chemistry/helper/iupac-analysis-helper
resources/chemistry/helper/payload-layout.json
resources/chemistry/java/bin/java
resources/chemistry/resources/opsin-cli-2.8.0.jar
resources/chemistry/resources/discovery/discovery-v2.sqlite3
share/iupac-synth-2/factory/All Modules.iupacpatch'
printf '%s\n' "$required" | while IFS= read -r file; do
    test -f "$root/$file" || { echo "repair or reinstall product; missing $file" >&2; exit 2; }
done
# Every format carries its own complete private payload under its resources.
printf '%s\n' "$formats" | while IFS= read -r bundle; do
    for file in \
        "Contents/MacOS/IUPAC Synth 2" \
        "Contents/Info.plist" \
        "Contents/Resources/chemistry/helper/iupac-analysis-helper" \
        "Contents/Resources/chemistry/helper/payload-layout.json" \
        "Contents/Resources/chemistry/java/bin/java" \
        "Contents/Resources/chemistry/resources/opsin-cli-2.8.0.jar" \
        "Contents/Resources/chemistry/resources/discovery/discovery-v2.sqlite3"; do
        test -f "$root/$bundle/$file" || { echo "repair or reinstall product; missing $bundle/$file" >&2; exit 2; }
    done
    test -x "$root/$bundle/Contents/Resources/chemistry/helper/iupac-analysis-helper" \
        || { echo "repair or reinstall product; $bundle helper is not executable" >&2; exit 2; }
done
if find "$root" -type l -exec sh -c 'root=$1; shift; for p do case "$(readlink -f "$p")" in "$root"/*) ;; *) exit 1;; esac; done' sh "$root" {} +; then :; else
    echo 'repair or reinstall product; escaping symlink' >&2; exit 2
fi
# Named files only: this script itself necessarily contains the patterns.
if grep -al -e /opt/homebrew -e /Users/Shared/iupac-macos-build -e iupac-synth-v2 \
    "$root/iupac-cli" "$root/resources/chemistry/helper/payload-layout.json" >/dev/null 2>&1; then
    echo 'repair or reinstall product; developer path recorded in the delivery' >&2; exit 2
fi
(cd "$root" && shasum -a 256 -c install-manifest.sha256 >/dev/null) \
    || { echo 'repair or reinstall product; manifest mismatch' >&2; exit 2; }
test "$("$root/iupac-cli")" = '{"product":"IUPAC Synth 2","architecture":3,"chemistryEnabled":true}' \
    || { echo 'wrong product identity' >&2; exit 2; }
printf '{"status":"ok","prefix":"%s"}\n' "$root"
