#!/usr/bin/env python3
"""Regenerate the frozen Analysis-v1 chemistry panels (issue #4)."""

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "chemistry"))
import helper
from rdkit import Chem, rdBase

LICENSE = "Repository-authored fixture facts and annotations, CC0-1.0"
OPSIN_JAR = ROOT / "third_party/opsin/opsin-cli-2.8.0.jar"


def entry(identifier, text, assertions, *, mode="smiles", identity=None, source="repository-authored minimal structure", note=""):
    return {"id": identifier, "mode": mode, "text": text, "identityGroup": identity,
            "assertions": assertions, "source": source, "license": LICENSE, "annotation": note}


def a(heavy, elements, charge=0, **extra):
    result = {"heavyAtoms": heavy, "elementCounts": elements, "formalCharge": charge}
    result.update(extra)
    return result


def sequence_smiles(sequence):
    molecule = Chem.MolFromSequence(sequence)
    if molecule is None:
        raise RuntimeError(f"RDKit could not author peptide {sequence}")
    return Chem.MolToSmiles(molecule, canonical=True, isomericSmiles=True)


def identity_panel():
    groups = [
        ("ethanol", "CCO", "OCC", a(3, {"C": 2, "O": 1})),
        ("propane", "CCC", "C(C)C", a(3, {"C": 3})),
        ("acetone", "CC(=O)C", "O=C(C)C", a(4, {"C": 3, "O": 1}, carbonyl=1)),
        ("acetic-acid", "CC(=O)O", "OC(C)=O", a(4, {"C": 2, "O": 2}, carbonyl=1)),
        ("benzene-aromatic", "c1ccccc1", "C1=CC=CC=C1", a(6, {"C": 6}, rings=1)),
        ("pyridine-aromatic", "n1ccccc1", "C1=CC=NC=C1", a(6, {"C": 5, "N": 1}, rings=1)),
        ("phenol-traversal", "Oc1ccccc1", "c1ccc(cc1)O", a(7, {"C": 6, "O": 1}, phenol=1)),
        ("ethyl-acetate", "CCOC(C)=O", "CC(=O)OCC", a(6, {"C": 4, "O": 2}, carbonyl=1)),
        ("glycine", "NCC(=O)O", "O=C(O)CN", a(5, {"C": 2, "N": 1, "O": 2}, carbonyl=1)),
        ("chlorobenzene", "Clc1ccccc1", "c1ccc(cc1)Cl", a(7, {"C": 6, "Cl": 1}, arylHalide=1)),
        ("fragment-order", "[Na+].CC(=O)[O-]", "CC(=O)[O-].[Na+]", a(5, {"C": 2, "O": 2, "other": 1})),
        ("salt-three-fragments", "[Cl-].[K+].CCO", "CCO.[K+].[Cl-]", a(5, {"C": 2, "O": 1, "Cl": 1, "other": 1})),
        ("explicit-h-ethanol", "[H]OC([H])([H])C([H])([H])[H]", "CCO", a(3, {"C": 2, "O": 1})),
        ("explicit-h-ammonia", "[H]N([H])[H]", "N", a(1, {"N": 1})),
        ("isotope-carbon", "[13CH3]CO", "OC[13CH3]", a(3, {"C": 2, "O": 1}, isotope=True)),
        ("isotope-deuterium", "[2H]OC", "CO[2H]", a(2, {"C": 1, "O": 1}, isotope=True)),
        ("stereo-lactic-r", "C[C@H](O)C(=O)O", "O=C(O)[C@@H](O)C", a(6, {"C": 3, "O": 3}, stereo="specified")),
        ("stereo-lactic-s", "C[C@@H](O)C(=O)O", "O=C(O)[C@H](O)C", a(6, {"C": 3, "O": 3}, stereo="specified")),
        ("alkene-e", "C/C=C/C", "C(=C/C)\\C", a(4, {"C": 4}, stereo="specified")),
        ("alkene-z", "C/C=C\\C", "C(=C\\C)\\C", a(4, {"C": 4}, stereo="specified")),
    ]
    records = []
    for group, left, right, assertions in groups:
        records += [entry(f"identity-{group}-a", left, assertions, identity=group),
                    entry(f"identity-{group}-b", right, assertions, identity=group)]
    non_equivalent = [
        ("charge-neutral-amine", "CN", a(2, {"C": 1, "N": 1}, 0)),
        ("charge-protonated-amine", "C[NH3+]", a(2, {"C": 1, "N": 1}, 1)),
        ("tautomer-keto", "CC(=O)C", a(4, {"C": 3, "O": 1})),
        ("tautomer-enol", "CC(O)=C", a(4, {"C": 3, "O": 1})),
        ("enantiomer-r", "F[C@H](Cl)Br", a(4, {"C": 1, "F": 1, "Cl": 1, "Br": 1}, stereo="specified")),
        ("enantiomer-s", "F[C@@H](Cl)Br", a(4, {"C": 1, "F": 1, "Cl": 1, "Br": 1}, stereo="specified")),
        ("stereo-unspecified", "FC(Cl)Br", a(4, {"C": 1, "F": 1, "Cl": 1, "Br": 1}, stereo="unspecified")),
    ]
    records += [entry("identity-" + identifier, text, assertions, note="deliberately non-equivalent") for identifier, text, assertions in non_equivalent]
    return records


