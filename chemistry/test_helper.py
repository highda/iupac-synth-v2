import json
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import helper

JAR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "third_party", "opsin", "opsin-cli-2.8.0.jar"))


class HelperTests(unittest.TestCase):
    def analysis(self, text):
        return helper.analyze("smiles", text, JAR)

    def test_canonical_equivalence_and_fragments(self):
        self.assertEqual(self.analysis("CCO")["canonicalIsomericSmiles"], self.analysis("OCC")["canonicalIsomericSmiles"])
        salt = self.analysis("[Na+].[Cl-]")
        self.assertEqual(salt["descriptors"]["formalCharge"], 0)
        self.assertEqual(salt["descriptors"]["elementCounts"]["other"], 1)
        self.assertIn(".", salt["canonicalIsomericSmiles"])

    def test_stereo_isotope_and_charge_are_retained(self):
        self.assertNotEqual(self.analysis("F[C@H](Cl)Br")["canonicalIsomericSmiles"], self.analysis("F[C@@H](Cl)Br")["canonicalIsomericSmiles"])
        isotope = self.analysis("[13CH3][NH3+]")
        self.assertEqual(isotope["detail"]["atoms"][0]["isotope"], 13)
        self.assertEqual(isotope["descriptors"]["formalCharge"], 1)

    def test_descriptors_and_motifs(self):
        aspirin = self.analysis("CC(=O)Oc1ccccc1C(=O)O")
        d = aspirin["descriptors"]
        self.assertEqual(d["heavyAtoms"], 13)
        self.assertEqual(d["elementCounts"]["C"], 9)
        self.assertEqual(d["elementCounts"]["O"], 4)
        self.assertEqual(d["motifCounts"]["carbonyl"], 2)
        self.assertGreaterEqual(d["motifCounts"]["ether"], 1)

    def test_name_uses_opsin(self):
        ethanol = helper.analyze("name", "ethanol", JAR)
        self.assertEqual(ethanol["canonicalIsomericSmiles"], "CCO")
        self.assertEqual(ethanol["backend"]["opsinSha256"], helper.OPSIN_SHA256)

    def test_bounded_errors_and_protocol(self):
        for value in ("C*", "CC>>CO", "C |$foo$|"):
            with self.assertRaises(helper.InputError):
                self.analysis(value)
        with self.assertRaises(helper.InputError):
            helper.analyze("smiles", "C" * 4097, JAR)
        with self.assertRaises(helper.InputError):
            helper.analyze("smiles", "   ", JAR)
        with self.assertRaises(helper.InputError):
            helper.process({"protocolVersion": 1, "requestId": True, "mode": "smiles", "text": "CCO"}, JAR)
        response = helper.process({"protocolVersion": 1, "requestId": "x", "mode": "smiles", "text": "CCO"}, JAR)
        self.assertEqual(response["requestId"], "x")
        self.assertEqual(response["status"], "ok")

    def test_child_entrypoint_json_only(self):
        request = json.dumps({"protocolVersion": 1, "requestId": 7, "mode": "smiles", "text": "CCO"})
        env = dict(os.environ, IUPAC_OPSIN_JAR=JAR)
        run = subprocess.run([sys.executable, helper.__file__], input=request, text=True, capture_output=True, env=env, timeout=15)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)["analysis"]["canonicalIsomericSmiles"], "CCO")
        self.assertEqual(run.stderr, "")

        bad = subprocess.run([sys.executable, helper.__file__], input="{}", text=True, capture_output=True, env=env, timeout=15)
        self.assertEqual(bad.returncode, 2)
        self.assertEqual(json.loads(bad.stdout)["status"], "error")
        self.assertEqual(bad.stderr, "")


if __name__ == "__main__":
    unittest.main()
