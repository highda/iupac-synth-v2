#!/usr/bin/env python3
"""Run frozen V3/V4 and calibration-relevant V5 gates through iupac-cli."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from collections import Counter
from pathlib import Path

import numpy as np

from render_distance import distance, read_wav


NOTES = (36, 60, 84)
RATE = 48000
VELOCITY = 100
FLOOR = 0.08


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run_json(command):
    completed = subprocess.run(command, check=True, text=True, capture_output=True)
    return json.loads(completed.stdout)


def state_for(patch, trace):
    return {
        "stateVersion": 1, "basePatch": patch, "editedPatch": patch,
        "controls": {"macros": [0, 0, 0, 0], "outputGain": -6,
                     "width": .5, "masterTune": 0, "bypass": False},
        "provenance": {"mappingTrace": trace},
    }


def midi_for(note):
    return {"events": [
        {"sampleOffset": 0, "type": "noteOn", "channel": 1,
         "data1": note, "data2": VELOCITY},
        {"sampleOffset": 2 * RATE, "type": "noteOff", "channel": 1,
         "data1": note, "data2": 0},
    ]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--pairs", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--coverage", default=str(Path(__file__).resolve().parents[1] / "data/mapping-coverage.json"))
    args = parser.parse_args()
    fixture_path, pair_path = Path(args.fixture), Path(args.pairs)
    fixture = json.loads(fixture_path.read_text())
    records = {r["id"]: r for r in fixture["records"]}
    broad = [key for key in records if not key.startswith(("identity-", "legacy-", "holdout-"))
             and key not in {"propan-1-ol", "propan-2-ol", "ethylene-glycol", "succinic-acid",
                             "furan", "thiazole", "anisole", "benzoic-acid",
                             "methyl-acetylsalicylate", "valine"}]
    hard = [key for key in records if key.startswith("legacy-")]
    holdout = [key for key in records if key in {"propan-1-ol", "propan-2-ol", "ethylene-glycol",
               "succinic-acid", "furan", "thiazole", "anisole", "benzoic-acid",
               "methyl-acetylsalicylate", "valine"} or key.startswith("holdout-")]
    selected = broad + hard + holdout
    output = Path(args.output_dir); output.mkdir(parents=True, exist_ok=True)
    traces, renders, latencies, signatures, safety, safety_failures = {}, {}, {}, {}, {}, []
    with tempfile.TemporaryDirectory(prefix="iupac-mapping-") as temporary:
        temporary = Path(temporary)
        for identifier in selected:
            analysis = temporary / f"{identifier}.analysis.json"
            analysis.write_text(json.dumps(records[identifier]["response"], sort_keys=True))
            trace = run_json([args.cli, "inspect", "--stage", "mapping", "--analysis", str(analysis),
                              "--request-id", identifier])
            if trace["mappingVersion"] != 2 or trace["projectionVersion"] != 1:
                raise AssertionError(f"{identifier}: versions not frozen at mapper 2 / projection 1")
            traces[identifier] = trace
            state = temporary / f"{identifier}.state.json"
            state.write_text(json.dumps(state_for(trace["patch"], trace), sort_keys=True))
            signatures[identifier] = {}
            for note in NOTES:
                midi = temporary / f"note-{note}.json"; midi.write_text(json.dumps(midi_for(note)))
                wav = output / "renders" / identifier / f"note-{note}.wav"
                manifest = run_json([args.cli, "render", "--snapshot", str(state), "--midi", str(midi),
                                     "--sample-rate", str(RATE), "--block-size", "128",
                                     "--samples", str(3 * RATE), "--output", str(wav)])
                renders[identifier, note] = wav
                latencies[identifier, note] = manifest["latencySamples"]
                signatures[identifier] = {"graph": manifest["graphSignature"],
                                           "value": manifest["valueSignature"]}
                if note == 60:
                    rate, samples = read_wav(wav); active = samples[manifest["latencySamples"]:manifest["latencySamples"] + 2 * rate]
                    peak = float(np.max(np.abs(samples))); rms = float(np.sqrt(np.mean(active * active)))
                    dc = float(abs(np.mean(samples)))
                    safety[identifier] = {"peak": peak, "activeRmsDb": float(20*np.log10(max(rms, 1e-12))),
                                          "dcMagnitude": dc, "guardHits": manifest["guardHits"]}
                    if peak > .891252 or dc > .005 or not (-60 < safety[identifier]["activeRmsDb"] < -6):
                        safety_failures.append(f"V5 {identifier}: {safety[identifier]}")

    def pair_result(first, second):
        values = {}
        for note in NOTES:
            try:
                values[str(note)] = distance(read_wav(renders[first, note])[1][latencies[first, note]:],
                                              read_wav(renders[second, note])[1][latencies[second, note]:], RATE)
            except ValueError as failure:
                values[str(note)] = {"metricVersion": 1, "distance": 0.0,
                                     "spectral": 0.0, "envelope": 0.0, "error": str(failure)}
        return {"first": first, "second": second, "notes": values,
                "maximum": max(item["distance"] for item in values.values())}

    hard_pairs = json.loads(pair_path.read_text())["pairs"]
    hard_results = [pair_result(*pair) for pair in hard_pairs]
    holdout_pairs = [pair_result("propan-1-ol", "propan-2-ol"),
                     pair_result("holdout-leu-gly", "holdout-gly-leu")]
    broad_results = [pair_result(broad[i], broad[j]) for i in range(len(broad)) for j in range(i + 1, len(broad))]
    failures = [p for p in hard_results + holdout_pairs if p["maximum"] < FLOOR]
    broad_fraction = sum(p["maximum"] >= FLOOR for p in broad_results) / len(broad_results)
    graph_counts = {panel: Counter(signatures[key]["graph"] for key in ids)
                    for panel, ids in (("broad", broad), ("hard", hard), ("holdout", holdout))}
    v3_failures = []
    if len(graph_counts["broad"]) < 6 or max(graph_counts["broad"].values()) / len(broad) > .6: v3_failures.append("broad graph occupancy")
    if len(graph_counts["hard"]) < 4 or max(graph_counts["hard"].values()) / len(hard) > .6: v3_failures.append("hard graph occupancy")
    if len(graph_counts["holdout"]) < 3: v3_failures.append("holdout graph count")
    broad_patches = [traces[key]["patch"] for key in broad]
    source_types = {node["type"] for patch in broad_patches for node in patch["nodes"]}
    resonator_modes = {node["parameters"]["mode"] for patch in broad_patches for node in patch["nodes"] if node["type"] == "resonator"}
    destinations = {row["destinationParameter"] for patch in broad_patches for row in patch["matrix"] if row["enabled"]}
    if not {"harmonic", "fm", "noise"}.issubset(source_types): v3_failures.append("broad source types")
    if resonator_modes != {0, 1}: v3_failures.append("broad resonator modes")
    if len(destinations) < 4: v3_failures.append("broad modulation destination kinds")
    serial = parallel = False
    for patch in broad_patches:
        sources = {node["id"] for node in patch["nodes"] if node["type"] in {"harmonic", "fm", "noise"}}
        incoming = Counter(edge["destination"] for edge in patch["edges"])
        serial |= any(edge["source"] not in sources for edge in patch["edges"])
        parallel |= any(count > 1 for count in incoming.values())
    if not serial: v3_failures.append("missing serial resonator graph")
    if not parallel: v3_failures.append("missing parallel resonator graph")
    # Mapper-coverage invariant, second half (#128): the table claims every entry's `ruleId`
    # appears in the generation trace. Check it against the traces actually produced above rather
    # than trusting the table, so a rule that stops firing is a gate failure and not a stale note.
    coverage = json.loads(Path(args.coverage).read_text())
    traced_rules = {item["ruleId"] for trace in traces.values() for item in trace["rules"]}
    declared_rules = {e["ruleId"] for e in coverage["entries"] if e["status"] == "mapped"}
    absent = sorted(declared_rules - traced_rules)
    if absent:
        v3_failures.append(f"coverage ruleIds never traced: {absent}")
    still_provisional = sorted(f"{e['module']}.{e['parameter']}" for e in coverage["entries"] if e["status"] != "mapped")
    if still_provisional:
        v3_failures.append(f"coverage entries still provisional: {still_provisional}")
    # MAPPING-POLICY caps generated rows at 12 inside the 40-row Patch cap, so the rest stays
    # editing room; D8 explicitly left that budget unchanged. Phase-4 parameters are reached by
    # value assignment, never by spending more rows.
    over_budget = {key: len(trace["patch"]["matrix"]) for key, trace in traces.items()
                   if len(trace["patch"]["matrix"]) > 12}
    if over_budget:
        v3_failures.append(f"generated rows over the MAPPING-POLICY budget of 12: {over_budget}")
    peptides = [key for key in hard if any(token in key for token in ("gly", "ala", "cys", "met", "phe"))]
    peptide_axes = [tuple(traces[key]["sonicIntent"]["axes"][axis] for axis in ("density", "brightness", "decay")) for key in peptides]
    if len({signatures[key]["graph"] for key in peptides}) < 2 or len(set(peptide_axes)) < 2:
        v3_failures.append("peptide graph/axis collapse")
    for first, second in hard_pairs:
        if signatures[first]["value"] == signatures[second]["value"]: v3_failures.append(f"equal hard values: {first}/{second}")
    for i, first in enumerate(holdout):
        for second in holdout[i+1:]:
            if records[first]["response"]["analysis"]["canonicalIsomericSmiles"] != records[second]["response"]["analysis"]["canonicalIsomericSmiles"] and signatures[first]["value"] == signatures[second]["value"]:
                v3_failures.append(f"equal holdout values: {first}/{second}")
    report = {
        "gateVersion": 1, "metricVersion": 1, "mapperVersion": 2, "projectionVersion": 1,
        "fixtureSha256": sha(fixture_path), "pairManifestSha256": sha(pair_path),
        "coverageSha256": sha(Path(args.coverage)), "coverageVersion": coverage["coverageVersion"],
        "coverage": {"declaredRules": sorted(declared_rules), "tracedRules": sorted(traced_rules),
                     "declaredRulesNeverTraced": absent, "provisionalEntries": still_provisional,
                     "maximumGeneratedRows": max(len(t["patch"]["matrix"]) for t in traces.values()),
                     "maximumGeneratedNodes": max(len(t["patch"]["nodes"]) for t in traces.values()),
                     "maximumGeneratedEdges": max(len(t["patch"]["edges"]) for t in traces.values())},
        "render": {"sampleRate": RATE, "blockSize": 128, "notes": list(NOTES), "velocity": VELOCITY,
                   "heldSeconds": 2, "releaseSeconds": 1},
        "panels": {"broad": broad, "hard": hard, "holdout": holdout},
        "graphOccupancy": {panel: dict(counts) for panel, counts in graph_counts.items()},
        "signatures": signatures, "safety": safety,
        "v4": {"threshold": FLOOR, "hardPairs": hard_results, "holdoutPairs": holdout_pairs,
               "broadPairPassFraction": broad_fraction,
               "nearestBroadPairs": sorted(broad_results, key=lambda x: x["maximum"])[:10]},
        "clamping": {"generatedPatchValidation": "production decoder/compiler accepted every snapshot",
                     "hardGuardHitsTotal": sum(x["guardHits"] for x in safety.values())},
        "status": "pass" if not failures and broad_fraction >= .8 and not v3_failures and not safety_failures else "fail",
        "failures": [f"V4 {x['first']}/{x['second']}={x['maximum']:.6f}" for x in failures]
                    + ([f"V4 broad fraction={broad_fraction:.6f}"] if broad_fraction < .8 else []) + v3_failures + safety_failures,
    }
    (output / "traces.json").write_text(json.dumps(traces, indent=2, sort_keys=True) + "\n")
    (output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": report["status"], "failures": report["failures"],
                      "broadPairPassFraction": broad_fraction,
                      "minimumHardPair": min(x["maximum"] for x in hard_results),
                      "minimumHoldoutPair": min(x["maximum"] for x in holdout_pairs)}, sort_keys=True))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
