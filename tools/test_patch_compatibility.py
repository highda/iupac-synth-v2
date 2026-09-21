#!/usr/bin/env python3
"""Pre-D8 patch compatibility and the authored maximal bounds (#109, D8).

ARCHITECTURE "Decoder compatibility for added parameters": raising the bounds and widening the
catalog must leave every earlier patch not merely decodable but *identical in sound*. A pre-D8
fixture omits every post-v1 parameter and carries three envelopes; the same patch written out in
full — every post-v1 parameter at its descriptor default, four envelopes — must render to the same
PCM, so a default chosen so it shifts the signal fails here.

The digest itself is toolchain-specific (the sanitiser presets round differently), so the test
compares two renders from the same build rather than a hard-coded hash. The absolute before/after
digest of the bounds change is recorded on the leaf's PR.

The same run pins the authored maximal panel case at the D8 bounds, so the panel and
tests/MaximalPatch.hpp cannot drift apart silently.

usage: test_patch_compatibility.py <iupac-cli> <repository-root>
"""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

CLI, ROOT = sys.argv[1], pathlib.Path(sys.argv[2])
MIDI = ROOT / "data" / "panels" / "authored-synth" / "held-note.midi.json"
FIXTURE = ROOT / "tests" / "fixtures" / "pre-d8-patch.snapshot.json"


def render(snapshot, samples=144000):
    with tempfile.TemporaryDirectory() as directory:
        out = subprocess.run([CLI, "render", "--snapshot", str(snapshot), "--midi", str(MIDI),
                              "--output", str(pathlib.Path(directory) / "render.wav"),
                              "--sample-rate", "48000", "--block-size", "128", "--samples", str(samples)],
                             capture_output=True, text=True, check=True)
    return json.loads(out.stdout)


def catalog():
    out = subprocess.run([CLI, "inspect", "--stage", "catalog"], capture_output=True, text=True, check=True)
    return {module["id"]: module for module in json.loads(out.stdout)["modules"]}


def written_out_in_full(document):
    """The same patch as the production encoder writes it today: complete parameter set, four envelopes."""
    modules = catalog()
    filled = json.loads(json.dumps(document))
    for patch in ("basePatch", "editedPatch"):
        for node in filled[patch]["nodes"]:
            for parameter in modules[node["type"]]["parameters"]:
                if parameter["id"] not in node["parameters"]:
                    default = parameter["default"]
                    size = parameter.get("arraySize", 0)
                    node["parameters"][parameter["id"]] = [default] * size if size else default
        while len(filled[patch]["envelopes"]) < 4:
            filled[patch]["envelopes"].append({"attack": 0.01, "decay": 0.1, "sustain": 1.0, "release": 0.2})
    return filled


class CompatibilityTests(unittest.TestCase):
    def test_the_fixture_is_a_genuine_pre_d8_patch(self):
        document = json.loads(FIXTURE.read_text())
        modules = catalog()
        self.assertEqual(len(document["editedPatch"]["envelopes"]), 3)
        omitted = sum(1 for node in document["editedPatch"]["nodes"]
                      for parameter in modules[node["type"]]["parameters"] if parameter["id"] not in node["parameters"])
        self.assertGreater(omitted, 0, "the fixture must omit post-v1 parameters or it proves nothing")

    def test_a_pre_d8_patch_renders_exactly_like_its_fully_written_form(self):
        baseline = render(FIXTURE)["pcmSha256"]
        with tempfile.TemporaryDirectory() as directory:
            full = pathlib.Path(directory) / "full.snapshot.json"
            full.write_text(json.dumps(written_out_in_full(json.loads(FIXTURE.read_text()))))
            self.assertEqual(render(full)["pcmSha256"], baseline)

    def test_the_spectral_shape_controls_never_rewrite_the_stored_spectrum(self):
        """#121: `harmonicityMorph`/`oddEvenBalance`/`symmetry` reshape the render, not the patch.

        The morph pulling a ratio toward its nearest integer is the obvious thing to implement by
        rewriting `partialRatios`, and that would silently destroy the chemistry-derived spectrum on
        the first save and stop the control being modulatable. This drives a real load/save round
        trip through the production decoder and encoder (`inspect --stage patch`) and asserts the
        arrays come back byte for byte, with the three controls off their defaults.
        """
        document = json.loads(FIXTURE.read_text())
        harmonic = next(n for n in document["editedPatch"]["nodes"] if n["type"] == "harmonic")
        stored = [1.07, 2.13, 3.41, 4.02, 5.77, 6.31, 7.19, 8.63,
                  9.05, 10.44, 11.28, 12.91, 13.36, 14.72, 15.11, 16.58]
        harmonic["parameters"]["partialRatios"] = stored
        amplitudes = harmonic["parameters"]["partialAmplitudes"]
        harmonic["parameters"]["harmonicityMorph"] = 1.0
        harmonic["parameters"]["oddEvenBalance"] = 0.6
        harmonic["parameters"]["symmetry"] = 0.2
        with tempfile.TemporaryDirectory() as directory:
            snapshot = pathlib.Path(directory) / "shaped.snapshot.json"
            snapshot.write_text(json.dumps(document))
            self.assertTrue(render(snapshot)["pcmSha256"])  # the shaped patch renders on the production path
            patch = pathlib.Path(directory) / "shaped.patch.json"
            patch.write_text(json.dumps(document["editedPatch"]))
            out = subprocess.run([CLI, "inspect", "--stage", "patch", "--patch", str(patch)],
                                 capture_output=True, text=True, check=True)
        node = next(n for n in json.loads(out.stdout)["nodes"] if n["type"] == "harmonic")
        self.assertEqual(node["parameters"]["partialRatios"], stored)
        self.assertEqual(node["parameters"]["partialAmplitudes"], amplitudes)
        self.assertEqual(node["parameters"]["harmonicityMorph"], 1.0)

    def test_authored_maximal_panel_case_sits_at_the_d8_bounds(self):
        patch = json.loads((ROOT / "data" / "panels" / "authored-synth" / "maximal.snapshot.json").read_text())["editedPatch"]
        self.assertEqual((len(patch["nodes"]), len(patch["edges"]), len(patch["matrix"]), len(patch["envelopes"])), (16, 48, 40, 4))
        self.assertEqual(sum(1 for e in patch["edges"] if e.get("port", "in") != "in"), 2)

    def test_the_authored_panel_rejections_hold(self):
        with tempfile.TemporaryDirectory() as directory:
            out = subprocess.run([CLI, "verify-panel", "--panel", str(ROOT / "data" / "panels" / "authored-synth" / "panel.json"),
                                  "--output-dir", directory], capture_output=True, text=True, check=True)
        self.assertEqual(len(json.loads(out.stdout)["rejections"]), 7)


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1], verbosity=2)
