#!/usr/bin/env python3
"""Cross-platform V1 parity of the frozen chemistry payload (#42).

Runs every frozen panel record through the production `iupac-cli generate`
path, so the canonical Analysis, the SonicIntent projection and the generated
Patch all come from the shipped helper rather than a test reimplementation, and
compares per-record digests against the expectations committed from the
canonical Linux arm64 payload. A macOS arm64 payload that pins the nearest
available RDKit release is acceptable only while these digests match exactly;
any divergence is an architecture-deviation, never a silent version bump.
"""
import argparse
import hashlib
import json
import platform
import re
import subprocess
import sys
from pathlib import Path

PANELS = ("identity", "broad-development", "legacy-hard", "holdout")
# The OPSIN/Java half of the payload is not reachable from the smiles-only
# panels, so the non-alias IUPAC name of the clean-machine gate rides along.
NAME_RECORDS = (("name-path-trifluoroethanol", "name", "2,2,2-trifluoroethan-1-ol"),)
ABSOLUTE_PATH = re.compile(r'"(/[^"]*)"')


def canonical(text, roots):
    """Drop deployment paths, then re-serialise deterministically."""
    for root in roots:
        text = text.replace(str(root), "<deployment>")
    stripped = ABSOLUTE_PATH.sub('"<deployment>"', text)
    return json.dumps(json.loads(stripped), sort_keys=True, separators=(",", ":"))


def digest(value):
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def records(repo):
    for panel in PANELS:
        loaded = json.loads((repo / "data/panels" / f"{panel}.json").read_text())
        yield panel, [(item["id"], item["mode"], item["text"]) for item in loaded["records"]]
    yield "name-path", list(NAME_RECORDS)


def measure(cli, helper_root, repo):
    result = {}
    for panel, items in records(repo):
        entries = {}
        for identifier, mode, text in items:
            command = [str(cli), "generate", "--mode", mode, "--text", text]
            if helper_root:
                command += ["--helper-root", str(helper_root)]
            run = subprocess.run(command, text=True, capture_output=True, timeout=120)
            if run.returncode != 0:
                raise SystemExit(f"{panel}/{identifier}: generate failed: {run.stdout} {run.stderr}")
            entries[identifier] = digest(canonical(run.stdout, [repo, helper_root] if helper_root else [repo]))
        result[panel] = entries
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--helper-root", type=Path)
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--expectations", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--write-expectations", action="store_true")
    arguments = parser.parse_args()
    expectations = arguments.expectations or arguments.repository / "data/panels/cross-platform-parity-v1.json"

    measured = measure(arguments.cli, arguments.helper_root, arguments.repository)
    manifest = json.loads((arguments.repository / "data/panels/chemistry-manifest.json").read_text())
    platform_name = f"{platform.system().lower()}-{platform.machine()}"
    document = {"schemaVersion": 1, "analysisVersion": 1, "backend": manifest["backend"],
                "command": "iupac-cli generate --mode MODE --text TEXT",
                "panelFiles": {panel: manifest["files"][f"data/panels/{panel}.json"] for panel in PANELS},
                "panels": measured}

    if arguments.write_expectations:
        document["canonicalPlatform"] = platform_name
        expectations.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
        report = {"schemaVersion": 1, "status": "written", "platform": platform_name,
                  "expectations": str(expectations.name), "records": sum(len(x) for x in measured.values())}
        arguments.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        print(f"wrote cross-platform parity expectations from {platform_name}")
        return 0

    expected = json.loads(expectations.read_text())
    differences = []
    if expected["backend"] != document["backend"]:
        differences.append(f"backend pin differs: {expected['backend']} vs {document['backend']}")
    if expected["panelFiles"] != document["panelFiles"]:
        differences.append("frozen panel files differ from the expectations they were taken from")
    for panel, entries in document["panels"].items():
        reference = expected["panels"].get(panel, {})
        for identifier, value in entries.items():
            if reference.get(identifier) != value:
                differences.append(f"{panel}/{identifier}: {reference.get(identifier)} != {value}")
        for identifier in reference.keys() - entries.keys():
            differences.append(f"{panel}/{identifier}: missing from this platform")
    report = {"schemaVersion": 1, "status": "pass" if not differences else "fail",
              "platform": platform_name, "canonicalPlatform": expected.get("canonicalPlatform"),
              "records": sum(len(x) for x in document["panels"].values()), "differences": differences}
    arguments.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if differences:
        for item in differences[:20]:
            print("parity difference:", item, file=sys.stderr)
        raise SystemExit(f"cross-platform V1 parity failed on {platform_name} ({len(differences)} differences)")
    print(f"cross-platform V1 parity holds on {platform_name}: {report['records']} records")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
