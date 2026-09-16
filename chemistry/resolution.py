"""Shared offline name normalization and exact-record resolution policy."""

import json
import sqlite3
import unicodedata
from pathlib import Path

MAX_QUERY_BYTES = 256


class ResolutionError(Exception):
    pass


def normalize_name(value):
    if not isinstance(value, str) or not value.strip():
        raise ResolutionError("query must be non-empty text")
    if len(value.encode("utf-8")) > MAX_QUERY_BYTES:
        raise ResolutionError("query exceeds 256 UTF-8 bytes")
    value = unicodedata.normalize("NFKC", value)
    return " ".join(value.replace("\u2010", "-").replace("\u2011", "-").split()).casefold()


def exact_records(path, query, limit=32):
    uri = Path(path).resolve().as_uri() + "?mode=ro&immutable=1"
    database = sqlite3.connect(uri, uri=True)
    try:
        rows = database.execute(
            "SELECT DISTINCT r.json FROM names n JOIN records r USING(record_id) "
            "WHERE n.normalized=? ORDER BY r.display_name,r.canonical_smiles,r.record_id LIMIT ?",
            (normalize_name(query), max(1, min(int(limit), 32)) + 1),
        ).fetchall()
        return [json.loads(row[0]) for row in rows]
    except (sqlite3.Error, json.JSONDecodeError) as exc:
        raise ResolutionError("offline discovery index is damaged") from exc
    finally:
        database.close()
