# Offline discovery snapshot v2 (Tier A)

`wikidata-archive-v2.json.gz` is the finite CC0 source archive. It holds the exact SPARQL TSV responses
(the sitelinked item lists and the P233/P2017/P235 values for explicit QID batches), the exact
`wbgetentities` responses, request URLs, entity revisions, retrieval time and SHA-256 of every response.
`records-v2.jsonl` is the validated portable record set, one record per canonical structure, ranked by
Wikipedia sitelink count. `discovery-v2.sqlite3` is its read-only production search index.
`manifest-v2.json` records file hashes, every rejected or conflicting input with its reason, and coverage.
`reviewed-aliases-v2.json` is the short, repository-reviewed list of common names that Wikidata lacks.
It is CC0 and names only, and each entry attaches a name to an archived QID; it never supplies a structure.
`alias-coverage-v2.json` is the checked-in list of common names the browser must resolve, with the QID each
must resolve to.

Normal builds and tests never access the network. `scripts/build-discovery.py --write-data` converts the
archive into records, index and manifest. `--output-dir DIR` re-converts a deterministic sample against
the retained records and rebuilds the index (the ctest path). An explicit maintainer refresh runs
`scripts/curate-discovery.py` first; review all revision, hash and rejection changes before committing.

Wikidata structured data is distributed under CC0 1.0. Source records are linked by QID and exact
revision. This snapshot contains no Wikipedia prose or media. Tier A and its browser rules are recorded in
[DISCOVERY.md](../../docs/architecture/DISCOVERY.md).
