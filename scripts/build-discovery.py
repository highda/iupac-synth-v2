#!/usr/bin/env python3
"""Convert the archived Tier A Wikidata snapshot into validated records, the index and the manifest.

Offline: reads only `wikidata-archive-v2.json.gz` (and the reviewed aliases). `--write-data` replaces the
shipped records/index/manifest; `--output-dir DIR --sample N` re-converts every Nth archived item and
checks it against the retained records, then rebuilds the index from them (the ctest path).
"""
import argparse, gzip, hashlib, json, multiprocessing, sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data/discovery"
sys.path.insert(0, str(ROOT / "chemistry"))
import discovery

ARCHIVE = DATA / "wikidata-archive-v2.json.gz"
RECORDS = DATA / "records-v2.jsonl.gz"
INDEX = DATA / "discovery-v2.sqlite3"
MANIFEST = DATA / "manifest-v2.json"
REVIEWED = DATA / "reviewed-aliases-v2.json"


def compact(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


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


def read_archive(path=ARCHIVE):
    with gzip.open(path, "rb") as stream:
        archive = json.loads(stream.read())
    for query in [*archive["items"], *archive["values"]]:
        if hashlib.sha256(query["response"].encode()).hexdigest() != query["sha256"]:
            raise SystemExit("archived SPARQL response hash mismatch")
    for batch in archive["batches"]:
        if hashlib.sha256(compact(batch["response"])).hexdigest() != batch["sha256"]:
            raise SystemExit("archived entity response hash mismatch")
    return archive


def qid(cell):
    return cell.strip("<>").rsplit("/", 1)[-1]


def literal(cell):
    # TSV literals are N-Triples-quoted: "value" or "value"@lang / ^^type.
    if cell.startswith('"'):
        end = cell.rindex('"')
        return cell[1:end].encode("latin-1", "backslashreplace").decode("unicode_escape")
    return cell


def sources(archive):
    """Every archived item as a DiscoveryRecord source, or a rejection naming why it has none."""
    reviewed = json.loads(REVIEWED.read_text(encoding="utf-8")) if REVIEWED.is_file() else {"aliases": []}
    extra = {}
    for item in reviewed["aliases"]:
        extra.setdefault(item["sourceId"], []).append(item["name"])
    rank = {qid(item): int(count) for listing in archive["items"]
            for item, count in (line.split("\t") for line in listing["response"].splitlines()[1:] if line)}
    values = {}
    for query in archive["values"]:
        for line in query["response"].splitlines()[1:]:
            if line:
                item, value = line.split("\t")
                values.setdefault((qid(item), query["property"]), set()).add(literal(value))
    result, rejected = [], []
    for batch in archive["batches"]:
        entities = batch["response"]["entities"]
        for entity in (entities.values() if isinstance(entities, dict) else entities):
            source_id = entity["id"]
            label = entity.get("labels", {}).get("en", {}).get("value")
            if not label:
                rejected.append({"sourceId": source_id, "reason": "no English label"}); continue
            isomeric, canonical = values.get((source_id, "P2017"), set()), values.get((source_id, "P233"), set())
            # Two recorded values of the chosen property are accepted only when they are notations of one
            # structure: the second is the reference the backend must canonicalize identically.
            chosen, prop = (isomeric, "P2017") if isomeric else (canonical, "P233")
            if not chosen:
                rejected.append({"sourceId": source_id, "reason": "no SMILES"}); continue
            if len(chosen) > 2:
                rejected.append({"sourceId": source_id, "reason": f"more than two {prop} values"}); continue
            structure, *reference = sorted(chosen)
            keys = values.get((source_id, "P235"), set())
            names = list(dict.fromkeys([label] + [x["value"] for x in entity.get("aliases", {}).get("en", [])] + extra.get(source_id, [])))
            result.append({"schemaVersion": discovery.SCHEMA_VERSION, "sourceId": source_id, "revision": entity["lastrevid"],
                           "retrievedAt": archive["retrievedAt"], "structure": structure, "structureFormat": "smiles",
                           "structureProperty": prop, "inchiKey": next(iter(keys)) if len(keys) == 1 else None,
                           **({"referenceStructure": reference[0]} if reference else {}),
                           "rank": rank.get(source_id, 0), "names": [{"language": "en", "value": x} for x in names[:discovery.MAX_ALIASES]],
                           "categories": categories(label, structure)})
    return result, rejected


def convert(source):
    try:
        record = discovery.convert_record(source)
        if record["validationStatus"] != "validated":
            return None, {"sourceId": source["sourceId"], "reason": "conflict: " + "; ".join(record["diagnostics"])}
        return record, None
    except Exception as exc:  # the backend's own refusal is the rejection reason
        return None, {"sourceId": source["sourceId"], "reason": str(exc)[:200]}


def convert_all(items):
    with multiprocessing.Pool() as pool:
        return pool.map(convert, items, chunksize=64)


def write_data(archive):
    items, rejected = sources(archive)
    accepted = []
    for record, rejection in convert_all(items):
        (accepted.append(record) if record else rejected.append(rejection))
    # One record per canonical structure: the most-linked item keeps it, the others are recorded.
    accepted.sort(key=lambda x: (-x["rank"], int(x["sourceId"][1:])))
    kept, seen = [], {}
    for record in accepted:
        owner = seen.get(record["canonicalIsomericSmiles"])
        if owner:
            rejected.append({"sourceId": record["sourceId"], "reason": f"duplicate canonical structure of {owner}"}); continue
        seen[record["canonicalIsomericSmiles"]] = record["sourceId"]; kept.append(record)
    kept.sort(key=lambda x: x["recordId"])
    text = "".join(json.dumps(x, sort_keys=True, ensure_ascii=False, separators=(",", ":")) + "\n" for x in kept)
    with gzip.GzipFile(RECORDS, "wb", mtime=0) as stream:  # mtime=0: identical records, identical bytes
        stream.write(text.encode("utf-8"))
    discovery.build_index(RECORDS, INDEX)
    panel_canonicals = set()
    def visit(value):
        if isinstance(value, dict):
            if isinstance(value.get("canonicalIsomericSmiles"), str): panel_canonicals.add(value["canonicalIsomericSmiles"])
            for child in value.values(): visit(child)
        elif isinstance(value, list):
            for child in value: visit(child)
    for panel in (ROOT / "data/panels").glob("*.json"):
        visit(json.loads(panel.read_text()))
    rejected.sort(key=lambda x: int(x["sourceId"][1:]))
    manifest = {"schemaVersion": discovery.SCHEMA_VERSION, "tier": "A", "retrievedAt": archive["retrievedAt"],
                "archivedItems": len(items) + sum(1 for x in rejected if x["reason"] in ("no English label", "no SMILES") or x["reason"].startswith("more than two")),
                "recordCount": len(kept), "twoNameCount": sum(len(x["names"]) >= 2 for x in kept),
                "outsideMappingPanels": sum(x["canonicalIsomericSmiles"] not in panel_canonicals for x in kept),
                "structureProperties": dict(Counter(x["structureProperty"] for x in kept)),
                "categories": sorted({c for x in kept for c in x["categories"]}),
                "rejectedByReason": dict(sorted(Counter(x["reason"].split(":")[0] if x["reason"].startswith(("conflict", "duplicate")) else x["reason"] for x in rejected).most_common(40))),
                "rejected": rejected,
                "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (RECORDS, ARCHIVE, INDEX) if p.is_file()}}
    if REVIEWED.is_file():
        manifest["files"][REVIEWED.name] = hashlib.sha256(REVIEWED.read_bytes()).hexdigest()
    MANIFEST.write_text(json.dumps(manifest, sort_keys=True, indent=1) + "\n")
    print(json.dumps({k: manifest[k] for k in ("recordCount", "twoNameCount", "structureProperties", "rejectedByReason")}, indent=1))


def verify_sample(archive, output_dir, step):
    retained = {x["sourceId"]: x for x in discovery.read_records(RECORDS)}
    items, _ = sources(archive)
    sample = [x for x in sorted(items, key=lambda x: int(x["sourceId"][1:]))[::step] if x["sourceId"] in retained]
    if not sample:
        raise SystemExit("sample contains no retained record")
    checked = ("sourceId", "revision", "displayName", "names", "sourceStructure", "structureProperty", "rank",
               "canonicalIsomericSmiles", "validationStatus", "backend")
    for source in sample:
        record, rejection = convert(source)
        expected = retained[source["sourceId"]]
        if rejection or any(record[key] != expected[key] for key in checked):
            raise SystemExit(f"retained record differs from archived conversion: {source['sourceId']}")
    output_dir.mkdir(parents=True, exist_ok=True)
    target = output_dir / RECORDS.name
    target.write_bytes(RECORDS.read_bytes())
    discovery.build_index(target, output_dir / INDEX.name)
    print(f"reproduced {len(sample)} sampled records")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, default=ARCHIVE)
    parser.add_argument("--write-data", action="store_true")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--sample", type=int, default=200)
    args = parser.parse_args()
    archive = read_archive(args.archive)
    if args.write_data:
        write_data(archive)
    elif args.output_dir:
        verify_sample(archive, args.output_dir, args.sample)
    else:
        parser.error("pass --write-data or --output-dir")


if __name__ == "__main__":
    main()
