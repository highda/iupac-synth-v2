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
# C5, an octave above the `keytrack` reference note, so the control is not inert (#122).
MIDI_C5 = ROOT / "data" / "panels" / "authored-synth" / "held-note-c5.midi.json"
FIXTURE = ROOT / "tests" / "fixtures" / "pre-d8-patch.snapshot.json"
SUB_SLOT = ROOT / "data" / "panels" / "authored-synth" / "sub-slot.snapshot.json"


def render(snapshot, samples=144000, midi=MIDI):
    with tempfile.TemporaryDirectory() as directory:
        out = subprocess.run([CLI, "render", "--snapshot", str(snapshot), "--midi", str(midi),
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

    def render_document(self, document, samples=144000, midi=MIDI):
        with tempfile.TemporaryDirectory() as directory:
            snapshot = pathlib.Path(directory) / "case.snapshot.json"
            snapshot.write_text(json.dumps(document))
            return render(snapshot, samples, midi)

    def sub_only(self, **overrides):
        """The authored sub-slot case with the harmonic source removed: the sub is the only voice."""
        document = json.loads(SUB_SLOT.read_text())
        patch = document["editedPatch"]
        patch["nodes"] = [n for n in patch["nodes"] if n["id"] != "lead"]
        patch["edges"] = [e for e in patch["edges"] if e["source"] != "lead"]
        next(n for n in patch["nodes"] if n["id"] == "bass")["parameters"].update(overrides)
        return document

    def test_the_sub_slot_is_a_real_oscillator_on_the_production_path(self):
        """#122: #109 registered `sub` as a silent skeleton; here it has to make sound.

        The authored panel case drives the whole decode/compile/render path, so a sub that is
        registered but never reaches the audio thread fails as loudly as a silent one.
        """
        sounding = self.render_document(self.sub_only())
        self.assertGreater(sounding["rms"], 0.01, "the sub slot renders silence")
        self.assertEqual(self.render_document(self.sub_only(outputLevel=0.0))["rms"], 0.0)
        # The octave and the waveform are both real: each changes what the sub renders.
        self.assertNotEqual(self.render_document(self.sub_only(octave=-1.0))["pcmSha256"], sounding["pcmSha256"])
        self.assertNotEqual(self.render_document(self.sub_only(waveform=0.0))["pcmSha256"], sounding["pcmSha256"])

    def test_the_pitch_block_transposes_a_general_source_on_the_production_path(self):
        """#122: `octave`, `coarse`, `fine` and `keytrack` are one semitone offset.

        One octave up and twelve coarse semitones up must be the same render, and the catalog
        defaults must leave the source exactly where a pre-D8 patch put it.
        """
        def lead(midi=MIDI, **overrides):
            document = json.loads(SUB_SLOT.read_text())
            patch = document["editedPatch"]
            patch["nodes"] = [n for n in patch["nodes"] if n["id"] != "bass"]
            patch["edges"] = [e for e in patch["edges"] if e["source"] != "bass"]
            next(n for n in patch["nodes"] if n["id"] == "lead")["parameters"].update(
                dict({"octave": 0.0, "coarse": 0.0, "fine": 0.0, "keytrack": 1.0}, **overrides))
            return self.render_document(document, midi=midi)

        defaults = lead()
        self.assertGreater(defaults["rms"], 0.01)
        self.assertEqual(lead(octave=1.0)["pcmSha256"], lead(coarse=12.0)["pcmSha256"])
        self.assertEqual(lead(fine=100.0)["pcmSha256"], lead(coarse=1.0)["pcmSha256"])
        for transposed in (lead(octave=1.0), lead(coarse=7.0), lead(fine=-8.0)):
            self.assertNotEqual(transposed["pcmSha256"], defaults["pcmSha256"])
        # `keytrack` scales the note's distance from the C4 reference, so it is inert at C4 itself
        # and an octave down at C5 — which is exactly `octave` -1 there.
        self.assertEqual(lead(keytrack=0.0)["pcmSha256"], defaults["pcmSha256"])
        at_c5 = lead(midi=MIDI_C5)
        self.assertNotEqual(lead(midi=MIDI_C5, keytrack=0.0)["pcmSha256"], at_c5["pcmSha256"])
        self.assertEqual(lead(midi=MIDI_C5, keytrack=0.0)["pcmSha256"], lead(midi=MIDI_C5, octave=-1.0)["pcmSha256"])

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
