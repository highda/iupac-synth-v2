#!/usr/bin/env python3
"""Mapper-coverage invariant (#105, D8).

docs/architecture/CHEMISTRY.md#mapper-coverage-invariant: no catalog parameter may be
dead for chemistry. The catalog is read through the production CLI
(`iupac-cli inspect --stage catalog`), never a transcribed copy, so adding a parameter
without declaring its mapping fails ctest. Runs in the chemistry-OFF build as well.

usage: test_mapping_coverage.py <iupac-cli> <mapping-coverage.json>
"""
import json
import subprocess
import sys
import unittest

AXES = {"density", "brightness", "rigidity", "roughness", "decay", "harmonicity", "motion"}
DETAIL = {"bondOrderMean", "bondOrderSpread", "heteroPlacement", "motifPlacement"}
DRIVERS = AXES | DETAIL
STATUSES = {"mapped", "provisional"}
REQUIRED = ("module", "parameter", "status", "ruleId", "drivers", "note")
# Every parameter kind the descriptor vocabulary defines; all of them need an entry.
COVERED_KINDS = {"continuous", "discrete", "convenience", "coefficientArray"}

CLI, COVERAGE = sys.argv[1], sys.argv[2]


def catalog():
    out = subprocess.run([CLI, "inspect", "--stage", "catalog"], capture_output=True, text=True, check=True)
    return json.loads(out.stdout)


class CoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = catalog()
        with open(COVERAGE, encoding="utf-8") as handle:
            cls.coverage = json.load(handle)
        cls.entries = cls.coverage["entries"]
        cls.declared = {(e.get("module"), e.get("parameter")) for e in cls.entries}
        cls.expected = {
            (m["id"], p["id"])
            for m in cls.catalog["modules"]
            for p in m["parameters"]
            if p["kind"] in COVERED_KINDS
        }

    def test_every_catalog_parameter_has_an_entry(self):
        missing = sorted(self.expected - self.declared)
        self.assertEqual(missing, [], f"catalog parameters with no mapping entry (dead for chemistry): {missing}")

    def test_no_entry_names_an_unknown_parameter(self):
        stale = sorted(self.declared - self.expected)
        self.assertEqual(stale, [], f"mapping entries for parameters the catalog does not have: {stale}")

    def test_entries_are_unique(self):
        self.assertEqual(len(self.declared), len(self.entries), "duplicate module/parameter entry")

    def test_every_kind_in_the_catalog_is_one_we_cover(self):
        kinds = {p["kind"] for m in self.catalog["modules"] for p in m["parameters"]}
        unknown = sorted(kinds - COVERED_KINDS)
        self.assertEqual(unknown, [], f"unknown parameter kind: extend COVERED_KINDS and the invariant first: {unknown}")

    def test_entries_are_well_formed_and_no_driver_list_is_empty(self):
        for entry in self.entries:
            label = f"{entry.get('module')}.{entry.get('parameter')}"
            for field in REQUIRED:
                self.assertIn(field, entry, f"{label} is missing '{field}'")
            self.assertIn(entry["status"], STATUSES, f"{label} has status {entry['status']!r}")
            self.assertTrue(entry["ruleId"], f"{label} has no ruleId")
            self.assertTrue(entry["drivers"], f"{label} has an empty drivers list: that is a failure, not a status")
            unknown = sorted(set(entry["drivers"]) - DRIVERS)
            self.assertEqual(unknown, [], f"{label} names drivers that are not SonicIntent axes or structuralDetail fields: {unknown}")

    def test_provisional_entries_name_their_owning_leaf(self):
        for entry in self.entries:
            if entry["status"] == "provisional":
                self.assertTrue(entry["note"], f"{entry['module']}.{entry['parameter']} is provisional with no owning leaf in 'note'")


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1], verbosity=2)
