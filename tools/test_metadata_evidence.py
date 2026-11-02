#!/usr/bin/env python3
"""Compare every pinned model XML against Python's independent ElementTree reader."""
import argparse
import json
from pathlib import Path
from pathlib import PurePosixPath
import subprocess
import xml.etree.ElementTree as ET

from corpus import materialize, safe_path, verify


def expected(raw):
    root = ET.fromstring(raw)
    if root.tag != "TeklaStructuresModels" or len(root.findall("Model")) != 1:
        raise ValueError("unexpected public metadata structure")
    model = root.find("Model")
    names = ("Name", "Designer", "Description", "Version", "ProductVersion", "Language",
             "Template", "Environment", "XS_PROJECT", "XS_FIRM", "XS_SYSTEM", "ConnectedId")
    fields = [model.findtext(name, "").encode("utf8") for name in names]
    boolean = model.findtext("IsTemplate", "").strip().upper()
    if boolean not in ("", "TRUE", "FALSE", "1", "0"):
        raise ValueError("unexpected public metadata boolean")
    fingerprint = 14695981039346656037

    def update(data):
        nonlocal fingerprint
        for byte in data:
            fingerprint = ((fingerprint ^ byte) * 1099511628211) & ((1 << 64) - 1)

    for field in fields:
        update(len(field).to_bytes(8, "little"))
        update(field)
    update(int(boolean in ("TRUE", "1")).to_bytes(8, "little"))
    update(len(raw).to_bytes(8, "little"))
    update(raw)
    return f"fields=13 raw_bytes={len(raw)} fingerprint={fingerprint:x}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / "tests/corpus.json").read_text(encoding="utf8"))
    cases = {c["path"]: c for c in manifest["cases"] if c["mode"] == "model_metadata"}
    files = manifest["files"]
    db1_by_parent = {}
    for candidate in files:
        candidate_path = PurePosixPath(candidate["path"])
        if candidate_path.suffix.lower() == ".db1" and candidate_path.name.lower() != "xslib.db1":
            db1_by_parent.setdefault(str(candidate_path.parent), []).append(candidate)
    project_pair = next(
        ((spec, db1_by_parent.get(str(PurePosixPath(spec["path"]).parent), []))
         for spec in files if spec["path"] in cases
         and len(db1_by_parent.get(str(PurePosixPath(spec["path"]).parent), [])) == 1),
        None,
    )
    if project_pair is None:
        raise ValueError("metadata evidence manifest has no unambiguous sibling DB1 project pair")
    project_xml, project_db1 = project_pair[0], project_pair[1][0]
    count = 0
    for spec in files:
        if spec["path"] not in cases:
            continue
        path = safe_path(args.data.resolve(), spec["path"])
        if args.download:
            materialize(spec, path, manifest.get("archives", {}), args.data.resolve())
        if not verify(path, spec):
            raise ValueError(f"missing or changed pinned metadata: {path}")
        oracle = expected(path.read_bytes())
        if cases[spec["path"]]["stdout"] != oracle:
            raise ValueError(f"metadata expectation disagrees with independent XML reader: {path}")
        result = subprocess.run([str(args.exe.resolve()), "model_metadata", str(path)],
                                capture_output=True, encoding="utf8", timeout=30, check=True)
        if result.stdout.strip() != oracle:
            raise ValueError(f"metadata fields disagree with independent XML reader: {path}")
        if spec["path"] == project_xml["path"]:
            db1_path = safe_path(args.data.resolve(), project_db1["path"])
            if args.download:
                materialize(project_db1, db1_path, manifest.get("archives", {}), args.data.resolve())
            if not verify(db1_path, project_db1):
                raise ValueError(f"missing or changed pinned project DB1: {db1_path}")
            project_result = subprocess.run(
                [str(args.exe.resolve()), "model_metadata_project", str(path.parent)],
                capture_output=True, encoding="utf8", timeout=60)
            if project_result.returncode != 0:
                raise RuntimeError(
                    f"project metadata association failed for {path.parent}: "
                    f"{project_result.stderr.strip() or project_result.stdout.strip()}")
            if project_result.stdout.strip() != "model_metadata=partial fields=13 associated=1":
                raise ValueError(f"project metadata file was not linked to its selected DB1: {path.parent}")
        count += 1
    if count != len(cases) or count < 32:
        raise ValueError("missing public metadata evidence")
    print(f"{count}/{count} independent metadata and selected-DB1 project association checks passed")


if __name__ == "__main__":
    main()
