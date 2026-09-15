import hashlib, json, os, sqlite3, subprocess, sys, tempfile, unittest
from pathlib import Path

sys.path.insert(0,os.path.dirname(__file__))
import discovery

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/"data/discovery"

class DiscoveryTests(unittest.TestCase):
    def test_snapshot_acceptance_and_provenance(self):
        manifest=json.loads((DATA/"manifest-v1.json").read_text())
        self.assertEqual(manifest["recordCount"],256); self.assertGreaterEqual(manifest["twoNameCount"],100)
        self.assertGreaterEqual(manifest["outsideMappingPanels"],128); self.assertEqual(len(manifest["categories"]),8)
        self.assertLess(sum(p.stat().st_size for p in DATA.iterdir()),16*1024*1024)
        records=[json.loads(x) for x in (DATA/"records-v1.jsonl").read_text().splitlines()]
        self.assertEqual(len({x["canonicalIsomericSmiles"] for x in records}),256)
        self.assertTrue(all(x["validationStatus"]=="validated" and x["revisionUrl"].endswith(str(x["revision"])) for x in records))
        archive=json.loads((DATA/"wikidata-archive-v1.json").read_text())
        compact=lambda value: json.dumps(value,sort_keys=True,separators=(",",":")).encode()
        self.assertEqual(hashlib.sha256(compact(archive["queryResponse"])).hexdigest(),archive["queryResponseSha256"])
        self.assertTrue(all(hashlib.sha256(compact(x["response"])).hexdigest()==x["sha256"] for x in archive["batches"]))

    def test_exact_prefix_ambiguity_truncation_and_escaping(self):
        index=discovery.DiscoveryIndex(DATA/"discovery-v1.sqlite3")
        record=json.loads((DATA/"records-v1.jsonl").read_text().splitlines()[0]); name=record["names"][0]
        exact=index.search(name); self.assertTrue(exact["candidates"]); self.assertFalse(exact["truncated"])
        prefix=index.search(name[:max(1,len(name)//2)],prefix=True,limit=1); self.assertEqual(len(prefix["candidates"]),1)
        self.assertEqual(index.search("%",prefix=True)["candidates"],[])
        ambiguous=index.search("gasotransmitter"); self.assertGreaterEqual(len({x["canonicalIsomericSmiles"] for x in ambiguous["candidates"]}),2)
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/"records.jsonl"; a=dict(record,recordId="test:a",displayName="shared",names=["shared"],canonicalIsomericSmiles="CCO"); b=dict(record,recordId="test:b",displayName="shared",names=["shared"],canonicalIsomericSmiles="CCN")
            source.write_text(json.dumps(a)+"\n"+json.dumps(b)+"\n"); path=Path(tmp)/"index.db"; discovery.build_index(source,path)
            result=discovery.DiscoveryIndex(path).search("shared",limit=1); self.assertTrue(result["truncated"]); self.assertEqual(len(result["candidates"]),1)

    def test_conflict_conversion_and_cache_lifecycle(self):
        source={"schemaVersion":1,"sourceId":"Q1","revision":2,"retrievedAt":"2026-09-15T00:00:00Z","structure":"CCO","referenceStructure":"CCN","structureFormat":"smiles","names":[{"language":"en","value":"shared"}],"categories":[]}
        self.assertEqual(discovery.convert_record(source)["validationStatus"],"conflict")
        source.pop("referenceStructure"); source["inchiKey"]="LFQSCWFLJHTTHZ-UHFFFAOYSA-N"
        with self.assertRaisesRegex(discovery.DiscoveryError,"InChIKey support"):
            discovery.convert_record(source)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/"cache.db"; cache=discovery.DiscoveryCache(path,"data-1","rdkit-1")
            self.assertIsNone(cache.get("x")); self.assertTrue(cache.put("x",{"ids":[1]})); self.assertEqual(cache.get("x"),{"ids":[1]})
            cache.db.close(); changed=discovery.DiscoveryCache(path,"data-2","rdkit-1"); self.assertIsNone(changed.get("x")); self.assertTrue(changed.clear())
            peer=discovery.DiscoveryCache(path,"data-2","rdkit-1"); self.assertTrue(changed.put("a",{"value":1})); self.assertTrue(peer.put("b",{"value":2})); self.assertEqual(changed.get("b"),{"value":2}); peer.db.close()
            changed.db.close(); path.write_bytes(b"corrupt")
            with self.assertRaises(discovery.DiscoveryError): discovery.DiscoveryCache(path,"data-2","rdkit-1")

    def test_offline_archive_rebuild(self):
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([sys.executable,str(ROOT/"scripts/build-discovery.py"),"--output-dir",tmp],check=True)
            self.assertEqual((Path(tmp)/"records-v1.jsonl").read_bytes(),(DATA/"records-v1.jsonl").read_bytes())
            rebuilt=discovery.DiscoveryIndex(Path(tmp)/"discovery-v1.sqlite3")
            self.assertEqual(rebuilt.search("gasotransmitter"),discovery.DiscoveryIndex(DATA/"discovery-v1.sqlite3").search("gasotransmitter"))

if __name__=="__main__": unittest.main()
