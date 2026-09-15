#!/usr/bin/env python3
"""Production offline discovery index and disposable metadata cache."""

import hashlib
import json
import os
import sqlite3
import time
import unicodedata
from pathlib import Path

import helper
from rdkit import Chem

SCHEMA_VERSION = 1
INDEX_SCHEMA_VERSION = 1
CACHE_SCHEMA_VERSION = 1
MAX_RESULTS = 32
MAX_QUERY_BYTES = 256
MAX_ALIASES = 32
MAX_CACHE_ENTRIES = 1000
BUSY_TIMEOUT_MS = 250


class DiscoveryError(Exception):
    pass


def normalize_name(value):
    if not isinstance(value, str) or not value.strip():
        raise DiscoveryError("query must be non-empty text")
    if len(value.encode("utf-8")) > MAX_QUERY_BYTES:
        raise DiscoveryError("query exceeds 256 UTF-8 bytes")
    value = unicodedata.normalize("NFKC", value)
    return " ".join(value.replace("\u2010", "-").replace("\u2011", "-").split()).casefold()


def _bounded_text(value, field, maximum=1024):
    if not isinstance(value, str) or not value.strip() or len(value.encode("utf-8")) > maximum:
        raise DiscoveryError(f"{field} is missing or oversized")
    return value.strip()


def convert_record(source, opsin_jar=""):
    """Convert one archived provider record through the sole RDKit analysis path."""
    if not isinstance(source, dict) or source.get("schemaVersion") != SCHEMA_VERSION:
        raise DiscoveryError("unsupported DiscoveryRecord schema")
    qid = _bounded_text(source.get("sourceId"), "sourceId", 64)
    revision = source.get("revision")
    if not isinstance(revision, int) or revision <= 0:
        raise DiscoveryError("revision must be a positive integer")
    structure = _bounded_text(source.get("structure"), "structure", 8192)
    if source.get("structureFormat") != "smiles":
        raise DiscoveryError("only recorded SMILES structures are supported")
    analysis = helper.analyze("smiles", structure, opsin_jar)
    canonical = analysis["canonicalIsomericSmiles"]
    aliases = []
    for item in source.get("names", []):
        if not isinstance(item, dict) or item.get("language") != "en":
            continue
        name = _bounded_text(item.get("value"), "name", 256)
        if name not in aliases:
            aliases.append(name)
    if not aliases or len(aliases) > MAX_ALIASES:
        raise DiscoveryError("record must contain 1 to 32 distinct English names")
    claimed_key = source.get("inchiKey")
    diagnostics = []
    status = "validated"
    if claimed_key:
        claimed_key = _bounded_text(claimed_key, "inchiKey", 64).upper()
        molecule = Chem.MolFromSmiles(structure)
        try:
            from rdkit.Chem import inchi
            if not hasattr(inchi, "MolToInchiKey"):
                raise DiscoveryError("RDKit build has no InChIKey support")
            derived_key = inchi.MolToInchiKey(molecule)
        except DiscoveryError:
            raise
        except Exception as exc:
            raise DiscoveryError(f"unable to derive InChIKey: {exc}") from exc
        if not derived_key or derived_key.upper() != claimed_key:
            status = "conflict"
            diagnostics.append("recorded InChIKey disagrees with the source structure")
    reference = source.get("referenceStructure")
    if reference and helper.analyze("smiles", reference, opsin_jar)["canonicalIsomericSmiles"] != canonical:
        status = "conflict"
        diagnostics.append("recorded reference structure disagrees with the source structure")
    return {
        "schemaVersion": SCHEMA_VERSION, "recordId": f"wikidata:{qid}@{revision}",
        "provider": "wikidata", "sourceId": qid, "revision": revision,
        "displayName": aliases[0], "names": aliases, "categories": sorted(set(source.get("categories", []))),
        "sourceStructure": structure, "structureFormat": "smiles", "canonicalIsomericSmiles": canonical,
        "inchiKey": claimed_key, "sourceUrl": f"https://www.wikidata.org/wiki/{qid}",
        "revisionUrl": f"https://www.wikidata.org/w/index.php?title={qid}&oldid={revision}",
        "retrievedAt": _bounded_text(source.get("retrievedAt"), "retrievedAt", 64), "license": "CC0-1.0",
        "validationStatus": status, "diagnostics": diagnostics,
        "backend": analysis["backend"], "analysisVersion": analysis["analysisVersion"],
    }


