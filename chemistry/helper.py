#!/usr/bin/env python3
"""One-shot, synth-independent molecular Analysis protocol."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from rdkit import Chem, rdBase
from rdkit.Chem import Crippen, Descriptors, Lipinski, rdMolDescriptors
from resolution import ResolutionError, exact_records

PROTOCOL_VERSION = 1
ANALYSIS_VERSION = 1
MAX_INPUT_BYTES = 4096
MAX_RESPONSE_BYTES = 256 * 1024
MAX_REQUEST_BYTES = 1024 * 1024
OPSIN_TIMEOUT_SECONDS = 10
OPSIN_SHA256 = "d25bc08f41b8f6fcd6f35e18ab83f3b8d9218cdb003d55c5f74aaefe2e0c68ab"

MOTIFS = (
    ("carbonyl", "[CX3]=[OX1]"),
    ("amide", "[NX3][CX3](=[OX1])"),
    ("amine", "[NX3;H2,H1,H0;!$(NC=O)]"),
    ("alcohol", "[OX2H][CX4]"),
    ("phenol", "[OX2H][c]"),
    ("ether", "[OD2]([#6])[#6]"),
    ("arylHalide", "[c][F,Cl,Br,I]"),
)
ELEMENTS = ("C", "N", "O", "S", "P", "F", "Cl", "Br", "I")


class InputError(Exception):
    pass


def _validate_text(text):
    if not isinstance(text, str) or not text:
        raise InputError("text must be a non-empty UTF-8 string")
    raw = text.encode("utf-8")
    if len(raw) > MAX_INPUT_BYTES:
        raise InputError("input exceeds 4096 UTF-8 bytes")
    if "\x00" in text or "\n" in text or "\r" in text:
        raise InputError("input must not contain NUL or newlines")
    text = text.strip()
    if not text:
        raise InputError("text must contain non-whitespace content")
    return text


def _bundle_root():
    """Return the immutable payload root for a frozen helper."""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent.parent
    return Path(__file__).resolve().parent.parent


def _payload_paths():
    root = _bundle_root()
    if getattr(sys, "frozen", False):
        return root / "resources" / "opsin-cli-2.8.0.jar", root / "java" / "bin" / "java"
    jar = Path(os.environ.get("IUPAC_OPSIN_JAR", root / "third_party" / "opsin" / "opsin-cli-2.8.0.jar"))
    return jar, Path(os.environ.get("IUPAC_JAVA_EXECUTABLE", "java"))


def _discovery_path():
    root = _bundle_root()
    if getattr(sys, "frozen", False):
        return root / "resources" / "discovery" / "discovery-v1.sqlite3"
    return Path(os.environ.get("IUPAC_DISCOVERY_INDEX", root / "data" / "discovery" / "discovery-v1.sqlite3"))


def _resolve_name(name, opsin_jar=None):
    """Apply the shared #22 exact-name policy before falling back to OPSIN."""
    path = _discovery_path()
    if path.is_file():
        try:
            candidates = exact_records(path, name)
            structures = sorted({item["canonicalIsomericSmiles"] for item in candidates})
            if len(structures) == 1:
                return structures[0]
            if len(structures) > 1:
                labels = ", ".join(item["displayName"] for item in candidates[:8])
                raise InputError("name is ambiguous in the offline index; select a candidate: " + labels)
        except ResolutionError as exc:
            raise InputError("offline discovery index is damaged: " + str(exc)) from exc
    bundled_jar, java = _payload_paths()
    jar = Path(opsin_jar) if opsin_jar else bundled_jar
    return _opsin(name, jar, java)


def _child_environment(java_path):
    # Never let a host/DAW Python, Java or freezer loader setting redirect the
    # private runtime. The frozen executable has already loaded its own native
    # closure before this child environment is created.
    blocked = {
        "CLASSPATH", "JAVA_HOME", "JDK_JAVA_OPTIONS", "JAVA_TOOL_OPTIONS", "_JAVA_OPTIONS",
        "PYTHONHOME", "PYTHONPATH", "VIRTUAL_ENV", "CONDA_PREFIX", "LD_LIBRARY_PATH",
        "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "DYLD_INSERT_LIBRARIES",
    }
    env = {key: value for key, value in os.environ.items() if key not in blocked}
    if java_path.is_absolute():
        env["JAVA_HOME"] = str(java_path.parent.parent)
    return env


