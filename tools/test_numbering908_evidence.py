#!/usr/bin/env python3
"""Check 9.08 DB1/DB2 against pinned official numbering history and mutations."""
import argparse
import gzip
import json
from pathlib import Path
import re
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
    manifest = json.loads((Path(__file__).resolve().parents[1]/"tests/corpus.json").read_text(encoding="utf8"))
    specs = [f for f in manifest["files"] if f["path"].startswith("api-exam908-")]
    if len(specs) != 14:
        raise ValueError("missing pinned 9.08 numbering inputs")
    for spec in specs:
        source = safe_path(args.data.resolve(), spec["path"])
        if args.download:
            materialize(spec, source, manifest.get("archives", {}), args.data.resolve())
        if not verify(source, spec):
            raise ValueError(f"missing or changed evidence: {source}")
        destination = safe_path(args.work.resolve(), spec["path"])
        if destination.resolve() == source.resolve():
            raise ValueError("work directory must differ from pinned data")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    directory = args.work/"api-exam908-july"

    def run(passed=True, mode="numbering908_evidence", folder=directory):
        result = subprocess.run([str(args.exe.resolve()), mode, str(folder.resolve())],
                                capture_output=True, encoding="utf8", timeout=90)
        if (result.returncode == 0) != passed:
            raise ValueError(f"9.08 numbering evidence mutation failed: {result.stdout} {result.stderr}")

    run()
    run(folder=args.work/"api-exam908-september")
    model = directory/"API_Developer_Exam_01.db1"
    numbering = directory/"API_Developer_Exam_01.db2"
    history = directory/"numberinghistory.txt"
    original_model, original_numbering, original_history = model.read_bytes(), numbering.read_bytes(), history.read_bytes()
    def decode(data):
        return gzip.decompress(data) if data[:2] == b"\x1f\x8b" else data
    raw_model, raw_numbering = decode(original_model), decode(original_numbering)
    # Independent sequential DB2 walk; locate the non-1 series without parser output.
    cursor = 49
    series = None
    while cursor < len(raw_numbering):
        table, count, width = struct.unpack_from("<III", raw_numbering, cursor)
        cursor += 12
        for _ in range(count):
            if table == 22 and width == 80 and raw_numbering[cursor+1:cursor+7] == b"/1001\0":
                series = cursor+1
            cursor += 1+width
        if raw_numbering[cursor:cursor+4] != b"Oa\xbc\0":
            raise ValueError("DB2 trailer changed")
        cursor += 4
    if series is None:
        raise ValueError("non-1 series missing")
    tables = []
    cursor = 0
    while True:
        start = raw_model.find(b"\x66\xc0\xce\xdb", cursor)
        if start < 0:
            break
        width, fields = struct.unpack_from("<II", raw_model, start+4)
        cursor = start+12+fields*4
        rows = []
        while cursor < len(raw_model) and raw_model[cursor] in (4, 12):
            rows.append(cursor+1)
            cursor += width+9
        tables.append((width, rows))
    if len(tables) != 339 or tables[211][0] != 12 or tables[322][0] != 68:
        raise ValueError("DB1 numbering layout changed")
    word = lambda offset: struct.unpack_from("<I", raw_model, offset)[0]
    observed = re.search(rb"Part\s+guid:\s+([\w-]+)\s+series:/1001\s+.*? -> /(\d+)", original_history)
    if observed is None:
        raise ValueError("non-1 historical assignment missing")
    identity = next(row for row in tables[293][1] if raw_model[row+16:row+52] == observed[1])
    reference = next(row for row in tables[211][1] if word(row) == word(identity))
    record = next(row for row in tables[322][1] if word(row) == word(reference+8))
    alternative = next(row for row in tables[322][1]
                       if word(row+8) == 1001 and word(row+12) > 0 and word(row+12) != word(record+12))
    try:
        changed = bytearray(raw_numbering)
        struct.pack_into("<I", changed, series+52, struct.unpack_from("<I", changed, series+52)[0]+1)
        numbering.write_bytes(gzip.compress(changed, mtime=0));run(False)
        changed = bytearray(raw_numbering);changed[series:series+5] = b"/1002"
        numbering.write_bytes(gzip.compress(changed, mtime=0));run(False)
        numbering.write_bytes(original_numbering)
        changed = bytearray(raw_model)
        struct.pack_into("<I", changed, record+12, word(record+12)+1)
        model.write_bytes(gzip.compress(changed, mtime=0));run(False)
        changed = bytearray(raw_model)
        struct.pack_into("<I", changed, reference+8, word(alternative))
        model.write_bytes(gzip.compress(changed, mtime=0));run(False)
        model.write_bytes(original_model)
        history.write_bytes(original_history[:observed.start(2)] + str(int(observed[2])+1).encode() + original_history[observed.end(2):])
        run(False);history.write_bytes(original_history)
        for offset, replacement in [(13, b"0" if raw_numbering[13:14] != b"0" else b"1"), (8, b"9.60")]:
            changed = bytearray(raw_numbering);changed[offset:offset+len(replacement)] = replacement
            numbering.write_bytes(gzip.compress(changed, mtime=0))
            run(False)
            run(mode="numbering908_unpaired")
    finally:
        model.write_bytes(original_model)
        numbering.write_bytes(original_numbering)
        history.write_bytes(original_history)
    run()
    print("9.08 numbering snapshots and 7/7 mutations passed; wrong GUID/version retain objects without pairing")


if __name__ == "__main__":
    main()
