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
import sys
import uuid

from corpus import materialize, safe_path, verify

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_db1_950_evidence import db1_identity_rows


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
    model_references = tables[322][2]
    if (tables[260][0], len(tables[260][1]), tables[257][0], len(tables[257][1]),
            tables[256][0], len(tables[256][1]), tables[322][0], len(tables[322][1])) != (
                4740, 652, 144, 22, 1496, 205, 144, 20):
        raise ValueError("9.50 view/dimension/model-reference table signature changed")
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
    reference_ids, references = set(), []
    for offset, row in model_references:
        if len(row) != 144:
            raise ValueError("unexpected 9.50 type-322 record width")
        record_id, context = struct.unpack_from("<II", row)
        guid = row[8:24]
        if not record_id or record_id in reference_ids or not context or not any(guid):
            raise ValueError("invalid 9.50 model-reference ID/context/GUID")
        if context not in view_contexts:
            raise ValueError("9.50 type-322 context does not resolve to one view")
        reference_ids.add(record_id)
        references.append((record_id, context, str(uuid.UUID(bytes=guid))))
    return tables, {"views": len(views), "sets": len(sets), "dimensions": len(dimensions),
                    "subtype1": subtype_counts.get(1, 0), "unhandled": sum(v for k, v in subtype_counts.items() if k != 1),
                    "projected": projected, "empty_sets": sum(value == 0 for value in links.values())}, references


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
    db1_spec = next((item for item in manifest["files"]
                     if item["path"] == "abri-moto-vccf25/Abri-Moto-VCCF25/Abri-Moto-VCCF25.db1"), None)
    if db1_spec is None:
        raise ValueError("missing pinned Abri-Moto 9.50 DB1 identity evidence")
    db1_path = safe_path(args.data.resolve(), db1_spec["path"])
    if args.download:
        materialize(db1_spec, db1_path, manifest.get("archives", {}), args.data.resolve())
    if not verify(db1_path, db1_spec):
        raise ValueError(f"missing or changed pinned DB1 identity sample: {db1_path}")
    identities, part_ids, assembly_ids = db1_identity_rows(db1_path)
    identity_ids_by_guid = {}
    for model_id, (_, guid) in identities.items():
        identity_ids_by_guid.setdefault(guid, []).append(model_id)
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
        tables, expected, references = audit(raw)
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
        refs_result = subprocess.run([str(args.exe.resolve()), "drawing_model_references", str(target)],
                                     capture_output=True, text=True, encoding="utf8", timeout=90)
        if refs_result.returncode:
            raise ValueError(f"9.50 model-reference field read failed: {refs_result.stderr}")
        parsed_references = []
        for line in refs_result.stdout.splitlines():
            if line.startswith("reference="):
                record_id, context, guid = line.removeprefix("reference=").split(":", 2)
                parsed_references.append((int(record_id), int(context), guid.lower()))
        expected_references = [(record_id, context, guid.lower()) for record_id, context, guid in references]
        if parsed_references != expected_references:
            raise ValueError("9.50 decoded type-322 fields differ from independent raw walk")
        for _, _, guid in references:
            model_ids = identity_ids_by_guid.get(uuid.UUID(guid).bytes, [])
            if len(model_ids) != 1 or model_ids[0] not in part_ids or model_ids[0] in assembly_ids:
                raise ValueError("9.50 type-322 GUID does not uniquely resolve to one DB1 part row")
        originals.append((target, raw, tables, expected, references))

    totals = {key: sum(item[3][key] for item in originals) for key in ("views", "sets", "dimensions", "subtype1", "unhandled", "projected", "empty_sets")}
    if totals != {"views": 19, "sets": 67, "dimensions": 136, "subtype1": 119, "unhandled": 17, "projected": 27, "empty_sets": 1}:
        raise ValueError(f"9.50 real drawing baselines changed: {totals}")

    source, raw, tables, expected, references = next(item for item in originals
                                                       if item[3]["dimensions"] and len(item[2][260][2]) >= 2)
    dimension_offset, _ = tables[256][2][0]
    first_view_offset, first_view = tables[260][2][0]
    second_view_offset, _ = tables[260][2][1]
    set_offset, _ = tables[257][2][0]

    def rejected(label, mutate, base_raw=None):
        damaged = bytearray(raw if base_raw is None else base_raw)
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
    ref_source, ref_raw, ref_tables, _, ref_values = next(item for item in originals if len(item[4]) >= 2)
    first_reference_offset, first_reference = ref_tables[322][2][0]
    second_reference_offset, _ = ref_tables[322][2][1]
    rejected("reference_zero_id", lambda data: struct.pack_into("<I", data, first_reference_offset, 0), ref_raw)
    rejected("reference_zero_context", lambda data: struct.pack_into("<I", data, first_reference_offset + 4, 0), ref_raw)
    rejected("reference_empty_guid", lambda data: data.__setitem__(slice(first_reference_offset + 8, first_reference_offset + 24), bytes(16)), ref_raw)
    rejected("reference_duplicate_id", lambda data: struct.pack_into("<I", data, second_reference_offset,
              struct.unpack_from("<I", data, first_reference_offset)[0]), ref_raw)
    total_references = sum(len(item[4]) for item in originals)
    if total_references != 5 or sum(bool(item[4]) for item in originals) != 3:
        raise ValueError("9.50 type-322 reference inventory changed")
    single_ref_source, single_ref_raw, single_ref_tables, _, single_refs = next(item for item in originals if len(item[4]) == 1)
    single_ref_offset, _ = single_ref_tables[322][2][0]
    non_object_guid = next(guid for model_id, (_, guid) in identities.items()
                           if model_id not in part_ids and model_id not in assembly_ids)
    unknown_guid = bytes([0xAA]) * 16
    if unknown_guid in identity_ids_by_guid:
        raise ValueError("unknown GUID mutation collides with a DB1 identity")
    project_mutations = (
        ("reference_orphan_context", lambda data: struct.pack_into("<I", data, single_ref_offset + 4, 0xFFFFFFFF)),
        ("reference_unknown_guid", lambda data: data.__setitem__(slice(single_ref_offset + 8, single_ref_offset + 24), unknown_guid)),
        ("reference_non_part_guid", lambda data: data.__setitem__(slice(single_ref_offset + 8, single_ref_offset + 24), non_object_guid)),
    )
    for label, mutate in project_mutations:
        project_root = args.work / "project_mutations" / label
        project_root.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(db1_path, project_root / db1_spec["path"].rsplit("/", 1)[1])
        damaged = bytearray(single_ref_raw)
        mutate(damaged)
        drawing_path = project_root / single_ref_source.name
        drawing_path.write_bytes(gzip.compress(bytes(damaged), mtime=0))
        result = subprocess.run([str(args.exe.resolve()), "project_drawing_reference_no_link", str(project_root)],
                                capture_output=True, text=True, encoding="utf8", timeout=120)
        if result.returncode or "model_references=1 direct_links=0" not in result.stdout:
            raise ValueError(f"unsafe 9.50 direct model-reference association for {label}: {result.stderr}{result.stdout}")
    print("version=9.50 drawings=9 views=19 dimensions=136 subtype1=119 dimension_sets=67 unhandled_subtype0=17 projected=27 model_references=5 uniquely_matched_parts=5 malformed_rejected=9 unsafe_links_rejected=3")


if __name__ == "__main__":
    main()