def build_index(records_path, database_path):
    records = [json.loads(line) for line in Path(records_path).read_text(encoding="utf-8").splitlines() if line]
    temporary = Path(str(database_path) + ".new")
    temporary.unlink(missing_ok=True)
    db = sqlite3.connect(temporary)
    db.executescript("""
      PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;
      CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);
      CREATE TABLE records(record_id TEXT PRIMARY KEY, source_id TEXT NOT NULL, revision INTEGER NOT NULL,
        display_name TEXT NOT NULL, canonical_smiles TEXT NOT NULL, status TEXT NOT NULL, json TEXT NOT NULL);
      CREATE TABLE names(normalized TEXT NOT NULL, display TEXT NOT NULL, record_id TEXT NOT NULL REFERENCES records(record_id));
      CREATE INDEX names_lookup ON names(normalized, display, record_id);
    """)
    digest = hashlib.sha256(Path(records_path).read_bytes()).hexdigest()
    db.executemany("INSERT INTO metadata VALUES(?,?)", (("schemaVersion", str(INDEX_SCHEMA_VERSION)), ("recordsSha256", digest),
        ("sqliteVersion", sqlite3.sqlite_version), ("sqliteCompileOptions", json.dumps([x[0] for x in db.execute("pragma compile_options")]))))
    for record in sorted(records, key=lambda x: x["recordId"]):
        encoded = json.dumps(record, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        db.execute("INSERT INTO records VALUES(?,?,?,?,?,?,?)", (record["recordId"], record["sourceId"], record["revision"],
            record["displayName"], record["canonicalIsomericSmiles"], record["validationStatus"], encoded))
        db.executemany("INSERT INTO names VALUES(?,?,?)", ((normalize_name(name), name, record["recordId"]) for name in record["names"]))
    db.commit(); db.execute("VACUUM"); db.close()
    os.replace(temporary, database_path)


class DiscoveryIndex:
    def __init__(self, path):
        uri = Path(path).resolve().as_uri() + "?mode=ro&immutable=1"
        self.db = sqlite3.connect(uri, uri=True)
        version = self.db.execute("SELECT value FROM metadata WHERE key='schemaVersion'").fetchone()
        if version != (str(INDEX_SCHEMA_VERSION),):
            self.db.close(); raise DiscoveryError("unsupported discovery index schema")

    def search(self, query, prefix=False, limit=MAX_RESULTS):
        normalized = normalize_name(query)
        limit = max(1, min(int(limit), MAX_RESULTS))
        if prefix:
            escaped = normalized.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_") + "%"
            predicate, value = "n.normalized LIKE ? ESCAPE '\\'", escaped
        else:
            predicate, value = "n.normalized = ?", normalized
        rows = self.db.execute(f"SELECT DISTINCT r.json,n.display FROM names n JOIN records r USING(record_id) WHERE {predicate} "
            "ORDER BY n.normalized,r.display_name,r.canonical_smiles,r.record_id LIMIT ?", (value, limit + 1)).fetchall()
        return {"query": query, "match": "prefix" if prefix else "exact", "candidates": [json.loads(x[0]) for x in rows[:limit]],
                "truncated": len(rows) > limit, "limit": limit}


class DiscoveryCache:
    def __init__(self, path, revision, backend_version):
        self.path, self.revision, self.backend_version = str(path), revision, backend_version
        try:
            self.db = sqlite3.connect(self.path, timeout=BUSY_TIMEOUT_MS / 1000)
            self.db.execute(f"PRAGMA busy_timeout={BUSY_TIMEOUT_MS}")
            self.db.executescript("CREATE TABLE IF NOT EXISTS cache_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
                "CREATE TABLE IF NOT EXISTS queries(key TEXT PRIMARY KEY,value TEXT NOT NULL,accessed INTEGER NOT NULL);")
            meta = dict(self.db.execute("SELECT key,value FROM cache_meta"))
            expected = {"schema": str(CACHE_SCHEMA_VERSION), "revision": revision, "backend": backend_version}
            if meta and meta != expected:
                self.db.execute("DELETE FROM queries"); self.db.execute("DELETE FROM cache_meta")
            self.db.executemany("INSERT OR REPLACE INTO cache_meta VALUES(?,?)", expected.items()); self.db.commit()
        except sqlite3.Error as exc:
            raise DiscoveryError(f"cache unavailable: {exc}") from exc

    def get(self, key):
        try:
            row = self.db.execute("SELECT value FROM queries WHERE key=?", (key,)).fetchone()
            if row: self.db.execute("UPDATE queries SET accessed=? WHERE key=?", (time.time_ns(), key)); self.db.commit()
            return json.loads(row[0]) if row else None
        except (sqlite3.Error, json.JSONDecodeError): return None

    def put(self, key, value):
        try:
            encoded = json.dumps(value, sort_keys=True, separators=(",", ":"))
            with self.db:
                self.db.execute("INSERT OR REPLACE INTO queries VALUES(?,?,?)", (key, encoded, time.time_ns()))
                self.db.execute("DELETE FROM queries WHERE key IN (SELECT key FROM queries ORDER BY accessed DESC LIMIT -1 OFFSET ?)", (MAX_CACHE_ENTRIES,))
            return True
        except sqlite3.Error: return False

    def clear(self):
        try:
            with self.db: self.db.execute("DELETE FROM queries")
            return True
        except sqlite3.Error: return False
