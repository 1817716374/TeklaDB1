#!/usr/bin/env python3
"""Check the pinned GUID history and reject valid-looking mapping mutations."""
import argparse
import gzip
import json
from pathlib import Path
import re
import shutil
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
    specs = [f for f in manifest["files"] if f["path"].startswith("guid-mapping-auvent/")]
    if len(specs) != 3:
        raise ValueError("missing pinned GUID mapping evidence inputs")
    args.work.mkdir(parents=True, exist_ok=True)
    for spec in specs:
        source = safe_path(args.data.resolve(), spec["path"])
        if args.download:
            materialize(spec, source, manifest.get("archives", {}), args.data.resolve())
        if not verify(source, spec):
            raise ValueError(f"missing or changed evidence: {source}")
        destination = args.work / source.name
        if source.resolve() == destination.resolve():
            raise ValueError("mutation work directory must differ from evidence directory")
        shutil.copyfile(source, destination)
    path = args.work / "guid.mapper"
    original = path.read_bytes()
    raw = gzip.decompress(original)
    lines = raw.splitlines()
    headers = [i for i, line in enumerate(lines) if line.startswith(b"!Guid mapping for ")]
    counts = [b - a - 1 for a, b in zip(headers, headers[1:] + [len(lines)])]
    if counts != [33899, 3112, 1688, 1007, 1003, 1003]:
        raise ValueError("independent literal batch counts changed")
    syntax = re.compile(rb"ID[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12} ID[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}")
    if any(not line.startswith(b"!Guid mapping for ") and not syntax.fullmatch(line) for line in lines):
        raise ValueError("independent GUID pair grammar mismatch")

    def run(passed):
        result = subprocess.run([str(args.exe.resolve()), "guid_mapping_evidence", str(args.work.resolve())],
                                capture_output=True, encoding="utf8", timeout=60)
        if (result.returncode == 0) != passed:
            raise ValueError(f"GUID evidence mutation check failed: {result.stdout} {result.stderr}")

    run(True)
    # Known real pair, independently read in the two raw identity tables.
    pair = b"ID7998C610-11CA-4333-8D41-1FC4375B943E ID9DB7F2EB-37A0-4656-B18A-DD7240715047"
    if raw.count(pair) != 1:
        raise ValueError("fixed cross-snapshot identity pair missing")
    left, right = pair.split()
    mutations = [raw.replace(pair, right + b" " + left),
                 raw.replace(pair, left + b" ID00000000-0000-0000-0000-000000000000"),
                 b"\n".join(lines[:headers[-1]]) + b"\n"]
    try:
        for modified in mutations:
            path.write_bytes(gzip.compress(modified, mtime=0))
            run(False)
    finally:
        path.write_bytes(original)
    run(True)
    print("GUID mapping evidence controls and 3/3 mutations passed")


if __name__ == "__main__":
    main()
