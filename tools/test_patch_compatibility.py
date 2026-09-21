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

    # ---- D8 modulator settings (#125) -----------------------------------------------------------

    def modulated(self, lfos=None, envelopes=None, rows=None, **overrides):
        """The authored curve case with its modulator settings replaced, rendered through the CLI."""
        document = json.loads((ROOT / "data" / "panels" / "authored-synth" / "modulation-curves.snapshot.json").read_text())
        patch = document["editedPatch"]
        if lfos is not None:
            patch["lfos"] = lfos
        if envelopes is not None:
            patch["envelopes"] = envelopes
        if rows is not None:
            patch["matrix"] = rows
        patch.update(overrides)
        document["basePatch"] = json.loads(json.dumps(patch))
        return self.render_document(document)

    @staticmethod
    def flat_envelopes(**curves):
        return [dict({"attack": 0.05, "decay": 0.4, "sustain": 0.6, "release": 0.35,
                      "attackCurve": 0.0, "decayCurve": 0.0, "releaseCurve": 0.0}, **curves) for _ in range(4)]

    @staticmethod
    def lfo(waveform, rate=3.0, fade=0.0, sync="free", division="1/4"):
        return {"rate": rate, "waveform": waveform, "fadeMs": fade, "syncMode": sync, "syncDivision": division}

    def test_a_stage_curve_of_zero_is_the_existing_adsr_shape_exactly(self):
        """A curve of 0 must be the literal identity, or every pre-D8 patch changes its sound."""
        flat = self.modulated(envelopes=self.flat_envelopes())
        omitted = [{k: v for k, v in e.items() if not k.endswith("Curve")} for e in self.flat_envelopes()]
        self.assertEqual(self.modulated(envelopes=omitted)["pcmSha256"], flat["pcmSha256"])
        # ...and a bent stage is audibly a different envelope, on each of the three stages.
        for stage in ("attackCurve", "decayCurve", "releaseCurve"):
            bent = self.modulated(envelopes=self.flat_envelopes(**{stage: 1.0}))
            self.assertNotEqual(bent["pcmSha256"], flat["pcmSha256"], f"{stage} is inert")
            self.assertGreater(bent["rms"], 0.0)
            # The stage keeps its endpoints, so the render stays inside the same peak envelope.
            self.assertLessEqual(bent["peak"], flat["peak"] * 1.05)

    def test_the_fourth_envelope_is_a_matrix_source_and_defaults_when_a_patch_stores_three(self):
        rows = [{"id": "e4-q", "enabled": True, "source": "e4", "destinationNode": "tone",
                 "destinationParameter": "q", "depth": 1.0}]
        slow = self.modulated(rows=rows, envelopes=self.flat_envelopes()[:3]
                              + [{"attack": 2.0, "decay": 4.0, "sustain": 0.0, "release": 6.0}])
        fast = self.modulated(rows=rows, envelopes=self.flat_envelopes()[:3]
                              + [{"attack": 0.001, "decay": 0.01, "sustain": 0.0, "release": 0.02}])
        self.assertNotEqual(slow["pcmSha256"], fast["pcmSha256"], "E4 does not reach the matrix")
        # Three stored envelopes stay valid and decode with E4 at its construction default, which is
        # the same render as writing that default out in full.
        three = self.modulated(rows=rows, envelopes=self.flat_envelopes()[:3])
        default_e4 = self.modulated(rows=rows, envelopes=self.flat_envelopes()[:3]
                                    + [{"attack": 0.01, "decay": 0.1, "sustain": 1.0, "release": 0.2}])
        self.assertEqual(three["pcmSha256"], default_e4["pcmSha256"])

    def test_the_persisted_source_list_appends_e4(self):
        out = subprocess.run([CLI, "inspect", "--stage", "catalog"], capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(out.stdout)["modulationSources"],
                         ["e1", "e2", "e3", "l1", "l2", "velocity", "keyTracking", "pitchBend", "cc1",
                          "macro1", "macro2", "macro3", "macro4", "e4"])

    def test_every_lfo_shape_renders_and_the_first_two_indices_did_not_move(self):
        rows = [{"id": "l1-cutoff", "enabled": True, "source": "l1", "destinationNode": "tone",
                 "destinationParameter": "cutoff", "depth": 0.6}]
        digests = {}
        for shape in ("sine", "triangle", "saw", "square", "sampleHold", "randomSmooth"):
            result = self.modulated(lfos=[self.lfo(shape), self.lfo("sine", rate=0.5)], rows=rows,
                                    envelopes=self.flat_envelopes())
            self.assertGreater(result["rms"], 0.0)
            digests[shape] = result["pcmSha256"]
        self.assertEqual(len(set(digests.values())), len(digests), "two LFO shapes render identically")

    def test_the_random_lfo_shapes_repeat_exactly(self):
        """V2: `sampleHold`/`randomSmooth` draw from the patch-seeded stream, never wall-clock entropy."""
        rows = [{"id": "l1-cutoff", "enabled": True, "source": "l1", "destinationNode": "tone",
                 "destinationParameter": "cutoff", "depth": 0.8}]
        for shape in ("sampleHold", "randomSmooth"):
            renders = [self.modulated(lfos=[self.lfo(shape, rate=8.0), self.lfo("sine")], rows=rows,
                                      envelopes=self.flat_envelopes(), noiseSeed=4242) for _ in range(2)]
            self.assertEqual(renders[0]["pcmSha256"], renders[1]["pcmSha256"], f"{shape} is not reproducible")
            other_seed = self.modulated(lfos=[self.lfo(shape, rate=8.0), self.lfo("sine")], rows=rows,
                                        envelopes=self.flat_envelopes(), noiseSeed=99)
            self.assertNotEqual(other_seed["pcmSha256"], renders[0]["pcmSha256"], f"{shape} ignores the patch seed")

    def test_the_lfo_fade_and_tempo_sync_are_real(self):
        rows = [{"id": "l1-cutoff", "enabled": True, "source": "l1", "destinationNode": "tone",
                 "destinationParameter": "cutoff", "depth": 0.8}]
        envelopes = self.flat_envelopes()
        immediate = self.modulated(lfos=[self.lfo("triangle"), self.lfo("sine")], rows=rows, envelopes=envelopes)
        # 0 ms is exactly no fade; a real fade changes the first seconds of the render.
        self.assertEqual(self.modulated(lfos=[self.lfo("triangle", fade=0.0), self.lfo("sine")], rows=rows,
                                        envelopes=envelopes)["pcmSha256"], immediate["pcmSha256"])
        self.assertNotEqual(self.modulated(lfos=[self.lfo("triangle", fade=2000.0), self.lfo("sine")], rows=rows,
                                           envelopes=envelopes)["pcmSha256"], immediate["pcmSha256"])
        # Sync follows the 120 BPM fallback the render manifest records, so a division is a rate.
        quarter = self.modulated(lfos=[self.lfo("triangle", sync="sync", division="1/4"), self.lfo("sine")],
                                 rows=rows, envelopes=envelopes)
        self.assertEqual(self.modulated(lfos=[self.lfo("triangle", rate=2.0, sync="sync", division="1/4"),
                                              self.lfo("sine")], rows=rows, envelopes=envelopes)["pcmSha256"],
                         quarter["pcmSha256"], "a synced LFO still reads its free-running rate")
        self.assertEqual(self.modulated(lfos=[self.lfo("triangle", rate=2.0), self.lfo("sine")], rows=rows,
                                        envelopes=envelopes)["pcmSha256"], quarter["pcmSha256"],
                         "1/4 at 120 BPM is not 2 Hz")
        self.assertNotEqual(self.modulated(lfos=[self.lfo("triangle", sync="sync", division="1/8"), self.lfo("sine")],
                                           rows=rows, envelopes=envelopes)["pcmSha256"], quarter["pcmSha256"])

    def test_the_authored_modulation_cases_cover_every_envelope_and_every_shape(self):
        root = ROOT / "data" / "panels" / "authored-synth"
        patches = [json.loads((root / name).read_text())["editedPatch"]
                   for name in ("modulation-curves.snapshot.json", "modulation-random.snapshot.json",
                                "modulation-sync.snapshot.json")]
        shapes = {lfo["waveform"] for patch in patches for lfo in patch["lfos"]}
        self.assertEqual(shapes, {"sine", "triangle", "saw", "square", "sampleHold", "randomSmooth"})
        curves = json.loads((root / "modulation-curves.snapshot.json").read_text())["editedPatch"]
        self.assertEqual(len(curves["envelopes"]), 4)
        self.assertTrue(all(any(e.get(stage) for stage in ("attackCurve", "decayCurve", "releaseCurve"))
                            for e in curves["envelopes"]), "an authored envelope has no stage curve")
        self.assertIn("e4", {row["source"] for row in curves["matrix"]})

    def test_the_authored_family_cases_reach_pad_stab_spike_and_bell(self):
        # D8's finding was that the instrument converged on metallic bell-like results because pads,
        # stabs and spikes were structurally unreachable (#104). These four authored cases are the
        # standing proof that the widened engine reaches all four families, so each one has to keep
        # using the breadth that makes its family possible rather than drifting back to a default.
        root = ROOT / "data" / "panels" / "authored-synth"
        panel = {case["id"]: case for case in json.loads((root / "panel.json").read_text())["cases"]}
        for family in ("family-pad", "family-stab", "family-spike", "family-bell"):
            self.assertIn(family, panel, f"{family} is not in the authored panel set")
        patches = {f: json.loads((root / panel[f]["snapshot"]).read_text())["editedPatch"]
                   for f in ("family-pad", "family-stab", "family-spike", "family-bell")}

        def params(patch, kind):
            return [n["parameters"] for n in patch["nodes"] if n["type"] == kind]

        pad = patches["family-pad"]
        self.assertTrue(any(p["unisonVoices"] >= 5 and p["drift"] > 0 for p in params(pad, "harmonic")),
                        "the pad case no longer uses a wide drifting unison stack")
        self.assertEqual({n["type"] for n in pad["nodes"]} & {"chorus", "delay", "reverb", "width"},
                         {"chorus", "delay", "reverb", "width"}, "the pad case no longer uses the whole tail")
        self.assertGreater(pad["envelopes"][0]["attack"], 0.5, "a pad needs a slow attack")

        stab = patches["family-stab"]
        self.assertLess(stab["envelopes"][0]["attack"], 0.01)
        self.assertEqual(stab["envelopes"][0]["sustain"], 0.0, "a stab does not sustain")
        self.assertTrue(any(p["envAmount"] >= 0.5 for p in params(stab, "filter")),
                        "the stab case no longer snaps its filter with the envelope")
        self.assertTrue(any(p["curve"] != 0 for p in params(stab, "shaper")), "the stab case lost its shaper curve")

        spike = patches["family-spike"]
        self.assertLess(spike["envelopes"][0]["decay"], 0.1)
        self.assertTrue(any(e.get("port") == "modIn" for e in spike["edges"]),
                        "the spike case no longer drives the audio-rate modIn port")
        self.assertTrue(any(p["envAmount"] < 0 for p in params(spike, "filter")),
                        "the spike case lost its inverted filter envelope")

        bell = patches["family-bell"]
        self.assertTrue(any(e.get("port") == "exciteIn" for e in bell["edges"]),
                        "the bell case no longer excites the resonator through exciteIn")
        self.assertTrue(any(p["mode"] == 1 and p["exciteDepth"] > 0 for p in params(bell, "resonator")),
                        "the bell case no longer uses the modal resonator")
        self.assertTrue(any(p["harmonicityMorph"] == 0 for p in params(bell, "harmonic")),
                        "a bell keeps its inharmonic ratios: harmonicityMorph must stay 0")

    def test_authored_maximal_panel_case_sits_at_the_d8_bounds(self):
        patch = json.loads((ROOT / "data" / "panels" / "authored-synth" / "maximal.snapshot.json").read_text())["editedPatch"]
        self.assertEqual((len(patch["nodes"]), len(patch["edges"]), len(patch["matrix"]), len(patch["envelopes"])), (16, 48, 40, 4))
        self.assertEqual(sum(1 for e in patch["edges"] if e.get("port", "in") != "in"), 2)

    def test_the_authored_panel_rejections_hold(self):
        panel_path = ROOT / "data" / "panels" / "authored-synth" / "panel.json"
        authored = json.loads(panel_path.read_text())["rejections"]
        with tempfile.TemporaryDirectory() as directory:
            out = subprocess.run([CLI, "verify-panel", "--panel", str(panel_path),
                                  "--output-dir", directory], capture_output=True, text=True, check=True)
        # Every authored rejection has to come back with its exact error: a relaxed bound or a
        # relaxed router constraint would drop the case rather than fail it.
        reported = json.loads(out.stdout)["rejections"]
        self.assertEqual([(r["id"], r["error"]) for r in reported], [(r["id"], r["error"]) for r in authored])


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1], verbosity=2)
