#!/usr/bin/env python3
"""Independent 9.50 identity-table and matched-IFC checks for public samples."""
from __future__ import annotations
import argparse
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_schema

SAMPLES = (
    ("Housing_Concrete_BuildingPermit", 65943, 36045, 17397, 719, 0, 3, 27, 65525, 329, 89),
    ("Housing_Timber_BuildingPermit", 25088, 4454, 2137, 1652, 1, 3, 25, 17755, 7328, 5),
    ("Office_Concrete_BuildingPermit", 125552, 9609, 3614, 1643, 0, 3, 26, 125304, 42, 206),
    ("Office_Timber_BuildingPermit", 97522, 6547, 2373, 1003, 0, 2, 19, 97480, 0, 42),
)
WORD2_REFERENCE_EXPECTATIONS = {
    # non-self word 2 values resolve to these many distinct identities; every
    # such target is outside the DB1 part and assembly tables.
    "Housing_Concrete_BuildingPermit": (8, 1, 8, 0),
    "Housing_Timber_BuildingPermit": (174, 2, 81, 93),
    "Office_Concrete_BuildingPermit": (4, 1, 4, 0),
    "Office_Timber_BuildingPermit": (0, 0, 0, 0),
}
ALPHABET = {char: index for index, char in enumerate("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$")}
ENTITY = re.compile(r"#(\d+)=\s*(IFC[A-Z0-9_]+)\((.*)\);\s*$")
GUID = re.compile(r"'([0-9A-Za-z_$]{22})'")
REFERENCE = re.compile(r"#(\d+)")


def split_args(source: str) -> list[str]:
    result: list[str] = []
    start = depth = 0
    quoted = False
    index = 0
    while index < len(source):
        char = source[index]
        if char == "'":
            if quoted and index + 1 < len(source) and source[index + 1] == "'":
                index += 2
                continue
            quoted = not quoted
        elif not quoted:
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            elif char == "," and depth == 0:
                result.append(source[start:index].strip())
                start = index + 1
        index += 1
    result.append(source[start:].strip())
    return result


def decode_ifc_guid(value: str) -> bytes:
    number = 0
    for char in value:
        number = (number << 6) | ALPHABET[char]
    if number.bit_length() > 128:
        raise ValueError(f"IFC GUID overflows 128 bits: {value}")
    return number.to_bytes(16, "big")


def db1_identity_rows(path: Path):
    data = analyze_schema.read_payload(path)
    database = analyze_schema.parse_database(path)
    if database.storage_version != "9.50" or len(database.tables) != 355:
        raise ValueError(f"unexpected DB1 schema: {path}")
    # Ordinal 315 is the identity-class table in adjacent legacy schemas. In
    # these 9.50 samples it retains the 28-byte layout but has no rows, so it
    # cannot explain the repeated second identity word as a class reference.
    class_candidate = database.tables[315]
    if class_candidate.payload_size != 28 or class_candidate.row_count != 0:
        raise ValueError(f"unexpected 9.50 ordinal-315 candidate table: {path}")
    offsets = analyze_schema.section_offsets(data)

    def table_rows(ordinal: int):
        table = database.tables[ordinal]
        offset = offsets[ordinal]
        field_count = struct.unpack_from("<I", data, offset + 8)[0]
        descriptors = struct.unpack_from(f"<{field_count}I", data, offset + 12)
        first = offset + 12 + field_count * 4
        stride = table.payload_size + 9
        return table, descriptors, [data[first + row * stride:first + row * stride + stride] for row in range(table.row_count)]

    identity_table, descriptors, rows = table_rows(353)
    if (identity_table.payload_size, descriptors) != (32, (1, 1, 1, 1, 1, 0)):
        raise ValueError("9.50 table 353 signature changed")
    identities = {}
    guid_ids = {}
    for row in rows:
        if len(row) != 41 or row[0] not in (4, 12):
            raise ValueError("invalid 9.50 identity row framing")
        words = struct.unpack_from("<4I", row, 1)
        guid = row[17:33]
        if words[0] in identities or guid in guid_ids:
            raise ValueError("duplicate 9.50 identity ID or GUID")
        identities[words[0]] = (words, guid)
        guid_ids[guid] = words[0]
    _, _, part_rows = table_rows(274)
    parts = {struct.unpack_from("<I", row, 1)[0] for row in part_rows}
    _, _, assembly_rows = table_rows(300)
    assemblies = {struct.unpack_from("<I", row, 1)[0] for row in assembly_rows}
    return identities, parts, assemblies


