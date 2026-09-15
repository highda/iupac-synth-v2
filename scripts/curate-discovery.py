#!/usr/bin/env python3
"""Explicit maintainer refresh of the bounded Wikidata snapshot (network required)."""
import argparse, datetime, hashlib, json, sys, urllib.parse, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "chemistry"))
import discovery

SPARQL = '''SELECT ?item ?smiles WHERE {
 ?item wdt:P233 ?smiles.
} LIMIT 1200'''
USER_AGENT = "iupac-synth-v2-curation/1.0 (https://github.com/highda/iupac-synth-v2)"

def get_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response: return json.load(response)

def categories(name, smiles):
    text = name.casefold(); result = set()
    if any(x in text for x in ("acid", "alanine", "glycine", "peptide")): result.add("amino-acids-short-peptides")
    if any(x in text for x in ("ose", "sugar", "glucose", "fructose", "ribose")): result.add("carbohydrates")
    if any(x in text for x in ("steroid", "cholest", "estr", "testost", "lipid", "fatty")): result.add("lipids-steroids")
    if any(x in text for x in ("water", "ethanol", "methanol", "acetone", "benzene", "solvent", "glycol")): result.add("solvents-material-ingredients")
    if "." in smiles or "[Na" in smiles or "[Cl-" in smiles: result.add("salts-inorganic")
    if "c" in smiles: result.add("aromatic-heterocyclic")
    if len(smiles) < 20: result.add("simple-organics")
    if len(smiles) > 35: result.add("common-natural-products")
    return sorted(result or {"simple-organics"})

def main():
    parser=argparse.ArgumentParser(); parser.add_argument("--output-root", type=Path, default=ROOT); args=parser.parse_args()
    query_url="https://query.wikidata.org/sparql?"+urllib.parse.urlencode({"query":SPARQL,"format":"json"})
    query=get_json(query_url); candidates={row["item"]["value"].rsplit("/",1)[-1]:row for row in query["results"]["bindings"]}
    qids=list(candidates); accepted=[]; rejected=[]; archives=[]; seen=set(); retrieved=datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat()
    for offset in range(0,len(qids),50):
        url="https://www.wikidata.org/w/api.php?"+urllib.parse.urlencode({"action":"wbgetentities","ids":"|".join(qids[offset:offset+50]),"props":"info|labels|aliases","languages":"en","format":"json","formatversion":2})
        payload=get_json(url); archives.append({"url":url,"sha256":hashlib.sha256(json.dumps(payload,sort_keys=True,separators=(",",":")).encode()).hexdigest(),"response":payload})
        entities = payload["entities"].values() if isinstance(payload["entities"], dict) else payload["entities"]
        for entity in entities:
            qid=entity["id"]; row=candidates[qid]; label=entity.get("labels",{}).get("en",{}).get("value")
            if not label: continue
            names=[label]+[x["value"] for x in entity.get("aliases",{}).get("en",[])]; names=list(dict.fromkeys(names))[:discovery.MAX_ALIASES]
            source={"schemaVersion":1,"sourceId":qid,"revision":entity["lastrevid"],"retrievedAt":retrieved,"structure":row["smiles"]["value"],"structureFormat":"smiles",
                    "inchiKey":None,"names":[{"language":"en","value":x} for x in names],"categories":categories(label,row["smiles"]["value"])}
            try:
                record=discovery.convert_record(source)
                if record["validationStatus"] != "validated": raise discovery.DiscoveryError("identifier conflict")
                if record["canonicalIsomericSmiles"] in seen: raise discovery.DiscoveryError("duplicate canonical structure")
                seen.add(record["canonicalIsomericSmiles"]); accepted.append(record)
            except Exception as exc: rejected.append({"sourceId":qid,"reason":str(exc)})
    if len(accepted)<256: raise SystemExit(f"only {len(accepted)} records validated")
    accepted=sorted(accepted,key=lambda x:(len(x["names"])<2,x["recordId"]))[:256]
    data=args.output_root/"data/discovery"; data.mkdir(parents=True,exist_ok=True)
    records=data/"records-v1.jsonl"; records.write_text("".join(json.dumps(x,sort_keys=True,ensure_ascii=False,separators=(",",":"))+"\n" for x in sorted(accepted,key=lambda x:x["recordId"])),encoding="utf-8")
    archive=data/"wikidata-archive-v1.json"; archive.write_text(json.dumps({"license":"CC0-1.0","retrievedAt":retrieved,"query":SPARQL,"queryResponseSha256":hashlib.sha256(json.dumps(query,sort_keys=True,separators=(",",":")).encode()).hexdigest(),"queryResponse":query,"batches":archives},sort_keys=True,separators=(",",":")),encoding="utf-8")
    discovery.build_index(records,data/"discovery-v1.sqlite3")
    panel_canonicals=set()
    for panel in (ROOT/"data/panels").glob("*.json"):
        def visit(value):
            if isinstance(value,dict):
                if isinstance(value.get("canonicalIsomericSmiles"),str): panel_canonicals.add(value["canonicalIsomericSmiles"])
                for child in value.values(): visit(child)
            elif isinstance(value,list):
                for child in value: visit(child)
        visit(json.loads(panel.read_text()))
    manifest={"schemaVersion":1,"recordCount":len(accepted),"twoNameCount":sum(len(x["names"])>=2 for x in accepted),"outsideMappingPanels":sum(x["canonicalIsomericSmiles"] not in panel_canonicals for x in accepted),"categories":sorted({c for x in accepted for c in x["categories"]}),"rejected":rejected,"files":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (records,archive,data/"discovery-v1.sqlite3")}}
    (data/"manifest-v1.json").write_text(json.dumps(manifest,sort_keys=True,indent=2)+"\n")
if __name__=="__main__": main()
