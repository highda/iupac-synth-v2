#!/usr/bin/env python3
"""Generate the fixed editor slot geometry (#69) from scripts/layout-slots.dot with Graphviz dot.

The committed numbers in include/iupac/ui/Layout.hpp come from this script; rerunning it must
reproduce them exactly. Layout engine: the `dot` binary when present, otherwise the identical
Graphviz build compiled to WebAssembly (@viz-js/viz, pinned) run through node. The DOT input pins
the six columns, so dot decides only the in-column order/spacing; the result is rescaled into the
1000x700 reference frame leaving a cable corridor above and a return corridor below the field.

Usage: scripts/layout-slots.py [--check include/iupac/ui/Layout.hpp]
"""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DOT_INPUT = ROOT / "scripts" / "layout-slots.dot"
VIZ_PACKAGE = "@viz-js/viz@3.30.0"
FRAME_WIDTH, FRAME_HEIGHT = 1000.0, 700.0
TOP_CORRIDOR, BOTTOM_CORRIDOR = 70.0, 90.0  # forward spanning corridor / backward return channel
SIDE_MARGIN = 20.0
COLUMNS = ["SRC", "RES", "FILT", "SHAPE", "MIX", "OUT"]
KINDS = {"SRC": "source", "RES": "resonator", "FILT": "filter", "SHAPE": "shaper", "MIX": "mixer", "OUT": "output"}


def run_dot_plain(dot_source: str) -> str:
    dot = shutil.which("dot")
    if dot:
        return subprocess.run([dot, "-Tplain"], input=dot_source, text=True, capture_output=True, check=True).stdout
    node = shutil.which("node")
    npm = shutil.which("npm")
    if not (node and npm):
        sys.exit("neither `dot` nor node/npm (for @viz-js/viz) is available")
    cache = pathlib.Path(os.environ.get("IUPAC_VIZ_CACHE", pathlib.Path.home() / ".cache" / "iupac-viz"))
    module = cache / "node_modules" / "@viz-js" / "viz"
    if not module.exists():
        cache.mkdir(parents=True, exist_ok=True)
        subprocess.run([npm, "install", "--prefix", str(cache), "--no-audit", "--no-fund", VIZ_PACKAGE], check=True, capture_output=True)
    script = (
        "const {instance} = require('@viz-js/viz');"
        "let src = ''; process.stdin.on('data', d => src += d);"
        "process.stdin.on('end', () => instance().then(v => process.stdout.write(v.renderString(src, {format: 'plain'}))));"
    )
    return subprocess.run([node, "-e", script], input=dot_source, text=True, capture_output=True, check=True,
                          env={**os.environ, "NODE_PATH": str(cache / "node_modules")}).stdout


def parse_plain(plain: str):
    nodes = {}
    for line in plain.splitlines():
        parts = line.split()
        if parts and parts[0] == "node":
            name, x, y, w, h = parts[1], float(parts[2]), float(parts[3]), float(parts[4]), float(parts[5])
            nodes[name] = (x, y, w, h)
    return nodes


def layout():
    nodes = parse_plain(run_dot_plain(DOT_INPUT.read_text()))
    # dot's plain output is inches, y up. Rescale x into the frame with side margins and y into the
    # field between the two corridors, flipping y so the reference frame is y-down.
    min_x = min(x - w / 2 for x, y, w, h in nodes.values())
    max_x = max(x + w / 2 for x, y, w, h in nodes.values())
    min_y = min(y - h / 2 for x, y, w, h in nodes.values())
    max_y = max(y + h / 2 for x, y, w, h in nodes.values())
    sx = (FRAME_WIDTH - 2 * SIDE_MARGIN) / (max_x - min_x)
    sy = (FRAME_HEIGHT - TOP_CORRIDOR - BOTTOM_CORRIDOR) / (max_y - min_y)
    slots = []
    for column, prefix in enumerate(COLUMNS):
        names = sorted(n for n in nodes if re.fullmatch(prefix + r"\d*", n))
        for name in names:
            x, y, w, h = nodes[name]
            left = SIDE_MARGIN + (x - w / 2 - min_x) * sx
            top = TOP_CORRIDOR + (max_y - (y + h / 2)) * sy
            slots.append({"name": name, "kind": KINDS[prefix], "column": column,
                          "x": round(left, 1), "y": round(top, 1), "width": round(w * sx, 1), "height": round(h * sy, 1)})
    # The DOT nodes of one column are interchangeable (identical eligible edges), so the instance
    # index is assigned top to bottom from dot's y and the table is ordered column-major.
    slots.sort(key=lambda s: (s["column"], s["y"]))
    for column in range(len(COLUMNS)):
        for instance, slot in enumerate(s for s in slots if s["column"] == column):
            slot["instance"] = instance
            slot["name"] = COLUMNS[column] + (str(instance + 1) if COLUMNS[column] != "OUT" else "")
    return slots


def cpp_rows(slots):
    rows = []
    for s in slots:
        rows.append(f"    {{SlotKind::{s['kind']}, {s['instance']}, {s['column']}, {{{s['x']:.1f}, {s['y']:.1f}, {s['width']:.1f}, {s['height']:.1f}}}}}, // {s['name']}")
    return "\n".join(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", type=pathlib.Path, help="verify the header's slot rows match the generated ones")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    slots = layout()
    if args.json:
        print(json.dumps(slots, indent=2))
        return
    rows = cpp_rows(slots)
    if args.check:
        header = args.check.read_text()
        missing = [r.strip() for r in rows.splitlines() if r.strip() not in header]
        if missing:
            sys.exit("Layout.hpp is out of date with scripts/layout-slots.py:\n" + "\n".join(missing))
        print(f"{args.check} matches the generated slot table")
        return
    print(rows)


if __name__ == "__main__":
    main()
