#!/usr/bin/env python3
"""Check 8.74 identity-class kinds against typed association endpoints."""
from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_schema


SAMPLE = Path(
    "devsachinborse__tekla-structures-projects"
) / "weirwelding.16" / "weirwelding.16.db1"
EXPECTED_RELATIONS = {4: 204, 9: 1217, 11: 2680, 12: 121, 13: 1224, 38: 228}
EXPECTED_PART_KINDS = {2: 6046, 11: 2680, 38: 228}


def table_rows(data: bytes, database: analyze_schema.Database, ordinal: int) -> list[bytes]:
    table = database.tables[ordinal]
    offsets = analyze_schema.section_offsets(data)
    if not table.valid:
        raise ValueError(f"table {ordinal} is malformed")
    start = offsets[ordinal] + 12 + 4 * len(table.fields)
    stride = table.payload_size + 9
    return [data[start + i * stride + 1:start + i * stride + 1 + table.payload_size]
            for i in range(table.row_count)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True,
                        help="external corpus root used by tools/corpus.py")
    args = parser.parse_args()
    path = (args.corpus / SAMPLE).resolve()
    if not path.is_file():
        raise ValueError(f"missing pinned sample: {path}")
    data = analyze_schema.read_payload(path)
    database = analyze_schema.parse_database(path)
    if database.storage_version != "8.74" or len(database.tables) != 307:
        raise ValueError("unexpected 8.74 main DB1 layout")

    class_table = database.tables[114]
    if (class_table.payload_size, class_table.fields) != (32, (1, 1, 0, 0, 0, 0, 0, 1, 1)):
        raise ValueError("8.74 identity-class signature changed")
    classes = {}
    for row in table_rows(data, database, 114):
        identity_class, kind = struct.unpack_from("<II", row, 0)
        if identity_class in classes:
            raise ValueError(f"duplicate identity-class ID {identity_class}")
        classes[identity_class] = kind

    identity_table = database.tables[293]
    if (identity_table.payload_size, identity_table.fields) != (55, (1, 1, 1, 1, 1, 0)):
        raise ValueError("8.74 identity signature changed")
    identities = {}
    for row in table_rows(data, database, 293):
        identity, class_reference = struct.unpack_from("<II", row, 0)
        if identity in identities:
            raise ValueError(f"duplicate identity ID {identity}")
        if class_reference not in classes:
            raise ValueError(f"identity {identity} has missing class {class_reference}")
        identities[identity] = classes[class_reference]

    part_kinds = Counter()
    for row in table_rows(data, database, 274):
        part_id = struct.unpack_from("<I", row, 0)[0]
        if part_id not in identities:
            raise ValueError(f"part {part_id} has no identity")
        part_kinds[identities[part_id]] += 1
    if dict(part_kinds) != EXPECTED_PART_KINDS:
        raise ValueError(f"part class counts changed: {dict(part_kinds)}")

    relation_counts = Counter()
    for ordinal in (191, 192):
        for row in table_rows(data, database, ordinal):
            relation_type, source, target = struct.unpack_from("<III", row, 4)
            if relation_type not in EXPECTED_RELATIONS:
                continue
            typed_identity = source if relation_type in (4, 13) else target
            if typed_identity not in identities:
                raise ValueError(f"typed relation {relation_type} has no identity endpoint {typed_identity}")
            if identities[typed_identity] != relation_type:
                raise ValueError(
                    f"relation type {relation_type} conflicts with identity class "
                    f"{identities[typed_identity]} for object {typed_identity}"
                )
            relation_counts[relation_type] += 1
    if dict(relation_counts) != EXPECTED_RELATIONS:
        raise ValueError(f"typed relation counts changed: {dict(relation_counts)}")

    print(f"version=8.74 classes={len(classes)} identities={len(identities)} "
          f"parts={dict(sorted(part_kinds.items()))} "
          f"typed_relation_endpoints={dict(sorted(relation_counts.items()))}")
    print("All checked links are internal cross-table consistency evidence, not an independent Tekla export.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, struct.error) as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
