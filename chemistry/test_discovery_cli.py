#!/usr/bin/env python3
"""Production C++ CLI -> supervised helper -> shipped SQLite discovery path."""
import json, os, subprocess, sys, tempfile, unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
CLI=Path(sys.argv.pop(1)).resolve()

class DiscoveryCliTests(unittest.TestCase):
    def invoke(self,*args,env=None):
        variables=dict(os.environ,IUPAC_CHEMISTRY_HELPER=str(ROOT/"chemistry/helper.py"),IUPAC_DISCOVERY_INDEX=str(ROOT/"data/discovery/discovery-v2.sqlite3"))
        if env: variables.update(env)
        run=subprocess.run([str(CLI),*args],text=True,capture_output=True,env=variables,timeout=30)
        self.assertEqual(run.returncode,0,run.stderr); return json.loads(run.stdout)

    def test_bounded_ambiguous_search_and_record_provenance(self):
        found=self.invoke("discover","--query","gasotransmitter","--limit","8")["discovery"]
        self.assertGreaterEqual(len({x["canonicalIsomericSmiles"] for x in found["candidates"]}),2)
        self.assertFalse(found["truncated"])
        prefix=self.invoke("discover","--query","hydro","--prefix","1","--limit","2")["discovery"]
        self.assertLessEqual(len(prefix["candidates"]),2); self.assertEqual(prefix["match"],"prefix")
        record=self.invoke("record","--record-id",found["candidates"][0]["recordId"])["record"]
        self.assertEqual(record["validationStatus"],"validated"); self.assertIn("revisionUrl",record)

    def test_cache_inspection_and_clear_do_not_require_network(self):
        with tempfile.TemporaryDirectory() as tmp:
            env={"IUPAC_GENERATED_CACHE":tmp}
            self.assertEqual(self.invoke("cache-inspect",env=env)["cache"],[])
            self.assertTrue(self.invoke("cache-clear",env=env)["cache"]["cleared"])

if __name__=="__main__": unittest.main()