PUBCHEM = "https://pubchem.ncbi.nlm.nih.gov/compound/"


def named(identifier, name, smiles, cid, assertions, note="Structure choice follows the cited PubChem compound record."):
    return entry(identifier, smiles, assertions, source=PUBCHEM + str(cid), note=f"{name}. {note}")


def broad_panel():
    rows = [
        ("methane", "methane", "C", 297, a(1,{"C":1})), ("ethanol", "ethanol", "CCO", 702, a(3,{"C":2,"O":1})),
        ("acetone", "acetone", "CC(=O)C", 180, a(4,{"C":3,"O":1},carbonyl=1)), ("acetic-acid", "acetic acid", "CC(=O)O", 176, a(4,{"C":2,"O":2},carbonyl=1)),
        ("glycine", "glycine", "NCC(=O)O", 750, a(5,{"C":2,"N":1,"O":2})), ("l-alanine", "L-alanine", "N[C@@H](C)C(=O)O", 5950, a(6,{"C":3,"N":1,"O":2},stereo="L")),
        ("urea", "urea", "NC(=O)N", 1176, a(4,{"C":1,"N":2,"O":1})), ("ethyl-acetate", "ethyl acetate", "CCOC(=O)C", 8857, a(6,{"C":4,"O":2})),
        ("benzene", "benzene", "c1ccccc1", 241, a(6,{"C":6},rings=1)), ("phenol", "phenol", "Oc1ccccc1", 996, a(7,{"C":6,"O":1},phenol=1)),
        ("aniline", "aniline", "Nc1ccccc1", 6115, a(7,{"C":6,"N":1})), ("pyridine", "pyridine", "n1ccccc1", 1049, a(6,{"C":5,"N":1},rings=1)),
        ("naphthalene", "naphthalene", "c1ccc2ccccc2c1", 931, a(10,{"C":10},rings=2)), ("cyclohexane", "cyclohexane", "C1CCCCC1", 8078, a(6,{"C":6},rings=1)),
        ("caffeine", "caffeine", "Cn1c(=O)c2c(ncn2C)n(C)c1=O", 2519, a(14,{"C":8,"N":4,"O":2})), ("aspirin", "aspirin", "CC(=O)Oc1ccccc1C(=O)O", 2244, a(13,{"C":9,"O":4},carbonyl=2)),
        ("d-glucose", "D-glucose", "OC[C@H]1O[C@@H](O)[C@H](O)[C@@H](O)[C@@H]1O", 5793, a(12,{"C":6,"O":6},stereo="D")),
        ("dimethyl-sulfoxide", "dimethyl sulfoxide", "CS(C)=O", 679, a(4,{"C":2,"O":1,"S":1})), ("thiophene", "thiophene", "c1ccsc1", 8028, a(5,{"C":4,"S":1},rings=1)),
        ("phosphoric-acid", "phosphoric acid", "OP(=O)(O)O", 1004, a(5,{"O":4,"P":1})), ("sodium-acetate", "sodium acetate", "[Na+].CC(=O)[O-]", 517045, a(5,{"C":2,"O":2,"other":1})),
        ("chlorobenzene", "chlorobenzene", "Clc1ccccc1", 7967, a(7,{"C":6,"Cl":1})), ("serotonin", "serotonin", "NCCc1c[nH]c2ccc(O)cc12", 5202, a(13,{"C":10,"N":2,"O":1})),
        ("cholesterol", "cholesterol", "C[C@H](CCCC(C)C)[C@H]1CC[C@@H]2[C@@]1(CC[C@H]3[C@H]2CC=C4[C@@]3(CC[C@@H](C4)O)C)C", 5997, a(28,{"C":27,"O":1},stereo="PubChem CID 5997 isomeric record")),
    ]
    return [named(*row) for row in rows]