def _opsin(name, jar_path, java_path=None):
    if not jar_path or not os.path.isfile(jar_path):
        raise InputError("OPSIN 2.8.0 artifact is missing")
    java_path = Path(java_path or _payload_paths()[1])
    if java_path.is_absolute() and not java_path.is_file():
        raise InputError("private Java 17 runtime is missing")
    with tempfile.TemporaryDirectory(prefix="iupac-opsin-") as directory:
        source = os.path.join(directory, "name.txt")
        target = os.path.join(directory, "structure.smi")
        with open(source, "x", encoding="utf-8") as stream:
            stream.write(name + "\n")
        process = subprocess.Popen(
            [str(java_path), "-Xmx256m", "-jar", str(jar_path), "-o", "smi", source, target],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=_child_environment(java_path), start_new_session=True,
        )
        try:
            stdout, stderr = process.communicate(timeout=OPSIN_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as exc:
            try:
                os.killpg(process.pid, 15)
                process.communicate(timeout=1)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                try:
                    os.killpg(process.pid, 9)
                except ProcessLookupError:
                    pass
                process.communicate()
            raise InputError("OPSIN name resolution exceeded its deadline") from exc
        diagnostic = stderr[:65536].decode("utf-8", "replace").strip()
        if process.returncode != 0:
            raise InputError("OPSIN could not resolve the name" + (": " + diagnostic if diagnostic else ""))
        try:
            with open(target, encoding="utf-8") as stream:
                structure = stream.read(8193).strip()
        except OSError as exc:
            raise InputError("OPSIN produced no structure") from exc
        if not structure or len(structure.encode("utf-8")) > 8192:
            raise InputError("OPSIN produced an empty or oversized structure")
        return structure


def _molecule(smiles):
    if "*" in smiles or ">" in smiles or "|" in smiles:
        raise InputError("queries, wildcards, reactions, and extended stereochemistry are unsupported")
    try:
        with rdBase.BlockLogs():
            molecule = Chem.MolFromSmiles(smiles, sanitize=True)
    except Exception as exc:
        raise InputError("structure failed RDKit sanitization") from exc
    if molecule is None or any(atom.HasQuery() for atom in molecule.GetAtoms()) or any(bond.HasQuery() for bond in molecule.GetBonds()):
        raise InputError("structure is invalid or contains query atoms")
    for atom in molecule.GetAtoms():
        atom.SetAtomMapNum(0)
    molecule = Chem.RemoveHs(molecule, sanitize=True)
    heavy = molecule.GetNumHeavyAtoms()
    total = molecule.GetNumAtoms(onlyExplicit=False)
    components = len(Chem.GetMolFrags(molecule))
    if not 1 <= heavy <= 256:
        raise InputError("structure must contain 1 to 256 heavy atoms")
    if total > 512:
        raise InputError("structure exceeds 512 total graph atoms")
    if components > 8:
        raise InputError("structure exceeds 8 disconnected components")
    canonical = Chem.MolToSmiles(molecule, canonical=True, isomericSmiles=True)
    molecule = Chem.MolFromSmiles(canonical, sanitize=True)
    if molecule is None:
        raise InputError("canonical structure could not be reparsed")
    ranks = list(Chem.CanonicalRankAtoms(molecule, breakTies=True, includeChirality=True, includeIsotopes=True))
    molecule = Chem.RenumberAtoms(molecule, sorted(range(len(ranks)), key=ranks.__getitem__))
    Chem.AssignStereochemistry(molecule, cleanIt=True, force=True)
    return canonical, molecule


def analyze(mode, text, opsin_jar):
    text = _validate_text(text)
    if mode not in ("name", "smiles"):
        raise InputError("mode must be name or smiles")
    structure = _resolve_name(text, opsin_jar) if mode == "name" else text
    canonical, molecule = _molecule(structure)
    atoms = []
    element_counts = {name: 0 for name in ELEMENTS}
    element_counts["other"] = 0
    for atom in molecule.GetAtoms():
        symbol = atom.GetSymbol()
        if atom.GetAtomicNum() > 1:
            element_counts[symbol if symbol in element_counts else "other"] += 1
        atoms.append({
            "atomicNumber": atom.GetAtomicNum(), "isotope": atom.GetIsotope(),
            "formalCharge": atom.GetFormalCharge(), "aromatic": atom.GetIsAromatic(),
            "degree": atom.GetDegree(), "bondOrders": sorted(float(b.GetBondTypeAsDouble()) for b in atom.GetBonds()),
            "stereo": atom.GetProp("_CIPCode") if atom.HasProp("_CIPCode") else "none",
        })
    bonds = [{
        "begin": min(b.GetBeginAtomIdx(), b.GetEndAtomIdx()),
        "end": max(b.GetBeginAtomIdx(), b.GetEndAtomIdx()),
        "order": float(b.GetBondTypeAsDouble()), "stereo": str(b.GetStereo()),
    } for b in molecule.GetBonds()]
    bonds.sort(key=lambda b: (b["begin"], b["end"], b["order"], b["stereo"]))
    if len(bonds) > 1024:
        raise InputError("structure exceeds 1024 bonds")
    motif_matches = {}
    match_total = 0
    for name, smarts in MOTIFS:
        query = Chem.MolFromSmarts(smarts)
        matches = sorted({tuple(match) for match in molecule.GetSubstructMatches(query, uniquify=True)})
        motif_matches[name] = [list(match) for match in matches]
        match_total += len(matches)
    if match_total > 1024:
        raise InputError("structure exceeds 1024 stored motif matches")
    rings = list(molecule.GetRingInfo().AtomRings())
    fused = sum(1 for left in range(len(rings)) for right in range(left + 1, len(rings)) if len(set(rings[left]) & set(rings[right])) >= 2)
    heavy = molecule.GetNumHeavyAtoms()
    return {
        "analysisVersion": ANALYSIS_VERSION,
        "backend": {"rdkitVersion": rdBase.rdkitVersion, "opsinVersion": "2.8.0", "opsinSha256": OPSIN_SHA256},
        "canonicalIsomericSmiles": canonical,
        "descriptors": {
            "heavyAtoms": heavy, "molecularWeight": Descriptors.MolWt(molecule),
            "formalCharge": Chem.GetFormalCharge(molecule), "elementCounts": element_counts,
            "aromaticAtomFraction": sum(a.GetIsAromatic() for a in molecule.GetAtoms()) / heavy,
            "ringCount": len(rings), "fusedRingAdjacencyCount": fused,
            "rotatableBonds": Lipinski.NumRotatableBonds(molecule), "fractionCsp3": rdMolDescriptors.CalcFractionCSP3(molecule),
            "hbd": Lipinski.NumHDonors(molecule), "hba": Lipinski.NumHAcceptors(molecule),
            "tpsa": rdMolDescriptors.CalcTPSA(molecule), "logP": Crippen.MolLogP(molecule),
            "motifCounts": {name: len(matches) for name, matches in motif_matches.items()},
        },
        "detail": {"atoms": atoms, "bonds": bonds, "motifMatches": motif_matches},
    }


def process(request, opsin_jar):
    if not isinstance(request, dict):
        raise InputError("request must be an object")
    if request["protocolVersion"] != PROTOCOL_VERSION:
        raise InputError("unsupported protocolVersion")
    request_id = request["requestId"]
    if not isinstance(request_id, (str, int)) or isinstance(request_id, bool):
        raise InputError("requestId must be a string or integer")
    action = request.get("action", "analyze")
    if action == "analyze":
        if set(request) not in ({"protocolVersion", "requestId", "mode", "text"},
                                {"protocolVersion", "requestId", "action", "mode", "text"}):
            raise InputError("analysis request has unexpected fields")
        result = {"analysis": analyze(request["mode"], request["text"], opsin_jar)}
    else:
        import discovery
        index = discovery.DiscoveryIndex(_discovery_path())
        if action == "discover":
            if set(request) - {"protocolVersion", "requestId", "action", "query", "prefix", "limit"}:
                raise InputError("discovery request has unexpected fields")
            result = {"discovery": index.search(request.get("query"), bool(request.get("prefix", False)), request.get("limit", discovery.MAX_RESULTS))}
        elif action == "record":
            result = {"record": index.record(request.get("recordId"))}
        elif action.startswith("cache-"):
            revision = str(index.db.execute("SELECT value FROM metadata WHERE key='recordsSha256'").fetchone()[0])
            root = Path(os.environ.get("IUPAC_GENERATED_CACHE", Path.home()/".cache"/"iupac-synth-2"))
            cache = discovery.GeneratedFileCache(root, revision)
            if action == "cache-put":
                result = {"cache": cache.put(request.get("identity"), request.get("versions"), request.get("settings", {}), request.get("state"))}
            elif action == "cache-get":
                result = {"cache": cache.get(request.get("key"))}
            elif action == "cache-inspect":
                result = {"cache": cache.inspect()}
            elif action == "cache-clear":
                result = {"cache": {"cleared": cache.clear()}}
            else:
                raise InputError("unsupported cache action")
        else:
            raise InputError("unsupported helper action")
    return {"protocolVersion": PROTOCOL_VERSION, "requestId": request_id, "status": "ok", **result}


def main():
    request_id = None
    try:
        raw = sys.stdin.buffer.read(MAX_REQUEST_BYTES + 1)
        if len(raw) > MAX_REQUEST_BYTES:
            raise InputError("request exceeds bounded input size")
        request = json.loads(raw.decode("utf-8"))
        if isinstance(request, dict):
            request_id = request.get("requestId")
        jar_path, _ = _payload_paths()
        response = process(request, str(jar_path))
    except (InputError, UnicodeError, json.JSONDecodeError, OSError, ValueError, TypeError) as exc:
        response = {"protocolVersion": PROTOCOL_VERSION, "requestId": request_id, "status": "error",
                    "diagnostic": str(exc)[:4096]}
    except Exception as exc:  # discovery/sqlite boundary: never emit a traceback protocol
        response = {"protocolVersion": PROTOCOL_VERSION, "requestId": request_id, "status": "error",
                    "diagnostic": str(exc)[:4096]}
    encoded = json.dumps(response, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if len(encoded) > MAX_RESPONSE_BYTES:
        encoded = json.dumps({"protocolVersion": PROTOCOL_VERSION, "requestId": request_id, "status": "error",
                              "diagnostic": "analysis response exceeds 256 KiB"}, separators=(",", ":")).encode()
    sys.stdout.buffer.write(encoded + b"\n")
    return 0 if response["status"] == "ok" else 2


if __name__ == "__main__":
    raise SystemExit(main())
