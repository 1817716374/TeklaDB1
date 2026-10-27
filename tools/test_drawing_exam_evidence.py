#!/usr/bin/env python3
"""Check five official DGs against XML metadata and scoped DB1 numbering."""
import argparse
import gzip
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
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
    specs = [f for f in manifest["files"] if f["path"].startswith("exam-drawings/")]
    if len(specs) != 13:
        raise ValueError("missing pinned exam drawing inputs")
    for spec in specs:
        source = safe_path(args.data.resolve(), spec["path"])
        if args.download:
            materialize(spec, source, manifest.get("archives", {}), args.data.resolve())
        if not verify(source, spec):
            raise ValueError(f"missing or changed drawing evidence: {source}")
        destination = safe_path(args.work.resolve(), spec["path"])
        if destination.resolve() == source.resolve():
            raise ValueError("work directory must differ from evidence directory")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    root = args.work/"exam-drawings"
    paths = sorted((root/"drawings").glob("*.dg"))
    dg = root/"drawings/D9654e52e-2e28-4ee5-85cd-75a8d8e605e9.dg"
    metadata = Path(str(dg)+".metadata")

    def run(mode, path):
        result = subprocess.run([str(args.exe.resolve()), mode, str(path.resolve())],
                                capture_output=True, encoding="utf8", timeout=90)
        if result.returncode:
            raise ValueError(result.stderr)
        return dict(field.split("=", 1) for field in result.stdout.strip().split())

    def xml(path):
        element = ET.fromstring(path.read_bytes())
        if element.tag != "DrawingVersionMetadata":
            raise ValueError("unexpected metadata root")
        return {child.tag: (child.text or "").strip() for child in element}

    def fields(path):
        actual = run("drawing_exam_fields", path)
        expected = xml(Path(str(path)+".metadata"))
        if (float(actual["width"]) != float(expected["Width"]) or
                float(actual["height"]) != float(expected["Height"]) or actual["type"] != expected["DrawingType"]):
            raise ValueError("DG sheet/type disagree with independent XML")
        if not expected["MainObjectGuid"] and actual["subject_id"] != "0":
            raise ValueError("general arrangement acquired a fabricated subject")

    def project():
        actual = run("drawing_exam_project", root)
        expected = xml(metadata)
        mark = re.fullmatch(r"\[(.*)\.(\d+)\]", expected["Mark"])
        if mark is None or actual["guid"] != expected["MainObjectGuid"] or actual["prefix"] != mark[1] or int(actual["number"]) != int(mark[2]):
            raise ValueError("DG/DB1 identity and position disagree with independent metadata")

    def rejected(check):
        try:
            check()
        except ValueError:
            return
        raise ValueError("mutated DG evidence was accepted")

    if len(paths) != 5:
        raise ValueError("drawing coverage changed")
    for path in paths:
        run("raw", path)  # C++ reconstructs every byte from the parsed tables.
        fields(path)
    project()
    original, original_metadata = dg.read_bytes(), metadata.read_bytes()
    raw = gzip.decompress(original)
    cursor = 76
    sections = []
    while cursor < len(raw):
        magic, width, count = struct.unpack_from("<III", raw, cursor)
        if magic != 0xdbcec066:
            raise ValueError("independent DG section framing changed")
        descriptors = struct.unpack_from("<"+"I"*count, raw, cursor+12)
        cursor += 12+4*count
        rows = []
        while cursor < len(raw) and raw[cursor] in (4, 12):
            rows.append(cursor)
            cursor += 1+width+24+16*sum(descriptors)
        if raw[cursor] != 0:
            raise ValueError("independent DG table terminator changed")
        cursor += 1
        sections.append((width, rows))
    types = [struct.unpack_from("<I", raw, row+1)[0] for row in sections[0][1]]
    if len(types) != 47 or len(sections) != 48:
        raise ValueError("9.08 DG directory changed")
    tables = dict(zip(reversed(types), sections[1:]))
    sheet = tables[266][1][0]+1
    subject = tables[269][1][0]+1
    reference = tables[322][1][0]+1
    prop = next(row+1 for row in tables[298][1] if raw[row+9:row+22] == b"grProjectGuid")
    def write_raw(data):
        dg.write_bytes(gzip.compress(data, mtime=0))
    try:
        changed = bytearray(raw);struct.pack_into("<d", changed, sheet+16, 298.0)
        write_raw(changed);rejected(lambda: fields(dg))
        changed = bytearray(raw);struct.pack_into("<I", changed, subject+12, 3)
        write_raw(changed);rejected(lambda: fields(dg))
        dg.write_bytes(original)
        metadata.write_bytes(original_metadata.replace(b"<Width>\r\n297", b"<Width>\r\n298"))
        rejected(lambda: fields(dg));metadata.write_bytes(original_metadata)
        changed = bytearray(raw);struct.pack_into("<I", changed, subject+16, 1)
        write_raw(changed);rejected(project)
        changed = bytearray(raw);struct.pack_into("<I", changed, reference+8, 1)
        write_raw(changed);rejected(project)
        changed = bytearray(raw);changed[prop+29] = ord("0")
        write_raw(changed);rejected(project);run("drawing_exam_unpaired", root)
        # A structurally valid 8.95 serialization must not pair with DB1 9.08.
        kept = [value for value in reversed(types) if value != 263]
        sequential = bytearray(b"Xsteel  8.95")+struct.pack("<II", len(kept), 4)
        for value in reversed(kept):
            sequential += b"\x04"+struct.pack("<I", value)
        for value in kept:
            width, rows = tables[value]
            sequential += struct.pack("<II", len(rows), width)
            for row in rows:
                sequential += raw[row:row+1+width]
        write_raw(sequential);rejected(project);run("drawing_exam_unpaired", root)
        dg.write_bytes(original)
        metadata.write_bytes(original_metadata.replace(b"02f6a87b-e776-431e-a2db-9f8f1865e98c", b"12f6a87b-e776-431e-a2db-9f8f1865e98c"))
        rejected(project);metadata.write_bytes(original_metadata)
        metadata.write_bytes(original_metadata.replace(b"[M.92]", b"[M.93]"))
        rejected(project)
    finally:
        dg.write_bytes(original)
        metadata.write_bytes(original_metadata)
    project()
    print("5 DG byte reconstructions and XML sheet/type checks, scoped DB1 GUID/number pairing, 9/9 mutations passed")


if __name__ == "__main__":
    main()
