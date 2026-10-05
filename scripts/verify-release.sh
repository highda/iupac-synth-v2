#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
commit=$(git -C "$repo_root" rev-parse HEAD)
output_root=${IUPAC_VERIFY_OUTPUT_ROOT:-$repo_root/out/verification}
output_dir="$output_root/$commit"
package_report=${IUPAC_PACKAGE_REPORT:-$repo_root/out/iupac-chemistry/package-report.json}
jobs=${IUPAC_BUILD_JOBS:-4}

mkdir -p "$output_dir/logs"
exec > >(tee "$output_dir/logs/verify-release.log") 2>&1

run_logged() {
    local name=$1
    shift
    "$@" 2>&1 | tee "$output_dir/logs/$name.log"
}

cmake --preset linux-release -S "$repo_root"
cmake --build --preset linux-release --parallel "$jobs"
run_logged release-ctest ctest --preset linux-release --output-on-failure

IUPAC_V0_BENCHMARK_SECONDS=${IUPAC_RELEASE_BENCHMARK_SECONDS:-60} \
    run_logged v0 "$repo_root/scripts/verify-v0.sh"

cmake --preset linux-sanitize -S "$repo_root"
cmake --build --preset linux-sanitize --target iupac-tests iupac-plugin-tests --parallel "$jobs"
run_logged sanitize-ctest ctest --preset linux-sanitize --output-on-failure

if [[ -n "${IUPAC_TSAN_RUN_ID:-}" ]]; then
    gh api "repos/{owner}/{repo}/actions/runs/$IUPAC_TSAN_RUN_ID" > "$output_dir/tsan-action.json"
    jq -e --arg commit "$commit" '.head_sha == $commit and .status == "completed" and .conclusion == "success"' \
        "$output_dir/tsan-action.json" >/dev/null
    tsan_status="pass: exact-commit GitHub Actions run $IUPAC_TSAN_RUN_ID"
else
    cmake --preset linux-tsan -S "$repo_root"
    cmake --build --preset linux-tsan --target iupac-tests --parallel "$jobs"
    run_logged tsan-ctest ctest --test-dir "$repo_root/build/linux-tsan" \
        --output-on-failure -R '^product-path-smoke$'
    tsan_status='pass: local instrumented production test'
fi

if [[ -f "$package_report" ]]; then
    jq -e '.schemaVersion == 1 and .negativeIsolation.sourceTreeRequired == false and
           .negativeIsolation.damagedPayload == "diagnosed" and
           .negativeIsolation.deadlineTreeCleanup == "passed"' "$package_report" >/dev/null
    cp "$package_report" "$output_dir/package-report.json"
    package_status='prerequisite private-runtime report validated; installed-artifact V10 pending #15'
else
    package_status='not-present (V10 installed-artifact proof belongs to #15)'
fi

python3 - "$repo_root" "$output_dir" "$commit" "$package_status" "$tsan_status" <<'PY'
import hashlib, json, platform, subprocess, sys
from pathlib import Path

root, output, commit, package_status, tsan_status = (Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5])
inputs = ["data/panels/authored-synth/panel.json", "data/panels/chemistry-manifest.json",
          "data/panels/legacy-hard-pairs.json", "data/discovery/manifest-v2.json",
          "cmake/Dependencies.lock"]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
tool = lambda *c: subprocess.check_output(c, text=True, stderr=subprocess.STDOUT).splitlines()[0]
report = {
    "schemaVersion": 1, "architecture": 3, "commit": commit,
    "dirty": bool(subprocess.check_output(["git", "-C", str(root), "status", "--porcelain"], text=True).strip()),
    "platform": {"machine": platform.machine(), "system": platform.system(), "release": platform.release()},
    "toolchain": {"cmake": tool("cmake", "--version"), "compiler": tool("c++", "--version"), "python": platform.python_version()},
    "inputs": {name: sha(root / name) for name in inputs},
    "gates": {"V0": "pass: chemistry-OFF production orchestration",
              "V1-V9": "pass: full Release CTest, mapping calibration, V0, ASan/UBSan, pluginval/editor",
              "V7-tsan": tsan_status,
              "V10": package_status,
              "V11": "pass: discovery data/helper/CLI/browser/cache production tests"},
    "reports": {"releaseCtest": "logs/release-ctest.log", "v0": "logs/v0.log",
                "sanitizers": "logs/sanitize-ctest.log", "tsan": "logs/tsan-ctest.log"},
}
(output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
print(json.dumps({"status": "pass", "report": str(output / "report.json"), "commit": commit}, sort_keys=True))
PY
