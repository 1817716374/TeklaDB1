#!/usr/bin/env python3
"""Compare modern Xsteel DB1 table layouts without interpreting model data."""

from __future__ import annotations

import argparse
import gzip
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SECTION_MAGIC = b"\x66\xc0\xce\xdb"

REFERENCE_ROLES = {
    "point": 61,
    "placement": 64,
    "frame": 65,
    "profile_dimension_string": 75,
    "numeric_property": 122,
    "component": 154,
    "property_link_a": 160,
    "property_link_b": 161,
    "weld_instance": 190,
    "association_a": 191,
    "association_b": 192,
    "weld_definition": 228,
    "contour_link": 270,
    "part_instance": 274,
    "assembly_relation": 294,
    "assembly": 300,
    "bolt_group": 310,
    "contour_block": 328,
    "bolt_layer": 332,
    "string_property": 340,
    "part_definition": 341,
    "bolt_definition": 351,
    "identity": 355,
}


@dataclass(frozen=True)
class Table:
    ordinal: int
    payload_size: int
    fields: tuple[int, ...]
    row_count: int
    valid: bool

    @property
    def signature(self) -> tuple[int, tuple[int, ...]]:
        return self.payload_size, self.fields


@dataclass(frozen=True)
class Database:
    path: Path
    storage_version: str
    tables: tuple[Table, ...]


def read_payload(path: Path) -> bytes:
    data = path.read_bytes()
    return gzip.decompress(data) if data.startswith(b"\x1f\x8b") else data


def storage_version(data: bytes) -> str:
    header = data[:160].decode("latin-1", errors="replace")
    match = re.search(r"Xsteel[^0-9]*([0-9]+(?:\.[0-9]+)?)", header)
    return match.group(1) if match else "unknown"


def section_offsets(data: bytes) -> list[int]:
    result: list[int] = []
    start = 0
    while True:
        offset = data.find(SECTION_MAGIC, start)
        if offset < 0:
            return result
        result.append(offset)
        start = offset + len(SECTION_MAGIC)
        if offset + 12 <= len(data):
            payload, fields = struct.unpack_from("<II", data, offset + 4)
            if fields <= (len(data) - offset - 12) // 4:
                start = offset + 12 + fields * 4
                stride = payload + 9
                while start < len(data) and data[start] in (4, 12):
                    if stride > len(data) - start:
                        break
                    start += stride


def parse_database(path: Path) -> Database:
    data = read_payload(path)
    if not data.startswith(b"Xsteel"):
        raise ValueError(f"{path} is not an Xsteel database")
    offsets = section_offsets(data)
    if not offsets:
        raise ValueError(f"{path} uses the legacy layout without modern sections")

    tables: list[Table] = []
    for ordinal, offset in enumerate(offsets):
        next_offset = offsets[ordinal + 1] if ordinal + 1 < len(offsets) else len(data)
        if offset + 12 > next_offset:
            tables.append(Table(ordinal, 0, (), 0, False))
            continue
        payload_size, field_count = struct.unpack_from("<II", data, offset + 4)
        header_size = 12 + field_count * 4
        if offset + header_size > next_offset:
            tables.append(Table(ordinal, payload_size, (), 0, False))
            continue
        fields = struct.unpack_from(f"<{field_count}I", data, offset + 12) if field_count else ()
        stride = payload_size + 9
        row_count, valid = 0, False
        for trailer_size in ((5,) if ordinal + 1 == len(offsets) else (9, 1)):
            body_size = next_offset - offset - header_size - trailer_size
            if body_size < 0 or body_size % stride or data[next_offset - trailer_size] != 0:
                continue
            if any(data[cursor] not in (4, 12) for cursor in range(offset + header_size, next_offset - trailer_size, stride)):
                continue
            row_count, valid = body_size // stride, True
            break
        tables.append(Table(ordinal, payload_size, tuple(fields), row_count, valid))
    return Database(path, storage_version(data), tuple(tables))


def match_score(left: Table, right: Table) -> int:
    if left.signature == right.signature:
        return 16
    if left.payload_size == right.payload_size and len(left.fields) == len(right.fields):
        return 7
    if left.payload_size == right.payload_size:
        return 4
    if len(left.fields) == len(right.fields):
        return 1
    return -8


def align(reference: tuple[Table, ...], target: tuple[Table, ...]) -> dict[int, int]:
    """Global sequence alignment; table order is stable across known modern DB1 versions."""

    gap = -4
    rows = len(reference) + 1
    columns = len(target) + 1
    scores = [[0] * columns for _ in range(rows)]
    moves = [[0] * columns for _ in range(rows)]  # 1 diagonal, 2 up, 3 left
    for row in range(1, rows):
        scores[row][0] = row * gap
        moves[row][0] = 2
    for column in range(1, columns):
        scores[0][column] = column * gap
        moves[0][column] = 3

    for row in range(1, rows):
        for column in range(1, columns):
            diagonal = scores[row - 1][column - 1] + match_score(reference[row - 1], target[column - 1])
            up = scores[row - 1][column] + gap
            left = scores[row][column - 1] + gap
            best = max(diagonal, up, left)
            scores[row][column] = best
            moves[row][column] = 1 if diagonal == best else (2 if up == best else 3)

    mapping: dict[int, int] = {}
    row = len(reference)
    column = len(target)
    while row or column:
        move = moves[row][column]
        if move == 1:
            mapping[row - 1] = column - 1
            row -= 1
            column -= 1
        elif move == 2:
            row -= 1
        else:
            column -= 1
    return mapping


def describe(reference: Database, targets: Iterable[Database]) -> dict:
    result = {
        "reference": {
            "storage_version": reference.storage_version,
            "table_count": len(reference.tables),
        },
        "targets": [],
    }
    for target in targets:
        mapping = align(reference.tables, target.tables)
        roles = {}
        for name, reference_ordinal in REFERENCE_ROLES.items():
            target_ordinal = mapping.get(reference_ordinal)
            reference_table = reference.tables[reference_ordinal]
            target_table = target.tables[target_ordinal] if target_ordinal is not None else None
            roles[name] = {
                "reference_ordinal": reference_ordinal,
                "target_ordinal": target_ordinal,
                "exact_signature": bool(target_table and target_table.signature == reference_table.signature),
                "reference_payload": reference_table.payload_size,
                "target_payload": target_table.payload_size if target_table else None,
                "target_rows": target_table.row_count if target_table else None,
            }
        result["targets"].append(
            {
                "path": str(target.path),
                "storage_version": target.storage_version,
                "table_count": len(target.tables),
                "roles": roles,
            }
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path)
    parser.add_argument("targets", nargs="+", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    reference = parse_database(args.reference)
    targets = [parse_database(path) for path in args.targets]
    report = describe(reference, targets)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    for target in report["targets"]:
        print(f"{target['storage_version']}\ttables={target['table_count']}\t{target['path']}")
        for name, role in target["roles"].items():
            exact = "exact" if role["exact_signature"] else "changed"
            print(
                f"  {name:26} {role['reference_ordinal']:3} -> "
                f"{str(role['target_ordinal']):>3}  {exact:7} "
                f"payload={role['reference_payload']}->{role['target_payload']} "
                f"rows={role['target_rows']}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
