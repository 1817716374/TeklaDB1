#!/usr/bin/env python3
"""Check historical DG text/identity against independent log and GUID mapping."""
import argparse
import gzip
import json
import math
from pathlib import Path
import shutil
import struct
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

    # Independently walk the 8.44 table framing and inspect the stored view
    # coordinates/ranges. The wide view payload is not searched for magic.
    pos, physical = 76, []
    while pos < len(raw):
        if struct.unpack_from("<I", raw, pos)[0] != 0xdbcec066:
            raise ValueError("invalid 8.44 table signature in independent view audit")
        width, count = struct.unpack_from("<II", raw, pos + 4)
        descriptors = struct.unpack_from("<" + str(count) + "I", raw, pos + 12)
        pos += 12 + 4 * count
        rows = []
        while True:
            tag = raw[pos]
            pos += 1
            if tag == 0:
                break
            if tag != 4:
                raise ValueError("unexpected allocation tag in independent view audit")
            rows.append(raw[pos:pos + width])
            pos += width + 24 + 16 * sum(value == 1 for value in descriptors)
        physical.append((width, descriptors, rows))
    if pos != len(raw) or len(physical) != 47:
        raise ValueError("8.44 table walk did not cover the drawing")
    directory_types = [struct.unpack_from("<I", row)[0] for row in physical[0][2]]
    type_tables = dict(zip(reversed(directory_types), physical[1:]))
    views = type_tables[260][2]
    if len(views) != 1:
        raise ValueError("8.44 view count changed")
    view = views[0]
    if struct.unpack_from("<III", view) != (260, 1182, 11):
        raise ValueError("8.44 view identity changed")

    def coordinates(offset):
        points = [struct.unpack_from("<3d", view, offset + point * 24) for point in range(3)]
        axes = [tuple(points[axis + 1][i] - points[0][i] for i in range(3)) for axis in range(2)]
        lengths = [math.sqrt(sum(value * value for value in axis)) for axis in axes]
        dot = sum(axes[0][i] * axes[1][i] for i in range(3))
        if any(not math.isfinite(value) for point in points for value in point):
            raise ValueError("non-finite 8.44 view coordinate")
        if any(abs(length - 1) > 1e-6 for length in lengths) or abs(dot) > 1e-6:
            raise ValueError("8.44 view basis is not orthonormal")

    coordinates(40)
    coordinates(160)
    for offset in (112, 232):
        bounds = struct.unpack_from("<6d", view, offset)
        if any(not math.isfinite(value) for value in bounds) or bounds[0] > bounds[1] or bounds[2] > bounds[3]:
            raise ValueError("invalid stored 8.44 view range")
    set_rows = type_tables[257][2]
    dimension_rows = type_tables[256][2]
    sets = {struct.unpack_from("<I", row, 4)[0]: struct.unpack_from("<I", row, 8)[0] for row in set_rows}
    if len(sets) != 5 or len(set_rows) != 5 or len(dimension_rows) != 13:
        raise ValueError("8.44 dimension/set counts changed")
    identities = set()
    links = {key: 0 for key in sets}
    for row in dimension_rows:
        kind, record_id, context, set_id, subtype = struct.unpack_from("<5I", row)
        if kind != 256 or not record_id or record_id in identities or subtype != 1:
            raise ValueError("unexpected or duplicate 8.44 dimension identity/subtype")
        if set_id not in sets or sets[set_id] != context or context != 11:
            raise ValueError("8.44 dimension/set context association mismatch")
        identities.add(record_id)
        links[set_id] += 1
        for offset in (24, 48, 168):
            if any(not math.isfinite(value) for value in struct.unpack_from("<3d", row, offset)):
                raise ValueError("non-finite 8.44 dimension coordinate")
        if not math.isfinite(struct.unpack_from("<d", row, 192)[0]):
            raise ValueError("non-finite 8.44 dimension offset")
    if sum(links.values()) != 13 or any(count == 0 for count in links.values()):
        raise ValueError("8.44 dimensions do not link to every stored set")
    parsed_view = subprocess.run([str(args.exe.resolve()), "drawing", safe_path(args.work.resolve(), dg)],
                                 capture_output=True, encoding="utf8", timeout=60)
    if parsed_view.returncode or "views=1" not in parsed_view.stdout:
        raise ValueError(f"8.44 semantic view result mismatch: {parsed_view.stdout} {parsed_view.stderr}")
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
    print("DG 8.44 view coordinates/ranges independently validated; history controls and 4/4 mutations passed")


if __name__ == "__main__":
    main()
