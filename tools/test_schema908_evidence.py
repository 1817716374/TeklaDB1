#!/usr/bin/env python3
"""Check pinned 9.08 snapshots against official API count and paired catalog names.

The two snapshots are one project, not two independent models. Row and raw-byte
checks in the C++ validator are internal consistency, not independent geometry.
"""
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
    specs = [f for f in manifest["files"] if f["path"].startswith("api-exam908-")]
    if len(specs) != 9:
        raise ValueError("missing pinned 9.08 evidence inputs")
    for spec in specs:
        source = safe_path(args.data.resolve(), spec["path"])
        if args.download:
            materialize(spec, source, manifest.get("archives", {}), args.data.resolve())
        if not verify(source, spec):
            raise ValueError(f"missing or changed evidence: {source}")
        destination = safe_path(args.work.resolve(), spec["path"])
        if source.resolve() == destination.resolve():
            raise ValueError("work directory must differ from evidence directory")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)

    def run(directory, passed):
        result = subprocess.run([str(args.exe.resolve()), "schema908_evidence", str(directory.resolve())],
                                capture_output=True, encoding="utf8", timeout=90)
        if (result.returncode == 0) != passed:
            raise ValueError(f"9.08 evidence mutation failed: {result.stdout} {result.stderr}")

    run(args.work / "api-exam908-september", True)
    directory = args.work / "api-exam908-july"
    model = directory / "API_Developer_Exam_01.db1"
    source = directory / "Form1.cs"
    original, official = model.read_bytes(), source.read_bytes()
    decoded = gzip.decompress(original) if original[:2] == b"\x1f\x8b" else original
    tables = []
    cursor = 0
    while True:
        start = decoded.find(b"\x66\xc0\xce\xdb", cursor)
        if start < 0:
            break
        width, fields = struct.unpack_from("<II", decoded, start + 4)
        cursor = start + 12 + fields * 4
        rows = []
        while cursor < len(decoded) and decoded[cursor] in (4, 12):
            rows.append(cursor + 1)
            cursor += width + 9
        tables.append((width, rows))
    if len(tables) != 339 or tables[299][0] != 332 or tables[329][0] != 44:
        raise ValueError("9.08 independent row layout changed")
    word = lambda offset: struct.unpack_from("<I", decoded, offset)[0]
    classes = {word(row): (word(row + 4), row) for row in tables[329][1]}
    identities = {word(row): (word(row + 4), row) for row in tables[293][1]}
    definitions = {word(row): row for row in tables[299][1]}
    beam = next(row for row in tables[274][1]
                if classes[identities[word(row)][0]][0] == 2 and word(definitions[word(row+4)]+8) % 10 == 0)
    wrong_class = next(key for key, (kind, _) in classes.items() if kind == 11)
    profile = next(row for row in definitions.values() if decoded[row+76:row+80] == b"IPE\0")
    run(directory, True)
    try:
        # A valid class reference to an operative object must change the API count.
        modified = bytearray(decoded)
        struct.pack_into("<I", modified, identities[word(beam)][1]+4, wrong_class)
        model.write_bytes(gzip.compress(modified, mtime=0))
        run(directory, False)
        # A plausible but wrong family must fail the independent catalog match.
        modified = bytearray(decoded)
        modified[profile+76:profile+140] = b"WRONG\0" + bytes(58)
        model.write_bytes(gzip.compress(modified, mtime=0))
        run(directory, False)
        model.write_bytes(original)
        if official.count(b"== 4237") != 1:
            raise ValueError("official July API count changed")
        source.write_bytes(official.replace(b"== 4237", b"== 4238"))
        run(directory, False)
    finally:
        model.write_bytes(original)
        source.write_bytes(official)
    run(directory, True)
    print("9.08 July/September controls and 3/3 independent-evidence mutations passed")


if __name__ == "__main__":
    main()
