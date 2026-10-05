#!/usr/bin/env python3
"""Explicit maintainer refresh of the Tier A Wikidata snapshot (network required).

Tier A (owner decision on #138): every structure-bearing Wikidata item with at least one Wikipedia
sitelink, ranked by sitelink count. This script only fetches and archives the exact SPARQL and
`wbgetentities` responses; `scripts/build-discovery.py --write-data` converts the archive into the
validated records, the index and the manifest, so the shipped data is always reproducible from the
archive alone.
"""
import argparse, datetime, gzip, hashlib, json, sys, time, urllib.error, urllib.parse, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
USER_AGENT = "iupac-synth-v2-curation/2.0 (https://github.com/highda/iupac-synth-v2)"
# The item list (sitelinked items with either SMILES property) is one query; projecting the values
# in that same query exceeds the service's 60 s limit, so values are fetched for explicit QID batches.
# One item-list query per SMILES property (a UNION of the two doubles the scan and times out).
ITEMS_QUERIES = {prop: f"SELECT ?item ?sitelinks WHERE {{ ?item wdt:{prop} [] ; wikibase:sitelinks ?sitelinks . FILTER(?sitelinks > 0) }}"
                 for prop in ("P233", "P2017")}
VALUE_PROPERTIES = {"canonicalSmiles": "P233", "isomericSmiles": "P2017", "inchiKey": "P235"}
VALUE_BATCH = 1000
BATCH = 50


def compact(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def fetch(url, accept="application/json", data=None):
    for attempt in range(8):
        request = urllib.request.Request(url, data=data, headers={"User-Agent": USER_AGENT, "Accept": accept})
        try:
            with urllib.request.urlopen(request, timeout=300) as response:
                return response.read().decode("utf-8")
        except (urllib.error.URLError, TimeoutError) as exc:
            wait = 10 * (attempt + 1)
            print(f"retry {attempt + 1} after {exc}; waiting {wait}s", file=sys.stderr)
            time.sleep(wait)
    raise SystemExit(f"giving up on {url[:120]}")


def sparql(query):
    """One exact TSV response; a Java stack trace in the body means the service timed out mid-stream."""
    for attempt in range(4):
        text = fetch("https://query.wikidata.org/sparql", "text/tab-separated-values",
                     urllib.parse.urlencode({"query": query}).encode())
        if "\tat " not in text and "Exception" not in text:
            return {"query": query, "sha256": hashlib.sha256(text.encode()).hexdigest(), "response": text}
        print(f"truncated SPARQL response, retry {attempt + 1}", file=sys.stderr)
        time.sleep(30)
    raise SystemExit("SPARQL keeps timing out")


def tsv_rows(text):
    lines = text.splitlines()
    return [line.split("\t") for line in lines[1:] if line]


def fetch_values(qids):
    values = []
    for name, prop in VALUE_PROPERTIES.items():
        for offset in range(0, len(qids), VALUE_BATCH):
            ids = " ".join("wd:" + q for q in qids[offset:offset + VALUE_BATCH])
            values.append(dict(sparql(f"SELECT ?item ?value WHERE {{ VALUES ?item {{ {ids} }} ?item wdt:{prop} ?value }}"), property=prop))
            time.sleep(1)
        print(f"{name}: done", file=sys.stderr)
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, default=ROOT / "data/discovery/wikidata-archive-v2.json.gz")
    args = parser.parse_args()
    retrieved = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat()
    # The SPARQL stage is checkpointed beside the archive so a restart resumes at the entity stage.
    checkpoint = args.archive.with_name(args.archive.name + ".sparql-checkpoint.json")
    if checkpoint.is_file():
        saved = json.loads(checkpoint.read_text())
        retrieved, items, values = saved["retrievedAt"], saved["items"], saved["values"]
    else:
        items = [dict(sparql(query), property=prop) for prop, query in ITEMS_QUERIES.items()]
        values = None
    sitelinks = {}
    for listing in items:
        for item, count in tsv_rows(listing["response"]):
            sitelinks[item.strip("<>").rsplit("/", 1)[-1]] = int(count)
    qids = sorted(sitelinks, key=lambda q: int(q[1:]))
    print(f"items: {len(qids)}", file=sys.stderr)
    if values is None:
        values = fetch_values(qids)
        checkpoint.write_text(json.dumps({"retrievedAt": retrieved, "items": items, "values": values}))

    batches = []
    for offset in range(0, len(qids), BATCH):
        url = "https://www.wikidata.org/w/api.php?" + urllib.parse.urlencode({
            "action": "wbgetentities", "ids": "|".join(qids[offset:offset + BATCH]), "props": "info|labels|aliases",
            "languages": "en", "format": "json", "formatversion": 2})
        for attempt in range(30):
            payload = json.loads(fetch(url))
            if payload.get("error", {}).get("code") != "maxlag":
                break
            # The API asks clients to back off while replicas lag; this is load shedding, not failure.
            time.sleep(min(60, 5 + float(payload["error"].get("lag", 5))))
        if "error" in payload:
            raise SystemExit(f"wbgetentities error: {payload['error']}")
        batches.append({"url": url, "sha256": hashlib.sha256(compact(payload)).hexdigest(), "response": payload})
        if len(batches) % 50 == 0:
            print(f"entities: {offset + BATCH}/{len(qids)}", file=sys.stderr)
        time.sleep(0.2)
    archive = {"license": "CC0-1.0", "tier": "A", "retrievedAt": retrieved, "items": items, "values": values, "batches": batches}
    args.archive.parent.mkdir(parents=True, exist_ok=True)
    # mtime=0 keeps the gzip container deterministic for identical content.
    with gzip.GzipFile(args.archive, "wb", mtime=0) as stream:
        stream.write(json.dumps(archive, sort_keys=True, separators=(",", ":")).encode())
    checkpoint.unlink(missing_ok=True)
    print(f"archived {len(qids)} items in {len(batches)} batches to {args.archive}", file=sys.stderr)


if __name__ == "__main__":
    main()
