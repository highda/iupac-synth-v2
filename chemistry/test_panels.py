import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "chemistry"))
import helper

PANELS = ("identity", "broad-development", "legacy-hard", "holdout")
JAR = ROOT / "third_party/opsin/opsin-cli-2.8.0.jar"


class ChemistryPanelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.panels = {name: json.loads((ROOT / "data/panels" / f"{name}.json").read_text()) for name in PANELS}
        cls.expected = json.loads((ROOT / "tests/fixtures/chemistry-analysis-v1.json").read_text())
        cls.analyses = {row["id"]: row["response"]["analysis"] for row in cls.expected["records"]}

    def test_fixed_scope_and_provenance(self):
        self.assertEqual(len(self.panels["broad-development"]["records"]), 24)
        self.assertEqual(len(self.panels["holdout"]["records"]), 12)
        groups = {}
        for record in self.panels["identity"]["records"]:
            if record["identityGroup"]:
                groups.setdefault(record["identityGroup"], []).append(record)
        self.assertGreaterEqual(len(groups), 20)
        self.assertTrue(all(len(records) >= 2 for records in groups.values()))
        for panel in self.panels.values():
            self.assertEqual(panel["backend"]["rdkitVersion"], helper.rdBase.rdkitVersion)
            for record in panel["records"]:
                self.assertTrue(record["id"] and record["source"] and record["license"] and record["assertions"])

    def test_independent_assertions_and_identity(self):
        for panel in self.panels.values():
            for record in panel["records"]:
                analysis = self.analyses[record["id"]]
                assertions = record["assertions"]
                descriptors = analysis["descriptors"]
                self.assertEqual(descriptors["heavyAtoms"], assertions["heavyAtoms"], record["id"])
                self.assertEqual(descriptors["formalCharge"], assertions["formalCharge"], record["id"])
                actual = {key: value for key, value in descriptors["elementCounts"].items() if value}
                self.assertEqual(actual, assertions["elementCounts"], record["id"])
                for motif in helper.MOTIFS:
                    name = motif[0]
                    if name in assertions:
                        self.assertEqual(descriptors["motifCounts"][name], assertions[name], record["id"])
                if "rings" in assertions:
                    self.assertEqual(descriptors["ringCount"], assertions["rings"], record["id"])
        grouped = {}
        for record in self.panels["identity"]["records"]:
            if record["identityGroup"]:
                grouped.setdefault(record["identityGroup"], set()).add(self.analyses[record["id"]]["canonicalIsomericSmiles"])
        self.assertTrue(all(len(canonicals) == 1 for canonicals in grouped.values()))
        deliberate = [r for r in self.panels["identity"]["records"] if "non-equivalent" in r["annotation"]]
        self.assertEqual(len({self.analyses[r["id"]]["canonicalIsomericSmiles"] for r in deliberate}), len(deliberate))

    def test_holdout_is_frozen_and_disjoint(self):
        manifest = json.loads((ROOT / "data/panels/chemistry-manifest.json").read_text())
        self.assertIs(manifest["frozenBeforeCalibration"], True)
        development = {self.analyses[r["id"]]["canonicalIsomericSmiles"] for r in self.panels["broad-development"]["records"]}
        holdout = [self.analyses[r["id"]]["canonicalIsomericSmiles"] for r in self.panels["holdout"]["records"]]
        self.assertEqual(len(holdout), len(set(holdout)))
        self.assertFalse(development.intersection(holdout))

    def test_hard_pair_manifest_exact(self):
        manifest = json.loads((ROOT / "data/panels/legacy-hard-pairs.json").read_text())
        self.assertEqual(len(manifest["pairs"]), 41)
        self.assertEqual(len({tuple(pair) for pair in manifest["pairs"]}), 41)
        ids = {r["id"] for r in self.panels["legacy-hard"]["records"]}
        self.assertTrue(all(len(pair) == 2 and set(pair) <= ids for pair in manifest["pairs"]))

    def test_source_helper_protocol_for_every_record(self):
        env = dict(os.environ, IUPAC_OPSIN_JAR=str(JAR))
        for panel in self.panels.values():
            for record in panel["records"]:
                request = {"protocolVersion":1,"requestId":record["id"],"mode":record["mode"],"text":record["text"]}
                run = subprocess.run([sys.executable, str(ROOT / "chemistry/helper.py")], input=json.dumps(request), text=True, capture_output=True, env=env, timeout=15)
                self.assertEqual(run.returncode, 0, record["id"] + run.stdout)
                self.assertEqual(json.loads(run.stdout)["analysis"], self.analyses[record["id"]])
        invalid = json.loads((ROOT / "data/panels/adversarial-analysis.json").read_text())["records"]
        for record in invalid:
            if record.get("surface") not in (None, "analysis"):
                continue
            request = {"protocolVersion":1,"requestId":record["id"],"mode":record["mode"],"text":record["text"]}
            run = subprocess.run([sys.executable, str(ROOT / "chemistry/helper.py")], input=json.dumps(request), text=True, capture_output=True, env=env, timeout=15)
            response = json.loads(run.stdout)
            self.assertEqual(run.returncode, 2, record["id"])
            self.assertEqual(response["status"], "error")
            self.assertIn(record["expectedDiagnostic"], response["diagnostic"])
            self.assertEqual(run.stderr, "", record["id"])

    def test_manifest_hashes_and_reproducible_generation(self):
        manifest = json.loads((ROOT / "data/panels/chemistry-manifest.json").read_text())
        for relative, expected_hash in manifest["files"].items():
            self.assertEqual(hashlib.sha256((ROOT / relative).read_bytes()).hexdigest(), expected_hash, relative)
        for row in self.expected["records"]:
            encoded = json.dumps(row["response"], sort_keys=True, separators=(",", ":")).encode()
            self.assertEqual(hashlib.sha256(encoded).hexdigest(), row["responseSha256"])
        with tempfile.TemporaryDirectory() as directory:
            run = subprocess.run([sys.executable, str(ROOT / "scripts/generate-chemistry-panels.py"), "--output-root", directory], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stderr)
            for relative in (*manifest["files"], "data/panels/chemistry-manifest.json"):
                self.assertEqual((ROOT / relative).read_bytes(), (Path(directory) / relative).read_bytes(), relative)


if __name__ == "__main__":
    unittest.main()