def verify_sample(root: Path, stem: str, expected: tuple[int, int, int, int, int, int]) -> str:
    folder = root / "BIM4LCA" / stem
    db1 = folder / f"STRUC_NordicLCA_{stem}.db1"
    ifc = folder / f"STRUC_NordicLCA_{stem}.ifc"
    identities, parts, assemblies = db1_identity_rows(db1)
    entities = {}
    aggregate_relations = []
    with ifc.open(encoding="latin1") as source:
        for line in source:
            match = ENTITY.match(line)
            if not match:
                continue
            entity_id, entity_type = int(match.group(1)), match.group(2)
            arguments = split_args(match.group(3))
            if arguments:
                guid_match = GUID.fullmatch(arguments[0])
                if guid_match:
                    entities[entity_id] = (entity_type, decode_ifc_guid(guid_match.group(1)))
            if entity_type == "IFCRELAGGREGATES" and len(arguments) == 6:
                parent = REFERENCE.fullmatch(arguments[4])
                if parent:
                    aggregate_relations.append((int(parent.group(1)), [int(x) for x in REFERENCE.findall(arguments[5])]))

    unresolved_owner_ids = sum(bool(words[3]) and words[3] not in identities for words, _ in identities.values())
    guid_matches = 0
    guid_to_row = {guid: (object_id, words) for object_id, (words, guid) in identities.items()}
    word1_ifc_types: dict[int, set[str]] = defaultdict(set)
    part_matches = assembly_matches = 0
    for entity_type, guid in entities.values():
        record = guid_to_row.get(guid)
        if record is None:
            continue
        guid_matches += 1
        object_id = record[0]
        word1_ifc_types[record[1][1]].add(entity_type)
        if entity_type == "IFCELEMENTASSEMBLY":
            if object_id not in assemblies:
                raise ValueError(f"IFCELEMENTASSEMBLY GlobalId resolved to non-assembly DB1 ID {object_id}")
            assembly_matches += 1
        else:
            if object_id not in parts:
                raise ValueError(f"IFC entity GlobalId resolved to non-part DB1 ID {object_id}")
            part_matches += 1

    checked_owner_edges = matching_owner_edges = 0
    entity_to_guid = {entity_id: guid for entity_id, (_, guid) in entities.items()}
    for parent_entity, child_entities in aggregate_relations:
        parent_guid = entity_to_guid.get(parent_entity)
        parent = guid_to_row.get(parent_guid) if parent_guid else None
        if not parent:
            continue
        for child_entity in child_entities:
            child_guid = entity_to_guid.get(child_entity)
            child = guid_to_row.get(child_guid) if child_guid else None
            if not child:
                continue
            checked_owner_edges += 1
            if child[1][3] == parent[0]:
                matching_owner_edges += 1
    (expected_identities, expected_guid_count, expected_matches, expected_edges,
     expected_unresolved_owners, expected_multiclass_word1, expected_word1_values,
     expected_word2_self, expected_word2_other, expected_word2_zero) = expected
    if len(identities) != expected_identities or len(entities) != expected_guid_count:
        raise ValueError("pinned DB1/IFC sample inventory changed")
    if guid_matches != expected_matches or checked_owner_edges != expected_edges or matching_owner_edges != expected_edges:
        raise ValueError("DB1 GUID/owner cross-check changed")
    if unresolved_owner_ids != expected_unresolved_owners:
        raise ValueError("DB1 unresolved owner ID inventory changed")
    multiclass_word1 = sum(len(entity_types) > 1 for entity_types in word1_ifc_types.values())
    if multiclass_word1 != expected_multiclass_word1:
        raise ValueError("DB1 raw identity word 1 / IFC class ambiguity changed")
    word1_values = len({words[1] for words, _ in identities.values()})
    word2_distribution = Counter(
        "zero" if words[2] == 0 else "self" if words[2] == object_id else "other"
        for object_id, (words, _) in identities.items()
    )
    if (word1_values != expected_word1_values or
            word2_distribution["self"] != expected_word2_self or
            word2_distribution["other"] != expected_word2_other or
            word2_distribution["zero"] != expected_word2_zero):
        raise ValueError("DB1 opaque identity-word value distribution changed")
    # Inspect cross-identity word-2 references without assigning them a
    # semantic label. These targets are identities, but not part/assembly IDs.
    word2_targets = {
        words[2]
        for object_id, (words, _) in identities.items()
        if words[2] and words[2] != object_id
    }
    target_word2 = Counter(
        "zero" if identities[target][0][2] == 0 else
        "self" if identities[target][0][2] == target else "other"
        for target in word2_targets
    )
    target_word1_values = {identities[target][0][1] for target in word2_targets}
    target_in_object_tables = sum(target in parts or target in assemblies for target in word2_targets)
    ifc_guids = {guid for _, guid in entities.values()}
    target_ifc_guid_matches = sum(identities[target][1] in ifc_guids for target in word2_targets)
    source_target_word1_equal = sum(
        words[1] == identities[words[2]][0][1]
        for object_id, (words, _) in identities.items()
        if words[2] and words[2] != object_id
    )
    expected_targets, expected_target_word1_values, expected_target_self, expected_target_other = (
        WORD2_REFERENCE_EXPECTATIONS[stem]
    )
    if (len(word2_targets) != expected_targets or
            len(target_word1_values) != expected_target_word1_values or
            target_word2["self"] != expected_target_self or
            target_word2["other"] != expected_target_other or
            target_in_object_tables != 0 or target_ifc_guid_matches != 0 or
            source_target_word1_equal != 0):
        raise ValueError("DB1 non-self word-2 reference inventory changed")
    if part_matches + assembly_matches != guid_matches:
        raise ValueError("a matched IFC identity did not classify as a DB1 part or assembly")
    return (f"model={stem} identities={len(identities)} ifc_globalids={len(entities)} "
            f"direct_guid_matches={guid_matches} part_matches={part_matches} assembly_matches={assembly_matches} "
            f"owner_edges={matching_owner_edges}/{checked_owner_edges} unresolved_owner_ids={unresolved_owner_ids} "
            f"word1_values={word1_values} word1_multiclass_groups={multiclass_word1} "
            f"word2_self={word2_distribution['self']} word2_other_identity={word2_distribution['other']} "
            f"word2_zero={word2_distribution['zero']} word2_distinct_targets={len(word2_targets)} "
            f"word2_target_word1_values={len(target_word1_values)} "
            f"word2_targets_part_or_assembly={target_in_object_tables} "
            f"word2_targets_with_ifc_globalid={target_ifc_guid_matches} "
            f"word2_target_self={target_word2['self']} word2_target_other={target_word2['other']} "
            f"source_target_word1_equal={source_target_word1_equal}")