def legacy_panel():
    rows = [
        ("amphetamine","amphetamine","CC(N)Cc1ccccc1",3007,a(10,{"C":9,"N":1})), ("dopamine","dopamine","NCCc1ccc(O)c(O)c1",681,a(11,{"C":8,"N":1,"O":2})),
        ("cathinone","cathinone","NC(C(=O)c1ccccc1)C",62258,a(11,{"C":9,"N":1,"O":1})), ("methcathinone","methcathinone","CNC(C)C(=O)c1ccccc1",1576,a(12,{"C":10,"N":1,"O":1})),
        ("4-methylmethcathinone","4-methylmethcathinone","CNC(C)C(=O)c1ccc(C)cc1",45266826,a(13,{"C":11,"N":1,"O":1})),
        ("mdpv-family","3,4-methylenedioxymethcathinone","CNC(C)C(=O)c1ccc2OCOc2c1",161464,a(15,{"C":11,"N":1,"O":3})),
        ("mda","MDA","CC(N)Cc1ccc2OCOc2c1",1615,a(13,{"C":10,"N":1,"O":2})), ("mdma","MDMA","CNC(C)Cc1ccc2OCOc2c1",16193,a(14,{"C":11,"N":1,"O":2})),
        ("mdea","MDEA","CCNC(C)Cc1ccc2OCOc2c1",105039,a(15,{"C":12,"N":1,"O":2})), ("ketamine","ketamine","CNC1(CCCCC1=O)c1ccccc1Cl",3821,a(16,{"C":13,"N":1,"O":1,"Cl":1})),
        ("norketamine","norketamine","NC1(CCCCC1=O)c1ccccc1Cl",123767,a(15,{"C":12,"N":1,"O":1,"Cl":1})),
        ("testosterone","testosterone","CC12CCC3C(C1CCC2O)CCC1=CC(=O)CCC31C",6013,a(21,{"C":19,"O":2},stereo="record choice")),
        ("estradiol","estradiol","CC12CCC3C(C1CCC2O)CCC1=C3C=CC(O)=C1",5757,a(20,{"C":18,"O":2},stereo="record choice")),
    ]
    records = [named(*row, note="Unspecified/racemic where the SMILES has no stereo marks; cited record fixes constitution.") for row in rows]
    for record in records:
        record["id"] = "legacy-" + record["id"]
    peptides = [("ala-ala","AA"),("gly-gly","GG"),("phe-gly","FG"),("gly-phe","GF"),("ala-gly","AG"),("gly-ala","GA"),("cys-gly","CG"),("met-gly","MG"),
                ("gly-ala-4","GAGA"),("gly-ala-8","GAGAGAGA")]
    for identifier, sequence in peptides:
        smiles = sequence_smiles(sequence)
        molecule = Chem.MolFromSmiles(smiles)
        counts = {}
        for atom in molecule.GetAtoms():
            if atom.GetAtomicNum() > 1: counts[atom.GetSymbol()] = counts.get(atom.GetSymbol(), 0) + 1
        records.append(entry("legacy-" + identifier, smiles, a(molecule.GetNumHeavyAtoms(), counts, stereo="RDKit L-amino-acid convention"),
                             source="RDKit MolFromSequence", note=f"Sequence {sequence}; generated with RDKit {rdBase.rdkitVersion}."))
    return records


