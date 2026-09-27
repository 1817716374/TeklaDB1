#!/usr/bin/env python3
"""Compare shape fields/mesh topology with ElementTree and the paired bounds."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET
from corpus import materialize, safe_path, verify


def expected(raw, mode, storage_id):
    root = ET.fromstring(raw)
    fingerprint = 14695981039346656037

    def update(data):
        nonlocal fingerprint
        for byte in data:
            fingerprint = ((fingerprint ^ byte) * 1099511628211) & ((1 << 64) - 1)

    def number(value):
        update(int(value).to_bytes(8, "little"))

    def text(value):
        data = value.encode("utf8")
        number(len(data))
        update(data)

    def real(value):
        update(struct.pack("<d", float(value)))

    if mode == "shape_definition":
        if root.tag != "ImportPart" or len(root.findall("Info")) != 1:
            raise ValueError("unexpected ImportPart structure")
        info = root.find("Info")
        for name in ["Name", "Guid", "BrepStorageId"]:
            text(info.findtext(name, ""))
        number(info.find("BrepGeomType") is not None)
        number(info.findtext("BrepGeomType", "0"))
        number(info.find("IsSolid") is not None)
        number(info.findtext("IsSolid", "false").strip().lower() in ("true", "1"))
        text(info.findtext("Fingerprint", ""))
        for node in ["MinPoint", "MaxPoint"]:
            for axis in "XYZ":
                real(info.find("Extrema/" + node).attrib[axis])
        return f"raw_bytes={len(raw)} fingerprint={fingerprint:x}"
    if root.tag != "Polymesh":
        raise ValueError("unexpected Polymesh structure")
    points = root.findall("Points/Point")
    faces = root.findall("Faces/Face")
    edges = root.findall("Edges/Edge")
    text(storage_id)
    number(len(points))
    for point in points:
        for axis in "XYZ":
            real(point.findtext(axis))
    number(len(faces))
    for face in faces:
        outer = face.findall("OuterLoop/Index")
        number(len(outer))
        for index in outer:
            number(index.text)
        inner = face.findall("InnerLoops/Loop")
        number(len(inner))
        for loop in inner:
            indices = loop.findall("Index")
            number(len(indices))
            for index in indices:
                number(index.text)
    number(len(edges))
    for edge in edges:
        number(edge.findtext("FirstVertexIndex"))
        number(edge.findtext("SecondVertexIndex"))
        edge_type = edge.findtext("EdgeType")
        number({"VisibleEdge": 1, "InvisibleEdge": 2}.get(edge_type, 0))
        text(edge_type)
    return f"points={len(points)} faces={len(faces)} edges={len(edges)} raw_bytes={len(raw)} fingerprint={fingerprint:x}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / "tests/corpus.json").read_text(encoding="utf8"))
    specs = [f for f in manifest["files"] if f["path"].startswith("shapes-auvent/")]
    cases = {c["path"]: c for c in manifest["cases"] if c["mode"] in ("shape_definition", "shape_geometry")}
    if len(specs) != 2 or len(cases) != 2:
        raise ValueError("missing pinned shape XML evidence")
    originals = {}
    paths = {}
    for spec in specs:
        source = safe_path(args.data.resolve(), spec["path"])
        if args.download:
            materialize(spec, source, manifest.get("archives", {}), args.data.resolve())
        if not verify(source, spec):
            raise ValueError(f"missing or changed evidence: {source}")
        dest = safe_path(args.work.resolve(), spec["path"])
        if source.resolve() == dest.resolve():
            raise ValueError("work must differ from source")
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        raw = source.read_bytes()
        mode = cases[spec["path"]]["mode"]
        oracle = expected(raw, mode, source.stem)
        if oracle != cases[spec["path"]]["stdout"]:
            raise ValueError("pinned shape baseline differs from independent XML reader")
        paths[mode] = dest
        originals[mode] = raw

    def run(mode, path, oracle=None):
        result = subprocess.run([str(args.exe.resolve()), mode, str(path)], capture_output=True, encoding="utf8", timeout=60)
        if oracle is None:
            if result.returncode == 0:
                raise ValueError("invalid shape accepted")
        elif result.returncode or result.stdout.strip() != oracle:
            raise ValueError(f"shape XML evidence mismatch: {result.stdout} {result.stderr}")

    def control():
        for mode, path in paths.items():
            run(mode, path, expected(originals[mode], mode, path.stem))
        run("shape_pair_evidence", args.work.resolve() / "shapes-auvent",
            "definitions=1 geometries=1 linked=1 points=60 faces=58 edges=117 bounds=6 solid=0")

    control()
    definition = originals["shape_definition"]
    geometry = originals["shape_geometry"]
    variants = [
        ("shape_definition", definition.replace(b"<Info>", b"<Info><!-- <Name>wrong</Name> --><Extension><Name>nested</Name></Extension>")),
        ("shape_definition", definition.replace(b"IFC BREP", b"IFC &amp;lt;&#x4e2d;")),
        ("shape_geometry", geometry.replace(b"<Points>", b"<!-- <Points><Point><X>999</X></Point></Points> --><Points>")),
    ]
    try:
        for mode, raw in variants:
            path = paths[mode]
            path.write_bytes(raw)
            run(mode, path, expected(raw, mode, path.stem))
            path.write_bytes(originals[mode])
        invalid = [
            ("shape_geometry", geometry.replace(b"<X>-1784.3303761117575</X>", b"<X>nan</X>", 1)),
            ("shape_geometry", geometry.replace(b"<Index>56</Index>", b"<Index>999</Index>", 1)),
            ("shape_definition", definition.replace(b"<Guid>", b"<Guid>duplicate</Guid><Guid>", 1)),
        ]
        for mode, raw in invalid:
            if raw == originals[mode]:
                raise ValueError("invalid mutation did not reach its target")
            paths[mode].write_bytes(raw)
            run(mode, paths[mode])
            paths[mode].write_bytes(originals[mode])
        # Explicit synthetic compression of the public XML, not another vendor sample.
        compressed = args.work.resolve() / "gzip" / (paths["shape_geometry"].stem + ".tez")
        compressed.parent.mkdir(parents=True, exist_ok=True)
        compressed.write_bytes(gzip.compress(geometry, mtime=0))
        run("shape_geometry", compressed, expected(geometry, "shape_geometry", compressed.stem))
    finally:
        for mode, path in paths.items():
            path.write_bytes(originals[mode])
    control()
    print("2/2 independent shape XML comparisons, paired bounds, 3 structure/entity variants, 3 invalid mutations and gzip equivalence passed")


if __name__ == "__main__":
    main()
