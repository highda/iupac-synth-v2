#!/usr/bin/env python3
"""Mapper-coverage invariant (#105, D8).

docs/architecture/CHEMISTRY.md#mapper-coverage: every catalog parameter has exactly one
entry saying whether mapper 3 assigns it (D13 dropped "no parameter may be dead"). The catalog is read through the production CLI
(`iupac-cli inspect --stage catalog`), never a transcribed copy, so adding a parameter
without declaring its mapping fails ctest. Runs in the chemistry-OFF build as well.

usage: test_mapping_coverage.py <iupac-cli> <mapping-coverage.json>
"""
import json
import subprocess
import sys
import unittest

import re
from pathlib import Path

# Drivers are the trait names of SonicIntent 2, read from the production header (mapper 3, D13).
_HEADER = (Path(__file__).resolve().parents[1] / "include/iupac/chemistry/Mapping.hpp").read_text()
DRIVERS = set(re.findall(r"X\((\w+)\)", _HEADER.split("#define IUPAC_TRAIT_FIELDS(X)")[1].split("struct Traits")[0]))
STATUSES = {"mapped", "offPanel", "unmapped"}
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

    def test_entries_are_well_formed(self):
        self.assertGreater(len(DRIVERS), 30, "trait vocabulary was not read from Mapping.hpp")
        for entry in self.entries:
            label = f"{entry.get('module')}.{entry.get('parameter')}"
            for field in REQUIRED:
                self.assertIn(field, entry, f"{label} is missing '{field}'")
            self.assertIn(entry["status"], STATUSES, f"{label} has status {entry['status']!r}")
            if entry["status"] == "unmapped":
                # D13: curated beats complete. An unreachable parameter is allowed, but it must say so.
                self.assertTrue(entry["note"], f"{label} is unmapped with no note")
                self.assertFalse(entry["ruleId"] or entry["drivers"], f"{label} is unmapped yet names a rule or drivers")
                continue
            self.assertTrue(entry["ruleId"], f"{label} has no ruleId")
            unknown = sorted(set(entry["drivers"]) - DRIVERS)
            self.assertEqual(unknown, [], f"{label} names drivers that are not SonicIntent traits: {unknown}")

    def test_the_mapper_reaches_most_of_the_catalog(self):
        # Not an invariant on any single parameter, only a floor so the table cannot quietly empty out.
        reached = sum(entry["status"] != "unmapped" for entry in self.entries)
        self.assertGreaterEqual(reached / len(self.entries), 0.6, "the mapper assigns fewer than 60% of catalog parameters")


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1], verbosity=2)
