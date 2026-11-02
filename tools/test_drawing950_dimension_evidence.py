#!/usr/bin/env python3
"""Verify 9.50 dimension/context decoding against pinned raw DG rows."""
import argparse
import gzip
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess

from corpus import materialize, safe_path, verify


def walk_950(data):
    if data[:12] != b"Xsteel# 9.50" or len(data) < 76:
        raise ValueError("unexpected 9.50 drawing header")
    pos, tables = 76, []
    while pos < len(data):
        if len(data) - pos < 12 or struct.unpack_from("<I", data, pos)[0] != 0xDBCEC066:
            raise ValueError("bad 9.50 section boundary")
        width, fields = struct.unpack_from("<II", data, pos + 4)
        pos += 12
        descriptors = struct.unpack_from("<" + str(fields) + "I", data, pos)
        pos += fields * 4
        stride = width + 1 + 24 + 16 * sum(field == 1 for field in descriptors)
        rows = []
        while pos < len(data) and data[pos] in (4, 12):
            rows.append((pos + 1, data[pos + 1:pos + width + 1]))
            pos += stride
        if pos >= len(data) or data[pos] != 0:
            raise ValueError("missing 9.50 section terminator")
        pos += 1
        tables.append((width, descriptors, rows))
    if pos != len(data) or len(tables) != 48:
        raise ValueError("9.50 section count or end boundary changed")
    directory = [struct.unpack_from("<I", row)[0] for _, _, rows in (tables[0],) for _, row in rows]
    if len(directory) != 47 or len(set(directory)) != 47:
        raise ValueError("9.50 table directory changed")
    forward = directory[0] < directory[-1]
    by_type = {directory[i] if forward else directory[len(directory) - 1 - i]: table
               for i, table in enumerate(tables[1:])}
    return by_type


