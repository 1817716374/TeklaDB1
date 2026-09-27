#!/usr/bin/env python3
"""Check historical DG text/identity against independent log and GUID mapping."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import subprocess
from corpus import materialize, safe_path, verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / "tests/corpus.json").read_text(encoding="utf8"))
    dg = "auvent844-drawing/DID54A03D14-3328-47D1-B298-9B37193B88A2.dg"
    log = "auvent844-drawing/drawing_history.log"
    mapper = "guid-mapping-auvent/guid.mapper"
    names = [dg, log, mapper]
    specs = {f["path"]: f for f in manifest["files"]}
    originals = {}
    for name in names:
        source = safe_path(args.data.resolve(), name)
        if args.download:
            materialize(specs[name], source, manifest.get("archives", {}), args.data.resolve())
        if not verify(source, specs[name]):
            raise ValueError(f"missing or changed evidence: {source}")
        dest = safe_path(args.work.resolve(), name)
        if source.resolve() == dest.resolve():
            raise ValueError("mutation work must differ from source")
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        originals[name] = dest.read_bytes()

    def run(passed):
        result = subprocess.run([str(args.exe.resolve()), "drawing844_evidence", str(args.work.resolve() / "auvent844-drawing")],
                                capture_output=True, encoding="utf8", timeout=60)
        if (result.returncode == 0) != passed:
            raise ValueError(f"DG evidence mutation check failed: {result.stdout} {result.stderr}")

    raw = gzip.decompress(originals[dg])
    history = originals[log]
    mapping = gzip.decompress(originals[mapper])
    project = b"ID152F6ED8-4DAA-4276-B493-0AEA86E0BD65"
    pair = project + b" ID2BD3F71C-E5E6-49EE-A2C5-7E870B087658"
    if raw.count(b"S14") != 1 or raw.count(project) != 1 or mapping.count(pair) != 1 or b"[S.14]" not in history:
        raise ValueError("fixed independent evidence locations changed")
    changes = [(dg, gzip.compress(raw.replace(b"S14", b"S99"), mtime=0)),
               (dg, gzip.compress(raw.replace(project, project[:-1] + b"6"), mtime=0)),
               (log, history.replace(b"[S.14]", b"[S.99]")),
               (mapper, gzip.compress(mapping.replace(pair, pair.replace(project, project[:-1] + b"6")), mtime=0))]
    run(True)
    try:
        for name, data in changes:
            dest = safe_path(args.work.resolve(), name)
            dest.write_bytes(data)
            run(False)
            dest.write_bytes(originals[name])
    finally:
        for name, data in originals.items():
            safe_path(args.work.resolve(), name).write_bytes(data)
    run(True)
    print("DG 8.44 history evidence controls and 4/4 mutations passed")


if __name__ == "__main__":
    main()
