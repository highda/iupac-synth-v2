#!/usr/bin/env python3
"""Rebuild validated discovery records and the logical index from the archived snapshot."""
import argparse, hashlib, json, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"chemistry"))
import discovery

def main():
    parser=argparse.ArgumentParser(); parser.add_argument("--archive",type=Path,default=ROOT/"data/discovery/wikidata-archive-v1.json"); parser.add_argument("--records",type=Path,default=ROOT/"data/discovery/records-v1.jsonl"); parser.add_argument("--output-dir",type=Path,required=True); args=parser.parse_args()
    archive=json.loads(args.archive.read_text(encoding="utf-8"))
    compact=lambda value: json.dumps(value,sort_keys=True,separators=(",",":")).encode()
    if hashlib.sha256(compact(archive["queryResponse"])).hexdigest()!=archive["queryResponseSha256"]: raise SystemExit("archived query response hash mismatch")
    for batch in archive["batches"]:
        if hashlib.sha256(compact(batch["response"])).hexdigest()!=batch["sha256"]: raise SystemExit("archived entity response hash mismatch")
    rows={x["item"]["value"].rsplit("/",1)[-1]:x for x in archive["queryResponse"]["results"]["bindings"]}
    retained={x["recordId"]:x for x in (json.loads(line) for line in args.records.read_text(encoding="utf-8").splitlines())}
    reproduced={}
    for batch in archive["batches"]:
        entities=batch["response"]["entities"]; entities=entities.values() if isinstance(entities,dict) else entities
        for entity in entities:
            row=rows.get(entity["id"]); label=entity.get("labels",{}).get("en",{}).get("value")
            if not row or not label: continue
            names=list(dict.fromkeys([label]+[x["value"] for x in entity.get("aliases",{}).get("en",[])]))[:discovery.MAX_ALIASES]
            source={"schemaVersion":1,"sourceId":entity["id"],"revision":entity["lastrevid"],"retrievedAt":archive["retrievedAt"],"structure":row["smiles"]["value"],"structureFormat":"smiles","inchiKey":None,"names":[{"language":"en","value":x} for x in names],"categories":[]}
            try: record=discovery.convert_record(source)
            except Exception: continue
            if record["recordId"] in retained: reproduced[record["recordId"]]=record
    if len(retained)!=256 or set(reproduced)!=set(retained): raise SystemExit("retained records are not reproduced by the source archive")
    checked=("sourceId","revision","displayName","names","sourceStructure","canonicalIsomericSmiles","validationStatus","backend")
    for record_id, expected in retained.items():
        if any(reproduced[record_id][key]!=expected[key] for key in checked): raise SystemExit(f"retained record differs from archived conversion: {record_id}")
    records=list(retained.values())
    args.output_dir.mkdir(parents=True,exist_ok=True); target=args.output_dir/"records-v1.jsonl"
    target.write_bytes(args.records.read_bytes())
    discovery.build_index(target,args.output_dir/"discovery-v1.sqlite3")
if __name__=="__main__": main()
