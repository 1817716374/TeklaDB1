#!/usr/bin/env python3
"""Compare saved DG metadata fields with ElementTree and exercise project pairing."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import struct
import xml.etree.ElementTree as ET

from corpus import materialize, safe_path, verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1]/"tests/corpus.json").read_text(encoding="utf8"))
    specs = [s for s in manifest["files"] if s["path"].startswith("exam-drawings/")]
    if len(specs) != 13:
        raise ValueError("drawing evidence coverage changed")
    for spec in specs:
        source = safe_path(args.data.resolve(), spec["path"])
        if args.download:
            materialize(spec, source, manifest.get("archives", {}), args.data.resolve())
        if not verify(source, spec):
            raise ValueError(f"missing or changed evidence: {source}")
        destination = safe_path(args.work.resolve(), spec["path"])
        if destination == source:
            raise ValueError("separate work directory required")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    root = args.work/"exam-drawings"
    paths = sorted((root/"drawings").glob("*.dg.metadata"))
    if len(paths) != 5:
        raise ValueError("unexpected drawing metadata work files")
    path = root/"drawings/D9654e52e-2e28-4ee5-85cd-75a8d8e605e9.dg.metadata"
    original = path.read_bytes()

    def run(mode, target=root, fail=False):
        result = subprocess.run([str(args.exe.resolve()), mode, str(target.resolve())],
                                capture_output=True, encoding="utf8", timeout=90)
        if fail:
            if result.returncode == 0:
                raise ValueError("malformed metadata unexpectedly accepted")
            return
        if result.returncode:
            raise ValueError(result.stderr)
        return dict(field.split("=", 1) for field in result.stdout.strip().split())

    strings = {"DrawingGuid", "MainObjectGuid", "Author", "Mark", "Name", "Title1", "Title2", "Title3"}

    def fields(target):
        element = ET.fromstring(target.read_bytes())
        if element.tag != "DrawingVersionMetadata":
            raise ValueError("unexpected metadata XML root")
        expected = {e.tag: (e.text or "").strip(" \r\n\t") for e in element}
        actual = run("drawing_metadata_fields", target)
        if bytes.fromhex(actual.pop("raw")) != target.read_bytes():
            raise ValueError("raw drawing XML was changed")
        if set(actual) != set(expected):
            raise ValueError("drawing metadata field presence changed")
        for key, value in expected.items():
            if key in strings:
                value = value.lower() if key.endswith("Guid") else value
                equal = bytes.fromhex(actual[key]).decode("utf8") == value
            elif key in ("Width", "Height"):
                equal = float(actual[key]) == float(value)
            else:
                equal = int(actual[key]) == int(value)
            if not equal:
                raise ValueError(f"metadata differs from independent XML: {key}")

    def project(pairs=5, objects=1, metadata=5, failed=0, discovered=0, drawings=5, mode="drawing_metadata_project"):
        expected = dict(pairs=pairs, objects=objects, metadata=metadata, failed=failed, discovered=discovered, drawings=drawings)
        actual = {k: int(v) for k, v in run(mode).items()}
        if actual != expected:
            raise ValueError(f"project metadata mismatch: {actual}, expected {expected}")

    for target in paths:
        fields(target)
    project()
    project(pairs=0, objects=0, metadata=0, discovered=5, mode="drawing_metadata_project_disabled")
    project(pairs=0, objects=0, drawings=0, mode="drawing_metadata_project_no_drawings")
    mutations = 0
    try:
        # These are valid metadata documents with conflicts, not parse failures.
        for old, new in [(b"<Width>\r\n297", b"<Width>\r\n298"),
                         (b"<Height>\r\n210", b"<Height>\r\n211"),
                         (b"<DrawingType>\r\n1", b"<DrawingType>\r\n3"),
                         (b"02f6a87b-e776-431e-a2db-9f8f1865e98c", b"02f6a87b-e776-431e-a2db-9f8f1865e98d"),
                         (b"02f6a87b-e776-431e-a2db-9f8f1865e98c", b""),
                         (b"6b38a239-b7b0-4bb2-ad69-21cebb58a2fa", b"")]:
            if old not in original:
                raise ValueError("mutation anchor missing")
            path.write_bytes(original.replace(old, new))
            project(pairs=4, objects=0)
            mutations += 1
        path.write_bytes(original)
        # Flags cannot manufacture a new model link, currentness or version choice.
        for tag in ("UpToDate", "Lock", "Revision"):
            element = ET.fromstring(original)
            element.find(tag).text = "57"
            path.write_bytes(ET.tostring(element, encoding="utf-8"))
            fields(path)
            project()
            mutations += 1
        # Missing dimensions stay absent; nested lookalikes cannot replace them.
        element = ET.fromstring(original)
        width = element.find("Width")
        element.remove(width)
        ET.SubElement(element, "Extension").append(width)
        path.write_bytes(ET.tostring(element, encoding="utf-8"))
        project(pairs=4, objects=0)
        mutations += 1
        # Malformed companions follow strict/lenient policy without losing the model.
        path.write_bytes(original.replace(b"<Width>", b"<Width/><Width>", 1))
        run("drawing_metadata_project", fail=True)
        project(pairs=4, objects=0, metadata=4, failed=1, mode="drawing_metadata_project_lenient")
        mutations += 1
    finally:
        path.write_bytes(original)
    dg = path.with_suffix("")
    dg_original = dg.read_bytes()
    raw = gzip.decompress(dg_original)
    # Alter the DG's own project identity while leaving its sidecar untouched.
    # Matching MainObjectGuid alone must never bypass the DG project scope.
    project_guid = b"a09d32dc-547d-4f09-a597-b4bd338e81fa"
    if raw.count(project_guid) != 1:
        raise ValueError("DG project GUID mutation anchor changed")
    try:
        dg.write_bytes(gzip.compress(raw.replace(project_guid, b"b09d32dc-547d-4f09-a597-b4bd338e81fa"), mtime=0))
        project(pairs=4, objects=0)
        mutations += 1
        # Independently walk record boundaries to mutate the numeric subject ID.
        cursor, sections = 76, []
        while cursor < len(raw):
            magic, width, count = struct.unpack_from("<III", raw, cursor)
            if magic != 0xdbcec066:
                raise ValueError("DG framing changed")
            flags = struct.unpack_from("<"+"I"*count, raw, cursor+12)
            cursor += 12+4*count
            rows = []
            while cursor < len(raw) and raw[cursor] in (4, 12):
                rows.append(cursor+1)
                cursor += width+1+24+16*sum(flags)
            if raw[cursor] != 0:
                raise ValueError("DG terminator changed")
            cursor += 1
            sections.append((width, rows))
        types = [struct.unpack_from("<I", raw, p)[0] for p in sections[0][1]]
        if len(types) != 47 or len(sections) != 48:
            raise ValueError("DG directory changed")
        tables = dict(zip(reversed(types), sections[1:]))
        changed = bytearray(raw)
        struct.pack_into("<I", changed, tables[269][1][0]+16, 1)
        dg.write_bytes(gzip.compress(changed, mtime=0))
        project(pairs=4, objects=0)
        mutations += 1
    finally:
        dg.write_bytes(dg_original)
    renamed_dg, renamed_metadata = dg.with_name("renamed.dg"), path.with_name("renamed.dg.metadata")
    if renamed_dg.exists() or renamed_metadata.exists():
        raise ValueError("unexpected existing rename fixture")
    try:
        dg.rename(renamed_dg)
        path.rename(renamed_metadata)
        project(pairs=4, objects=0)  # DG's internally saved filename disagrees.
        mutations += 1
    finally:
        if renamed_dg.exists():
            renamed_dg.rename(dg)
        if renamed_metadata.exists():
            renamed_metadata.rename(path)
    orphan = path.with_name("orphan.dg.metadata")
    try:
        shutil.copyfile(path, orphan)
        project(metadata=6)
        mutations += 1
    finally:
        orphan.unlink(missing_ok=True)
    # On case-sensitive systems both sidecars exist; both candidate links must
    # be excluded even though each document is individually valid.
    alternate = path.with_name(path.name.upper())
    if not alternate.exists():
        try:
            shutil.copyfile(path, alternate)
            project(metadata=6, pairs=4, objects=0)
            mutations += 1
        finally:
            alternate.unlink(missing_ok=True)
    project()
    for target in paths:
        fields(target)
    print(f"5 metadata documents / 22 fields independently checked; project controls and {mutations} mutations passed")


if __name__ == "__main__":
    main()
