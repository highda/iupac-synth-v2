#!/usr/bin/env python3
"""Production offline discovery index and disposable metadata cache."""

import gzip
import hashlib
import json
import os
import sqlite3
import time
from pathlib import Path

import helper
from resolution import ResolutionError, normalize_name as _normalize_name
from rdkit import Chem

SCHEMA_VERSION = 2
INDEX_SCHEMA_VERSION = 2
CACHE_SCHEMA_VERSION = 1
# Tier A (#138): keyset pages of 50 ranked candidates into the browser list.
MAX_RESULTS = 50
MAX_RECORDS = 40000
# A page must fit the helper's 256 KiB response, so candidates carry at most this many names; the
# full record (all aliases) is one `record` request away.
MAX_CANDIDATE_NAMES = 8
STRUCTURE_PROPERTIES = ("P2017", "P233")
MAX_QUERY_BYTES = 256
# Popular items carry long brand-name alias lists; 32 cut off plain names such as `estradiol` (#138).
MAX_ALIASES = 128
MAX_CACHE_ENTRIES = 1000
BUSY_TIMEOUT_MS = 250


class DiscoveryError(Exception):
    pass


def normalize_name(value):
    try:
        return _normalize_name(value)
    except ResolutionError as exc:
        raise DiscoveryError(str(exc)) from exc


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
    structure_property = source.get("structureProperty")
    if structure_property not in STRUCTURE_PROPERTIES:
        raise DiscoveryError("structureProperty must be P2017 (isomeric) or P233 (canonical SMILES)")
    rank = source.get("rank")
    if not isinstance(rank, int) or rank < 0:
        raise DiscoveryError("rank must be a non-negative sitelink count")
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
        raise DiscoveryError(f"record must contain 1 to {MAX_ALIASES} distinct English names")
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
        # An isomeric SMILES must reproduce the whole key. A canonical SMILES carries no stereo, so only
        # the connectivity block is comparable; checking more would demand stereo the source never stated.
        compared = derived_key.upper() if derived_key else ""
        if structure_property == "P233":
            compared, claimed_compared = compared[:14], claimed_key[:14]
        else:
            claimed_compared = claimed_key
        if not compared or compared != claimed_compared:
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
        "sourceStructure": structure, "structureFormat": "smiles", "structureProperty": structure_property,
        "rank": rank, "canonicalIsomericSmiles": canonical,
        "inchiKey": claimed_key, "sourceUrl": f"https://www.wikidata.org/wiki/{qid}",
        "revisionUrl": f"https://www.wikidata.org/w/index.php?title={qid}&oldid={revision}",
        "retrievedAt": _bounded_text(source.get("retrievedAt"), "retrievedAt", 64), "license": "CC0-1.0",
        "validationStatus": status, "diagnostics": diagnostics,
        "backend": analysis["backend"], "analysisVersion": analysis["analysisVersion"],
    }


# Fields every record of a snapshot shares are stored once in the index metadata, and the two Wikidata
# URLs are derived from the QID and revision, so the index holds only what varies per record (#138).
SHARED_FIELDS = ("analysisVersion", "backend", "license", "provider", "retrievedAt", "schemaVersion", "structureFormat")


def read_records(path):
    data = Path(path).read_bytes()
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    return [json.loads(line) for line in data.decode("utf-8").splitlines() if line]


def _compact_record(record, shared):
    row = {key: value for key, value in record.items() if key not in shared and key not in ("sourceUrl", "revisionUrl")}
    if not row.get("diagnostics"):
        row.pop("diagnostics", None)
    return row


def _expand_record(row, shared):
    record = dict(shared, diagnostics=[], **row)
    record["sourceUrl"] = f"https://www.wikidata.org/wiki/{record['sourceId']}"
    record["revisionUrl"] = f"https://www.wikidata.org/w/index.php?title={record['sourceId']}&oldid={record['revision']}"
    return record


