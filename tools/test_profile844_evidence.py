#!/usr/bin/env python3
"""Verify pinned 8.44 IFC evidence and reject profile/placement mutations."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import struct
import subprocess

from corpus import materialize, safe_path, verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / "tests/corpus.json").read_text(encoding="utf8"))
    specs = [f for f in manifest["files"] if f["path"].startswith("auvent844-ifc/")]
    if len(specs) != 2:
        raise ValueError("missing pinned 8.44 evidence inputs")
    args.work.mkdir(parents=True, exist_ok=True)
    for spec in specs:
        source = safe_path(args.data.resolve(), spec["path"])
        if args.download:
            materialize(spec, source, manifest.get("archives", {}), args.data.resolve())
        if not verify(source, spec):
            raise ValueError(f"missing or changed evidence: {source}")
        destination = args.work / source.name
        if source.resolve() == destination.resolve():
            raise ValueError("work directory must differ from evidence directory")
        shutil.copyfile(source, destination)
    model = args.work / "Philippe_Boineau_CCF_BE_AMCR_2019.db1.bak"
    export = args.work / "PB_AUVENT.ifc"
    original_model, original_export = model.read_bytes(), export.read_bytes()
    raw = gzip.decompress(original_model)
    # Independently locate the known column and its definition in fixed rows.
    sections = []
    position = 0
    while True:
        position = raw.find(b"\x66\xc0\xce\xdb", position)
        if position < 0:
            break
        width, fields = struct.unpack_from("<II", raw, position + 4)
        cursor = position + 12 + fields * 4
        start = cursor
        while cursor < len(raw) and raw[cursor] in (4, 12):
            cursor += width + 9
        sections.append((width, start, cursor))
        position = cursor
    if len(sections) != 286 or sections[273][0] != 64 or sections[245][0] != 322:
        raise ValueError("independent 8.44 row layout changed")

    def record(ordinal, object_id):
        width, start, end = sections[ordinal]
        found = [p for p in range(start, end, width + 9) if struct.unpack_from("<I", raw, p + 1)[0] == object_id]
        if len(found) != 1:
            raise ValueError("missing/ambiguous fixed 8.44 evidence record")
        return found[0] + 1

    part = record(273, 63072593)
    definition = record(245, struct.unpack_from("<I", raw, part + 4)[0])
    if raw[definition + 76:definition + 80] != b"IPE\0" or raw[definition + 144:definition + 147] != b"PT\0":
        raise ValueError("independent profile/prefix distinction changed")

    def run(passed):
        result = subprocess.run([str(args.exe.resolve()), "profile844_evidence", str(args.work.resolve())],
                                capture_output=True, encoding="utf8", timeout=60)
        if (result.returncode == 0) != passed:
            raise ValueError(f"8.44 evidence mutation failed: {result.stdout} {result.stderr}")

    run(True)
    try:
        modified = bytearray(raw)
        modified[definition + 76:definition + 140] = b"PT\0" + bytes(61)
        model.write_bytes(gzip.compress(modified, mtime=0))
        run(False)
        modified = bytearray(raw)
        struct.pack_into("<d", modified, part + 32, struct.unpack_from("<d", raw, part + 32)[0] + 100)
        model.write_bytes(gzip.compress(modified, mtime=0))
        run(False)
        model.write_bytes(original_model)
        if b"'IPE160'" not in original_export:
            raise ValueError("exported IPE160 evidence missing")
        export.write_bytes(original_export.replace(b"'IPE160'", b"'IPE999'"))
        run(False)
    finally:
        model.write_bytes(original_model)
        export.write_bytes(original_export)
    run(True)
    print("8.44 IFC evidence controls and 3/3 mutations passed")


if __name__ == "__main__":
    main()