def audit(raw):
    tables = walk_950(raw)
    views = tables[260][2]
    sets = tables[257][2]
    dimensions = tables[256][2]
    if (tables[260][0], len(tables[260][1]), tables[257][0], len(tables[257][1]),
            tables[256][0], len(tables[256][1])) != (4740, 652, 144, 22, 1496, 205):
        raise ValueError("9.50 view/dimension table signature changed")
    view_contexts = {}
    for offset, row in views:
        kind, record_id, context = struct.unpack_from("<III", row)
        if kind != 260 or not record_id or not context or context in view_contexts:
            raise ValueError("duplicate or invalid 9.50 view prefix")
        view_contexts[context] = record_id
    set_contexts = {}
    for offset, row in sets:
        kind, record_id, context = struct.unpack_from("<III", row)
        if kind != 257 or not record_id or not context or context not in view_contexts or record_id in set_contexts:
            raise ValueError("invalid 9.50 dimension set/context association")
        set_contexts[record_id] = context
    ids, subtype_counts, projected = set(), {}, 0
    links = {set_id: 0 for set_id in set_contexts}
    for offset, row in dimensions:
        kind, record_id, context, set_id, subtype = struct.unpack_from("<5I", row)
        if kind != 256 or not record_id or record_id in ids or context not in view_contexts:
            raise ValueError("invalid 9.50 dimension identity/context")
        if set_id not in set_contexts or set_contexts[set_id] != context:
            raise ValueError("9.50 dimension set/context does not close")
        ids.add(record_id)
        links[set_id] += 1
        subtype_counts[subtype] = subtype_counts.get(subtype, 0) + 1
        if subtype == 1:
            points = [struct.unpack_from("<3d", row, position) for position in (24, 48, 168)]
            distance = struct.unpack_from("<d", row, 192)[0]
            if any(not math.isfinite(value) for vector in points for value in vector) or not math.isfinite(distance):
                raise ValueError("non-finite 9.50 straight dimension value")
            start, end, up = points
            if abs(math.hypot(up[0], up[1]) - 1) <= 1e-8 and abs(up[2]) <= 1e-8 and abs(end[2] - start[2]) <= 1e-8:
                projected += 1
    return tables, {"views": len(views), "sets": len(sets), "dimensions": len(dimensions),
                    "subtype1": subtype_counts.get(1, 0), "unhandled": sum(v for k, v in subtype_counts.items() if k != 1),
                    "projected": projected, "empty_sets": sum(value == 0 for value in links.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / "tests/corpus.json").read_text(encoding="utf8"))
    specs = [item for item in manifest["files"] if item["path"].startswith("abri-moto-vccf25/Abri-Moto-VCCF25/drawings/")
             and item["path"].endswith(".dg")]
    if len(specs) != 9:
        raise ValueError("expected the nine pinned Abri-Moto 9.50 drawings")
    originals = []
    for spec in specs:
        source = safe_path(args.data.resolve(), spec["path"])
        if args.download:
            materialize(spec, source, manifest.get("archives", {}), args.data.resolve())
        if not verify(source, spec):
            raise ValueError(f"missing or changed pinned drawing: {source}")
        target = safe_path(args.work.resolve(), spec["path"])
        if source.resolve() == target.resolve():
            raise ValueError("mutation work directory must differ from pinned samples")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        compressed = target.read_bytes()
        raw = gzip.decompress(compressed)
        tables, expected = audit(raw)
        result = subprocess.run([str(args.exe.resolve()), "drawing", str(target)], capture_output=True,
                                text=True, encoding="utf8", timeout=90)
        if result.returncode:
            raise ValueError(f"9.50 drawing parse failed: {result.stderr}")
        actual = dict(part.split("=", 1) for part in result.stdout.strip().split() if "=" in part)
        if int(actual.get("views", -1)) != len({struct.unpack_from("<I", row, 8)[0] for _, row in tables[260][2]}) or \
                int(actual.get("dimension_sets", -1)) != expected["sets"] or \
                int(actual.get("dimensions", -1)) != expected["subtype1"] or \
                int(actual.get("unhandled_dimensions", -1)) != expected["unhandled"]:
            raise ValueError("9.50 decoded view/dimension counts differ from independent raw walk")
        originals.append((target, raw, tables, expected))

    totals = {key: sum(item[3][key] for item in originals) for key in ("views", "sets", "dimensions", "subtype1", "unhandled", "projected", "empty_sets")}
    if totals != {"views": 19, "sets": 67, "dimensions": 136, "subtype1": 119, "unhandled": 17, "projected": 27, "empty_sets": 1}:
        raise ValueError(f"9.50 real drawing baselines changed: {totals}")

    source, raw, tables, expected = next(item for item in originals
                                         if item[3]["dimensions"] and len(item[2][260][2]) >= 2)
    dimension_offset, _ = tables[256][2][0]
    first_view_offset, first_view = tables[260][2][0]
    second_view_offset, _ = tables[260][2][1]
    set_offset, _ = tables[257][2][0]

    def rejected(label, mutate):
        damaged = bytearray(raw)
        mutate(damaged)
        path = args.work / "mutations" / f"{label}.dg"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(gzip.compress(bytes(damaged), mtime=0))
        result = subprocess.run([str(args.exe.resolve()), "drawing", str(path)], capture_output=True,
                                text=True, encoding="utf8", timeout=90)
        if result.returncode == 0:
            raise ValueError(f"malformed 9.50 dimension mutation was accepted: {label}")

    rejected("dimension_nan", lambda data: struct.pack_into("<d", data, dimension_offset + 24, float("nan")))
    rejected("dimension_zero_id", lambda data: struct.pack_into("<I", data, dimension_offset + 4, 0))
    rejected("dimension_zero_context", lambda data: struct.pack_into("<I", data, dimension_offset + 8, 0))
    rejected("set_zero_context", lambda data: struct.pack_into("<I", data, set_offset + 8, 0))
    rejected("duplicate_view_id", lambda data: struct.pack_into("<I", data, second_view_offset + 4,
              struct.unpack_from("<I", data, first_view_offset + 4)[0]))
    print("version=9.50 drawings=9 views=19 dimensions=136 subtype1=119 dimension_sets=67 unhandled_subtype0=17 projected=27 malformed_rejected=5")


if __name__ == "__main__":
    main()