def verify_abri(root: Path) -> str:
    """Cross-check 9.50 part/assembly/bolt table identities against a real IFC export."""
    folder = root / "abri-moto-vccf25" / "Abri-Moto-VCCF25"
    db1 = folder / "Abri-Moto-VCCF25.db1"
    ifc = folder / "IFC" / "Abri-moto-VCCF25.ifc"
    identities, parts, assemblies = db1_identity_rows(db1)
    data = analyze_schema.read_payload(db1)
    database = analyze_schema.parse_database(db1)
    offsets = analyze_schema.section_offsets(data)

    def ids_in_table(ordinal: int) -> set[int]:
        table = database.tables[ordinal]
        offset = offsets[ordinal]
        fields = struct.unpack_from("<I", data, offset + 8)[0]
        first = offset + 12 + fields * 4
        stride = table.payload_size + 9
        return {struct.unpack_from("<I", data, first + row * stride + 1)[0]
                for row in range(table.row_count)}

    bolts = ids_in_table(310)
    if (len(identities), len(parts), len(assemblies), len(bolts)) != (1715, 177, 106, 109):
        raise ValueError("Abri-Moto 9.50 identity/object table inventory changed")
    guid_to_identity = {guid: (object_id, words) for object_id, (words, guid) in identities.items()}
    entity_types: dict[int, str] = {}
    entity_guid: dict[int, bytes] = {}
    relationships: list[tuple[int, list[int]]] = []
    with ifc.open(encoding="latin1") as source:
        for line in source:
            match = ENTITY.match(line)
            if not match:
                continue
            entity_id, entity_type = int(match.group(1)), match.group(2)
            arguments = split_args(match.group(3))
            if arguments:
                guid_match = GUID.fullmatch(arguments[0])
                if guid_match:
                    entity_types[entity_id] = entity_type
                    entity_guid[entity_id] = decode_ifc_guid(guid_match.group(1))
            if entity_type == "IFCRELAGGREGATES" and len(arguments) == 6:
                parent = REFERENCE.fullmatch(arguments[4])
                if parent:
                    relationships.append((int(parent.group(1)), [int(x) for x in REFERENCE.findall(arguments[5])]))

    categories: Counter[tuple[str, str]] = Counter()
    entity_to_model: dict[int, int] = {}
    for entity_id, guid in entity_guid.items():
        identity = guid_to_identity.get(guid)
        if identity is None:
            continue
        object_id, _ = identity
        entity_to_model[entity_id] = object_id
        entity_type = entity_types[entity_id]
        table = ("part" if object_id in parts else "assembly" if object_id in assemblies
                 else "bolt" if object_id in bolts else "other")
        categories[(entity_type, table)] += 1
        if entity_type in {"IFCBEAM", "IFCCOLUMN", "IFCPLATE", "IFCMEMBER"} and table != "part":
            raise ValueError(f"IFC {entity_type} identity is absent from DB1 part table")
        if entity_type == "IFCELEMENTASSEMBLY" and table != "assembly":
            raise ValueError("IFC assembly identity is absent from DB1 assembly table")
        if entity_type == "IFCMECHANICALFASTENER" and table != "bolt":
            raise ValueError("IFC mechanical fastener identity is absent from DB1 bolt-group table")

    expected = Counter({("IFCBEAM", "part"): 56, ("IFCCOLUMN", "part"): 35,
        ("IFCPLATE", "part"): 55, ("IFCMEMBER", "part"): 12,
        ("IFCELEMENTASSEMBLY", "assembly"): 99,
        ("IFCMECHANICALFASTENER", "bolt"): 108, ("IFCFASTENER", "other"): 55})
    if categories != expected:
        raise ValueError(f"Abri-Moto IFC/DB1 identity classification changed: {categories}")
    owner_edges = matching_edges = 0
    for parent, children in relationships:
        parent_id = entity_to_model.get(parent)
        if parent_id is None:
            continue
        for child in children:
            child_id = entity_to_model.get(child)
            if child_id is None:
                continue
            owner_edges += 1
            matching_edges += identities[child_id][0][3] == parent_id
    if (matching_edges, owner_edges) != (158, 266):
        raise ValueError("Abri-Moto IFC aggregation/DB1 owner evidence changed")
    return ("model=Abri-Moto-VCCF25 identities=1715 part_rows=177 assembly_rows=106 bolt_rows=109 "
            "ifc_parts=158/158 ifc_assemblies=99/99 ifc_mechanical_fasteners=108/108 "
            "ifc_fasteners_unclassified=55 owner_edges=158/266")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True, help="materialized external corpus directory")
    args = parser.parse_args()
    for sample in SAMPLES:
        print(verify_sample(args.corpus, sample[0], sample[1:]))
    print(verify_abri(args.corpus))
    return 0


if __name__ == "__main__":
    sys.exit(main())
