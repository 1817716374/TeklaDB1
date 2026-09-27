"""Verify that the fixed DWG geometry oracle rejects changed DG fields."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import corpus


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / "tests/corpus.json").read_text(encoding="utf8"))
    specs = {f["path"]: f for f in manifest["files"]}
    names = ["btscm-dg730/D0000516925.dg", "btscm-dg730/pechelirejulV1.db1",
             "legacy-drawing-output-evidence/ensemble passerelle.dwg"]
    for name in names:
        if not corpus.verify(args.data / name, specs[name]):
            raise ValueError(f"unexpected evidence input: {name}; download the pinned corpus first")
    original = gzip.decompress((args.data / names[0]).read_bytes())
    assert original[:12] == b"Xsteel  7.30"
    count, width = struct.unpack_from("<II", original, 12)
    assert width == 4
    types = [struct.unpack_from("<I", original, 21 + i * 5)[0] for i in range(count)]
    pos = 20 + count * 5
    records = {}
    for kind in reversed(types):
        count, width = struct.unpack_from("<II", original, pos)
        pos += 8
        for _ in range(count):
            if kind in (256, 260):
                record = struct.unpack_from("<I", original, pos + 5)[0]
                records[kind, record] = pos + 1
            pos += 1 + width
    assert pos == len(original)
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="drawing730-oracle-", dir=work) as temporary:
        root = Path(temporary).resolve()
        if not root.is_relative_to(work):
            raise ValueError("temporary directory escaped work path")
        for name in names:
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.data / name, destination)

        def run(data, expected=None):
            (root / names[0]).write_bytes(gzip.compress(data, mtime=0))
            result = subprocess.run([str(args.exe.resolve()), "drawing730_dimension_evidence",
                                     str(root / "legacy-drawing-output-evidence")],
                                    capture_output=True, encoding="utf8", errors="replace", timeout=120)
            if expected is None:
                if result.returncode != 0 or "endpoints=10" not in result.stdout:
                    raise ValueError(f"unmodified control failed: {result.stdout} {result.stderr}")
            elif result.returncode != 1 or expected not in result.stderr:
                raise ValueError(f"oracle missed {expected}: {result.stdout} {result.stderr}")

        run(original)
        cases = [(256, 284777, 24, 1.0, "line length"),
                 (256, 284777, 192, 0.1, "endpoints"),
                 (256, 284777, 176, -2.0, "endpoints"),
                 (260, 287646, 344, 1.0, "endpoints")]
        for kind, record, offset, change, error in cases:
            data = bytearray(original)
            at = records[kind, record] + offset
            struct.pack_into("<d", data, at, struct.unpack_from("<d", data, at)[0] + change)
            run(data, error)
            print(f"PASS rejects changed type {kind} record {record} field {offset}")
        run(original)
    print("4/4 drawing evidence mutations rejected; unmodified controls passed")


if __name__ == "__main__":
    main()