def holdout_panel():
    rows = [
        ("propan-1-ol","propan-1-ol","CCCO",1031,a(4,{"C":3,"O":1})), ("propan-2-ol","propan-2-ol","CC(O)C",3776,a(4,{"C":3,"O":1})),
        ("ethylene-glycol","ethylene glycol","OCCO",174,a(4,{"C":2,"O":2})), ("succinic-acid","succinic acid","OC(=O)CCC(=O)O",1110,a(8,{"C":4,"O":4})),
        ("furan","furan","c1ccoc1",8029,a(5,{"C":4,"O":1})), ("thiazole","thiazole","c1cscn1",9257,a(5,{"C":3,"N":1,"S":1})),
        ("anisole","anisole","COc1ccccc1",7519,a(8,{"C":7,"O":1})), ("benzoic-acid","benzoic acid","O=C(O)c1ccccc1",243,a(9,{"C":7,"O":2})),
        ("methyl-acetylsalicylate","acetylsalicylic-acid methyl ester","CC(=O)Oc1ccccc1C(=O)OC",234036,a(14,{"C":10,"O":4})),
        ("valine","L-valine","N[C@@H](C(C)C)C(=O)O",6287,a(8,{"C":5,"N":1,"O":2},stereo="L")),
    ]
    records = [named(*row) for row in rows]
    for identifier, sequence in (("leu-gly","LG"),("gly-leu","GL")):
        smiles = sequence_smiles(sequence); molecule = Chem.MolFromSmiles(smiles); counts = {}
        for atom in molecule.GetAtoms():
            if atom.GetAtomicNum() > 1: counts[atom.GetSymbol()] = counts.get(atom.GetSymbol(), 0) + 1
        records.append(entry("holdout-" + identifier, smiles, a(molecule.GetNumHeavyAtoms(), counts, stereo="RDKit L-amino-acid convention"), source="RDKit MolFromSequence", note=f"Sequence {sequence}; generated with RDKit {rdBase.rdkitVersion}."))
    return records


def invalid_panel():
    return [
        {"id":"invalid-empty","mode":"smiles","text":"","expectedDiagnostic":"non-empty"},
        {"id":"invalid-whitespace","mode":"smiles","text":"   ","expectedDiagnostic":"non-whitespace"},
        {"id":"invalid-smiles","mode":"smiles","text":"C1(","expectedDiagnostic":"invalid"},
        {"id":"invalid-wildcard","mode":"smiles","text":"C*","expectedDiagnostic":"wildcards"},
        {"id":"invalid-query","mode":"smiles","text":"[C;H3]","expectedDiagnostic":"invalid"},
        {"id":"invalid-reaction","mode":"smiles","text":"CCO>>CC=O","expectedDiagnostic":"reactions"},
        {"id":"invalid-newline","mode":"smiles","text":"CCO\nC","expectedDiagnostic":"newlines"},
        {"id":"invalid-over-limit","mode":"smiles","text":"C" * 4097,"expectedDiagnostic":"4096"},
        {"id":"invalid-over-graph","mode":"smiles","text":"C" * 257,"expectedDiagnostic":"256 heavy atoms"},
        {"id":"invalid-components","mode":"smiles","text":".".join(["C"] * 9),"expectedDiagnostic":"8 disconnected"},
        {"id":"invalid-long-name","mode":"name","text":"methane" * 600,"expectedDiagnostic":"4096"},
        {"id":"invalid-malformed-utf8","surface":"protocol-bytes","injection":"ff-fe","expected":"bounded UTF-8 error; last valid state preserved"},
        {"id":"invalid-malformed-helper-json","surface":"protocol-json","injection":"{","expected":"bounded JSON error; last valid state preserved"},
        {"id":"invalid-timeout","surface":"process","injection":"synthetic child exceeds deadline","expected":"process tree cancelled; last valid state preserved"},
        {"id":"invalid-missing-helper","surface":"process","injection":"helper executable absent","expected":"bounded unavailable status; saved sound remains playable"},
        {"id":"invalid-missing-jre","surface":"process","injection":"private Java executable absent","expected":"bounded unavailable status; last valid state preserved"},
        {"id":"invalid-cancelled-job","surface":"coordinator","injection":"newer document generation","expected":"stale result cannot publish"},
        {"id":"invalid-analysis-version","surface":"analysis-decoder","injection":{"analysisVersion":2},"expected":"strict rejection; last valid state preserved"},
        {"id":"invalid-nonfinite-patch","surface":"patch-decoder","injection":"NaN parameter","expected":"strict rejection; last valid state preserved"},
        {"id":"invalid-oversized-patch","surface":"patch-decoder","injection":"more than bounded nodes/bytes","expected":"strict rejection; last valid state preserved"},
    ]


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(json.dumps(value, indent=2, ensure_ascii=False).encode("utf-8") + b"\n")