def build_index(records_path, database_path):
    records = read_records(records_path)
    temporary = Path(str(database_path) + ".new")
    temporary.unlink(missing_ok=True)
    db = sqlite3.connect(temporary)
    db.executescript("""
      PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;
      CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);
      CREATE TABLE records(record_id TEXT PRIMARY KEY, source_id TEXT NOT NULL, revision INTEGER NOT NULL,
        display_name TEXT NOT NULL, canonical_smiles TEXT NOT NULL, status TEXT NOT NULL, rank INTEGER NOT NULL,
        json TEXT NOT NULL);
      CREATE TABLE names(normalized TEXT NOT NULL, display TEXT NOT NULL, record_id TEXT NOT NULL REFERENCES records(record_id));
      CREATE INDEX names_lookup ON names(normalized, record_id);
      CREATE INDEX records_rank ON records(rank DESC, record_id);
    """)
    digest = hashlib.sha256(Path(records_path).read_bytes()).hexdigest()
    shared = {key: records[0][key] for key in SHARED_FIELDS if records and all(x.get(key) == records[0][key] for x in records)}
    db.executemany("INSERT INTO metadata VALUES(?,?)", (("schemaVersion", str(INDEX_SCHEMA_VERSION)), ("recordsSha256", digest),
        ("sharedFields", json.dumps(shared, sort_keys=True)),
        ("sqliteVersion", sqlite3.sqlite_version), ("sqliteCompileOptions", json.dumps([x[0] for x in db.execute("pragma compile_options")]))))
    if len(records) > MAX_RECORDS:
        raise DiscoveryError(f"snapshot exceeds {MAX_RECORDS} records")
    for record in sorted(records, key=lambda x: x["recordId"]):
        encoded = json.dumps(_compact_record(record, shared), sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        db.execute("INSERT INTO records VALUES(?,?,?,?,?,?,?,?)", (record["recordId"], record["sourceId"], record["revision"],
            record["displayName"], record["canonicalIsomericSmiles"], record["validationStatus"], record["rank"], encoded))
        normalized = dict.fromkeys(normalize_name(name) for name in record["names"])
        db.executemany("INSERT INTO names VALUES(?,?,?)", ((key, "", record["recordId"]) for key in normalized))
    db.commit(); db.execute("VACUUM"); db.close()
    os.replace(temporary, database_path)


def _candidate(record):
    """A page row: the record with its name list bounded, so 50 rows fit one helper response."""
    names = record["names"]
    return dict(record, names=names[:MAX_CANDIDATE_NAMES], nameCount=len(names))


class DiscoveryIndex:
    def __init__(self, path):
        uri = Path(path).resolve().as_uri() + "?mode=ro&immutable=1"
        self.db = sqlite3.connect(uri, uri=True)
        version = self.db.execute("SELECT value FROM metadata WHERE key='schemaVersion'").fetchone()
        if version != (str(INDEX_SCHEMA_VERSION),):
            self.db.close(); raise DiscoveryError("unsupported discovery index schema")
        self.shared = json.loads(self.db.execute("SELECT value FROM metadata WHERE key='sharedFields'").fetchone()[0])

    def search(self, query, prefix=False, limit=MAX_RESULTS, after=None):
        """One ranked page: most-sitelinked first, then record ID, continued by keyset, never OFFSET."""
        normalized = normalize_name(query)
        limit = max(1, min(int(limit), MAX_RESULTS))
        if prefix:
            # A half-open range over the normalized names uses the index, unlike LIKE.
            predicate, values = "normalized >= ? AND normalized < ?", [normalized, normalized + "\U0010ffff"]
        else:
            predicate, values = "normalized = ?", [normalized]
        keyset = ""
        if after is not None:
            if not isinstance(after, dict) or not isinstance(after.get("rank"), int) or not isinstance(after.get("recordId"), str):
                raise DiscoveryError("page cursor must carry rank and recordId")
            keyset = "AND (rank < ? OR (rank = ? AND record_id > ?))"
            values += [after["rank"], after["rank"], _bounded_text(after["recordId"], "recordId", 128)]
        rows = self.db.execute(f"SELECT json FROM records WHERE record_id IN (SELECT record_id FROM names WHERE {predicate}) "
            f"{keyset} ORDER BY rank DESC, record_id LIMIT ?", (*values, limit + 1)).fetchall()
        candidates = [_candidate(_expand_record(json.loads(x[0]), self.shared)) for x in rows[:limit]]
        following = {"rank": candidates[-1]["rank"], "recordId": candidates[-1]["recordId"]} if len(rows) > limit else None
        return {"query": query, "match": "prefix" if prefix else "exact", "candidates": candidates,
                "truncated": following is not None, "limit": limit, "next": following}

    def record(self, record_id):
        record_id = _bounded_text(record_id, "recordId", 128)
        row = self.db.execute("SELECT json FROM records WHERE record_id=?", (record_id,)).fetchone()
        if not row:
            raise DiscoveryError("discovery record was not found")
        return _expand_record(json.loads(row[0]), self.shared)


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


class GeneratedFileCache:
    """Disposable metadata index whose values are ordinary State files.

    Files are written with atomic replacement.  The database never owns user
    preset directories and clear/eviction only unlink paths below this cache.
    """
    def __init__(self, root, revision):
        self.root = Path(root)
        self.files = self.root / "generated"
        self.files.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(self.root / "generated-cache.sqlite3", timeout=BUSY_TIMEOUT_MS / 1000)
        self.db.execute(f"PRAGMA busy_timeout={BUSY_TIMEOUT_MS}")
        self.db.executescript("CREATE TABLE IF NOT EXISTS generated("
            "key TEXT PRIMARY KEY,path TEXT NOT NULL,identity TEXT NOT NULL,versions TEXT NOT NULL,"
            "settings TEXT NOT NULL,accessed INTEGER NOT NULL);"
            "CREATE TABLE IF NOT EXISTS generated_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);")
        meta = dict(self.db.execute("SELECT key,value FROM generated_meta"))
        if meta and meta != {"schema": "1", "revision": revision}:
            self.clear()
            self.db.execute("DELETE FROM generated_meta")
        self.db.executemany("INSERT OR REPLACE INTO generated_meta VALUES(?,?)",
                            {"schema": "1", "revision": revision}.items())
        self.db.commit()

    @staticmethod
    def key(identity, versions, settings):
        value = {"identity": identity, "versions": versions, "settings": settings}
        return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()

    def put(self, identity, versions, settings, state_text):
        key = self.key(identity, versions, settings)
        # Validate enough of the shared wire shape to reject arbitrary files;
        # the C++ State codec remains authoritative when reopening/rendering.
        state = json.loads(state_text)
        if not isinstance(state, dict) or state.get("stateVersion") != 1 or "basePatch" not in state or "editedPatch" not in state:
            raise DiscoveryError("generated cache value is not State v1")
        target = self.files / (key + ".iupacpatch")
        temporary = target.with_name(target.name + f".tmp-{os.getpid()}-{time.time_ns()}")
        with open(temporary, "x", encoding="utf-8") as stream:
            stream.write(state_text)
            stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, target)
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO generated VALUES(?,?,?,?,?,?)", (key, str(target), identity,
                json.dumps(versions, sort_keys=True), json.dumps(settings, sort_keys=True), time.time_ns()))
            stale = self.db.execute("SELECT key,path FROM generated ORDER BY accessed DESC LIMIT -1 OFFSET ?", (MAX_CACHE_ENTRIES,)).fetchall()
            for stale_key, stale_path in stale:
                Path(stale_path).unlink(missing_ok=True)
                self.db.execute("DELETE FROM generated WHERE key=?", (stale_key,))
        return {"key": key, "path": str(target)}

    def get(self, key):
        row = self.db.execute("SELECT path,identity,versions,settings FROM generated WHERE key=?", (key,)).fetchone()
        if not row or not Path(row[0]).is_file():
            return None
        with self.db:
            self.db.execute("UPDATE generated SET accessed=? WHERE key=?", (time.time_ns(), key))
        return {"key": key, "path": row[0], "identity": row[1], "versions": json.loads(row[2]), "settings": json.loads(row[3])}

    def inspect(self):
        rows = self.db.execute("SELECT key,path,identity,versions,settings FROM generated ORDER BY accessed DESC LIMIT ?", (MAX_RESULTS,)).fetchall()
        return [{"key": x[0], "path": x[1], "identity": x[2], "versions": json.loads(x[3]), "settings": json.loads(x[4]),
                 "available": Path(x[1]).is_file()} for x in rows]

    def clear(self):
        try:
            rows = self.db.execute("SELECT path FROM generated").fetchall()
            with self.db: self.db.execute("DELETE FROM generated")
            for (path,) in rows:
                candidate = Path(path)
                if candidate.parent == self.files:
                    candidate.unlink(missing_ok=True)
            return True
        except sqlite3.Error:
            return False
