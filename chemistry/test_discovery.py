import hashlib, json, os, sqlite3, subprocess, sys, tempfile, unittest
from pathlib import Path

sys.path.insert(0,os.path.dirname(__file__))
import discovery

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/"data/discovery"
# An exact name the Tier A snapshot gives to several distinct structures.
AMBIGUOUS_NAME="gasotransmitter"

def _build_module():
    import importlib.util
    spec=importlib.util.spec_from_file_location("build_discovery",ROOT/"scripts/build-discovery.py")
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module); return module

class DiscoveryTests(unittest.TestCase):
    def test_snapshot_acceptance_and_provenance(self):
        # Tier A (#138): every backend-viable sitelinked structure, one record per canonical structure.
        manifest=json.loads((DATA/"manifest-v2.json").read_text())
        self.assertGreaterEqual(manifest["recordCount"],20000); self.assertLessEqual(manifest["recordCount"],discovery.MAX_RECORDS)
        self.assertGreaterEqual(manifest["twoNameCount"],100); self.assertGreaterEqual(manifest["outsideMappingPanels"],128)
        self.assertEqual(manifest["tier"],"A"); self.assertTrue(manifest["rejected"])
        records=discovery.read_records(DATA/"records-v2.jsonl.gz")
        self.assertEqual(len(records),manifest["recordCount"]); self.assertEqual(len({x["canonicalIsomericSmiles"] for x in records}),len(records))
        self.assertTrue(all(x["validationStatus"]=="validated" and x["revisionUrl"].endswith(str(x["revision"])) and x["rank"]>0 for x in records))
        self.assertEqual({x["structureProperty"] for x in records},{"P233","P2017"})
        for name in ("records-v2.jsonl.gz","discovery-v2.sqlite3","wikidata-archive-v2.json.gz"):
            self.assertEqual(hashlib.sha256((DATA/name).read_bytes()).hexdigest(),manifest["files"][name],name)
        build=_build_module(); build.read_archive()  # raises on any archived response hash mismatch

    def test_common_name_coverage(self):
        # The checked-in list of names a curious user types first resolves to the intended item, ranked first.
        coverage=json.loads((DATA/"alias-coverage-v2.json").read_text())["names"]
        self.assertGreaterEqual(len(coverage),150)
        index=discovery.DiscoveryIndex(DATA/"discovery-v2.sqlite3")
        for item in coverage:
            found=index.search(item["name"])["candidates"]
            self.assertTrue(found and found[0]["sourceId"]==item["sourceId"],f"{item['name']} -> {[x['sourceId'] for x in found[:3]]}, expected {item['sourceId']}")

    def test_ranked_keyset_paging_ambiguity_and_escaping(self):
        index=discovery.DiscoveryIndex(DATA/"discovery-v2.sqlite3")
        first=index.search("a",prefix=True); self.assertEqual(len(first["candidates"]),discovery.MAX_RESULTS); self.assertTrue(first["truncated"])
        second=index.search("a",prefix=True,after=first["next"])
        ranks=[x["rank"] for x in first["candidates"]+second["candidates"]]
        self.assertEqual(ranks,sorted(ranks,reverse=True))
        self.assertFalse({x["recordId"] for x in first["candidates"]}&{x["recordId"] for x in second["candidates"]})
        self.assertTrue(all(len(x["names"])<=discovery.MAX_CANDIDATE_NAMES and x["nameCount"]>=len(x["names"]) for x in first["candidates"]))
        self.assertLess(len(json.dumps(first)),256*1024)
        self.assertEqual(index.search("%",prefix=True)["candidates"],[])
        ambiguous=index.search(AMBIGUOUS_NAME); self.assertGreaterEqual(len({x["canonicalIsomericSmiles"] for x in ambiguous["candidates"]}),2)
        record=discovery.read_records(DATA/"records-v2.jsonl.gz")[0]
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/"records.jsonl"; a=dict(record,recordId="test:a",displayName="shared",names=["shared"],canonicalIsomericSmiles="CCO",rank=3); b=dict(record,recordId="test:b",displayName="shared",names=["Shared"],canonicalIsomericSmiles="CCN",rank=9)
            source.write_text(json.dumps(a)+"\n"+json.dumps(b)+"\n"); path=Path(tmp)/"index.db"; discovery.build_index(source,path)
            result=discovery.DiscoveryIndex(path).search("shared",limit=1); self.assertTrue(result["truncated"]); self.assertEqual(result["candidates"][0]["recordId"],"test:b")
            rest=discovery.DiscoveryIndex(path).search("shared",limit=1,after=result["next"]); self.assertEqual([x["recordId"] for x in rest["candidates"]],["test:a"]); self.assertIsNone(rest["next"])

    def test_conflict_conversion_and_cache_lifecycle(self):
        source={"schemaVersion":2,"sourceId":"Q1","revision":2,"retrievedAt":"2026-09-15T00:00:00Z","structure":"CCO","referenceStructure":"CCN","structureFormat":"smiles","structureProperty":"P2017","rank":1,"names":[{"language":"en","value":"shared"}],"categories":[]}
        self.assertEqual(discovery.convert_record(source)["validationStatus"],"conflict")
        source.pop("referenceStructure"); source["inchiKey"]="LFQSCWFLJHTTHZ-UHFFFAOYSA-N"
        # Debian's python3-rdkit is built without InChI, the macOS arm64 wheel with it
        # (#42). Either way a claimed InChIKey may never be accepted silently: a build
        # that cannot derive one refuses the record, and a build that can performs the
        # cross-check and records the disagreement.
        try:
            from rdkit.Chem import inchi
            derives_keys = hasattr(inchi, "MolToInchiKey") and bool(inchi.MolToInchiKey(discovery.Chem.MolFromSmiles("CCO")))
        except Exception:
            derives_keys = False
        if derives_keys:
            # LFQSCWFLJHTTHZ-UHFFFAOYSA-N is ethanol's real key, so the cross-check
            # accepts it and rejects a wrong one.
            self.assertEqual(discovery.convert_record(source)["validationStatus"], "validated")
            wrong = dict(source, inchiKey="QTBSBXVTEAMEQO-UHFFFAOYSA-N")
            converted = discovery.convert_record(wrong)
            self.assertEqual(converted["validationStatus"], "conflict")
            self.assertTrue(any("InChIKey" in x for x in converted["diagnostics"]))
        else:
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
            subprocess.run([sys.executable,str(ROOT/"scripts/build-discovery.py"),"--output-dir",tmp,"--sample","400"],check=True)
            self.assertEqual((Path(tmp)/"records-v2.jsonl.gz").read_bytes(),(DATA/"records-v2.jsonl.gz").read_bytes())
            rebuilt=discovery.DiscoveryIndex(Path(tmp)/"discovery-v2.sqlite3")
            self.assertEqual(rebuilt.search(AMBIGUOUS_NAME),discovery.DiscoveryIndex(DATA/"discovery-v2.sqlite3").search(AMBIGUOUS_NAME))

    def test_generated_state_file_cache_is_disposable_and_versioned(self):
        state=json.dumps({"stateVersion":1,"basePatch":{},"editedPatch":{},"controls":{}},sort_keys=True)
        with tempfile.TemporaryDirectory() as tmp:
            cache=discovery.GeneratedFileCache(tmp,"snapshot-a")
            first=cache.put("CCO",{"analysis":1,"projection":1,"mapper":1},{},state)
            self.assertTrue(Path(first["path"]).is_file()); self.assertEqual(cache.get(first["key"])["identity"],"CCO")
            peer=discovery.GeneratedFileCache(tmp,"snapshot-a")
            second=peer.put("CCN",{"analysis":1,"projection":1,"mapper":1},{},state)
            self.assertEqual(cache.get(second["key"])["identity"],"CCN")
            Path(second["path"]).unlink(); self.assertIsNone(cache.get(second["key"])); self.assertFalse(cache.inspect()[0]["available"])
            user=Path(tmp).parent/"user-preset.iupacpatch"; user.write_text(state)
            self.assertTrue(cache.clear()); self.assertFalse(Path(first["path"]).exists()); self.assertTrue(user.exists()); user.unlink(); peer.db.close()
            cache.db.close(); changed=discovery.GeneratedFileCache(tmp,"snapshot-b")
            self.assertIsNone(changed.get(first["key"])); changed.db.close()
            (Path(tmp)/"generated-cache.sqlite3").write_bytes(b"corrupt")
            with self.assertRaises(sqlite3.DatabaseError):
                discovery.GeneratedFileCache(tmp,"snapshot-b")

if __name__=="__main__": unittest.main()