def generate(output_root):
    panels = {"identity": identity_panel(), "broad-development": broad_panel(), "legacy-hard": legacy_panel(), "holdout": holdout_panel()}
    canonical_by_panel = {}
    expected = []
    for panel_name, records in panels.items():
        canonical_by_panel[panel_name] = []
        for record in records:
            analysis = helper.analyze(record["mode"], record["text"], str(OPSIN_JAR))
            response = {"protocolVersion": 1, "requestId": record["id"], "status": "ok", "analysis": analysis}
            expected.append({"id": record["id"], "responseSha256": hashlib.sha256(canonical_json(response)).hexdigest(), "response": response})
            canonical_by_panel[panel_name].append(analysis["canonicalIsomericSmiles"])
        panel = {"panelVersion": 1, "analysisVersion": 1, "backend": {"rdkitVersion": rdBase.rdkitVersion, "opsinVersion": "2.8.0", "opsinSha256": helper.OPSIN_SHA256},
                 "annotationLicense": "CC0-1.0", "records": records}
        write_json(output_root / "data/panels" / f"{panel_name}.json", panel)

    invalid = {"panelVersion":1,"annotationLicense":"CC0-1.0","records":invalid_panel()}
    write_json(output_root / "data/panels/adversarial-analysis.json", invalid)
    hard_families = [["legacy-amphetamine","legacy-dopamine"], ["legacy-cathinone","legacy-methcathinone","legacy-4-methylmethcathinone","legacy-mdpv-family"],
                     ["legacy-mda","legacy-mdma","legacy-mdea"], ["legacy-ketamine","legacy-norketamine"], ["legacy-testosterone","legacy-estradiol"],
                     ["legacy-ala-ala","legacy-gly-gly","legacy-phe-gly","legacy-gly-phe","legacy-ala-gly","legacy-gly-ala","legacy-cys-gly","legacy-met-gly"]]
    pairs = []
    for family in hard_families:
        pairs.extend([[family[i], family[j]] for i in range(len(family)) for j in range(i + 1, len(family))])
    pairs.append(["legacy-gly-ala-4","legacy-gly-ala-8"])
    write_json(output_root / "data/panels/legacy-hard-pairs.json", {"manifestVersion":1,"rule":"all unordered pairs within each specified family, plus the 4/8 residue length pair","pairs":pairs})
    invalid_responses = []
    for record in invalid["records"]:
        if record.get("surface") not in (None, "analysis"):
            continue
        try:
            helper.process({"protocolVersion":1,"requestId":record["id"],"mode":record["mode"],"text":record["text"]}, str(OPSIN_JAR))
            raise RuntimeError(f"invalid fixture unexpectedly passed: {record['id']}")
        except helper.InputError as error:
            invalid_responses.append({"id":record["id"],"response":{"protocolVersion":1,"requestId":record["id"],"status":"error","diagnostic":str(error)[:4096]}})
    write_json(output_root / "tests/fixtures/chemistry-analysis-v1.json", {"fixtureVersion":1,"generator":"scripts/generate-chemistry-panels.py","generatorVersion":1,"backend":{"rdkitVersion":rdBase.rdkitVersion,"opsinVersion":"2.8.0","opsinSha256":helper.OPSIN_SHA256},"records":expected,"invalidResponses":invalid_responses})
    manifest_files = [output_root / "data/panels" / f"{name}.json" for name in (*panels.keys(), "adversarial-analysis", "legacy-hard-pairs")] + [output_root / "tests/fixtures/chemistry-analysis-v1.json"]
    hashes = {str(path.relative_to(output_root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in manifest_files}
    write_json(output_root / "data/panels/chemistry-manifest.json", {"manifestVersion":1,"frozenBeforeCalibration":True,"generator":"scripts/generate-chemistry-panels.py","generatorVersion":1,"backend":{"rdkitVersion":rdBase.rdkitVersion,"opsinVersion":"2.8.0","opsinSha256":helper.OPSIN_SHA256},"files":hashes})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--output-root", type=Path, default=ROOT)
    generate(parser.parse_args().output_root.resolve())
