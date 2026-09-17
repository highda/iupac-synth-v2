#!/bin/sh
set -eu
root=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
required='bin/iupac-cli
bin/IUPAC Synth 2
lib/vst3/IUPAC Synth 2.vst3/Contents/aarch64-linux/IUPAC Synth 2.so
resources/chemistry/helper/iupac-analysis-helper
resources/chemistry/java/bin/java
resources/chemistry/resources/opsin-cli-2.8.0.jar
resources/chemistry/resources/discovery/discovery-v1.sqlite3
lib/vst3/IUPAC Synth 2.vst3/Contents/Resources/chemistry/helper/iupac-analysis-helper
share/iupac-synth-2/factory/All Modules.iupacpatch'
printf '%s\n' "$required" | while IFS= read -r file; do
    test -f "$root/$file" || { echo "repair or reinstall product; missing $file" >&2; exit 2; }
done
if find "$root" -type l -exec sh -c 'root=$1; shift; for p do case "$(readlink -f "$p")" in "$root"/*) ;; *) exit 1;; esac; done' sh "$root" {} +; then :; else
    echo 'repair or reinstall product; escaping symlink' >&2; exit 2
fi
(cd "$root" && sha256sum -c install-manifest.sha256 >/dev/null) || { echo 'repair or reinstall product; manifest mismatch' >&2; exit 2; }
test "$("$root/bin/iupac-cli")" = '{"product":"IUPAC Synth 2","architecture":3,"chemistryEnabled":true}' || { echo 'wrong product identity' >&2; exit 2; }
printf '{"status":"ok","prefix":"%s"}\n' "$root"
