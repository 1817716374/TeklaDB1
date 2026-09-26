#!/usr/bin/env python3
"""Fetch pinned public samples outside the repository and verify parser regressions.

No third-party model is redistributed. Downloads are explicit, SHA-256 checked,
and have exact size budgets. The manifest records regression baselines, not a
claim of independent geometric ground truth.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import urllib.request
import zipfile


def safe_path(root, relative):
    p = PurePosixPath(relative)
    if p.is_absolute() or ".." in p.parts or not p.parts or ":" in str(p) or "\\" in str(p):
        raise ValueError(f"unsafe manifest path: {relative}")
    dest = (root / str(p)).resolve()
    if not dest.is_relative_to(root.resolve()):
        raise ValueError(f"path escapes corpus directory: {relative}")
    return dest


def verify(path, spec):
    if not path.is_file() or path.stat().st_size != spec["size"]:
        return False
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest() == spec["sha256"]


def fetch(spec, destination):
    if verify(destination, spec):
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".partial")
    try:
        request = urllib.request.Request(spec["url"], headers={
            "User-Agent": "TeklaFormats-corpus/1.0 (+https://github.com/1817716374/TeklaDB1)"})
        with urllib.request.urlopen(request, timeout=60) as response, temporary.open("wb") as out:
            total = 0
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > spec["size"]:
                    raise ValueError("download exceeded pinned byte count")
                out.write(chunk)
        if not verify(temporary, spec):
            raise ValueError(f"download hash/size mismatch: {spec['url']}")
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def extract_member(spec, archive_path, destination):
    with zipfile.ZipFile(archive_path) as z:
        matches = [i for i in z.infolist() if i.filename == spec["member"]]
        if len(matches) != 1:
            raise ValueError("missing or ambiguous archive member")
        info = matches[0]
        if info.is_dir() or info.file_size != spec["size"]:
            raise ValueError("archive member exceeds pinned byte count")
        with z.open(info) as source:
            content = source.read(spec["size"] + 1)
    if len(content) != spec["size"] or hashlib.sha256(content).hexdigest() != spec["sha256"]:
        raise ValueError("archive member hash/size mismatch")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(content)


def materialize(spec, destination, archives, data, visiting=None):
    if verify(destination, spec):
        return
    if "archive" not in spec:
        fetch(spec, destination)
        return
    visiting = set() if visiting is None else visiting
    name = spec["archive"]
    if name in visiting or name not in archives:
        raise ValueError("cyclic or missing pinned archive")
    archive_path = safe_path(data, "_downloads/" + name + ".zip")
    visiting.add(name)
    try:
        materialize(archives[name], archive_path, archives, data, visiting)
        extract_member(spec, archive_path, destination)
    finally:
        visiting.remove(name)


def main():
    # Redirected Windows consoles may use a code page that cannot represent a
    # public sample's filename. Keep progress/error output from aborting a run;
    # paths, validator output and the UTF-8 JSON report remain unchanged.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).resolve().parents[1] / "tests/corpus.json")
    parser.add_argument("--data", type=Path, required=True, help="external corpus directory; all downloads stay here")
    parser.add_argument("--download", action="store_true", help="explicitly fetch pinned sources from the public Internet")
    parser.add_argument("--exe", type=Path, required=True, help="built tekladb1_validate executable")
    parser.add_argument("--report", type=Path, help="JSON results path (outside the source tree recommended)")
    args = parser.parse_args()
    data = args.data.resolve()
    manifest = json.loads(args.manifest.read_text(encoding="utf8"))
    archives = manifest.get("archives", {})
    for spec in manifest["files"]:
        path = safe_path(data, spec["path"])
        if not verify(path, spec):
            if not args.download:
                raise ValueError(f"missing or changed corpus file: {path}; use --download")
            materialize(spec, path, archives, data)
    failures = 0
    results = []
    for case in manifest["cases"]:
        path = safe_path(data, case["path"])
        result = subprocess.run([str(args.exe.resolve()), case["mode"], str(path)],
                                capture_output=True, encoding="utf8", errors="replace", timeout=120)
        ok = result.returncode == case["exit"]
        if "stdout" in case:
            ok = ok and result.stdout.strip() == case["stdout"]
        if "error_contains" in case:
            ok = ok and case["error_contains"] in result.stderr
        results.append(dict(name=case["name"],passed=ok,exit=result.returncode,
                            stdout=result.stdout.strip(),stderr=result.stderr.strip()))
        failures += not ok
        print(("PASS " if ok else "FAIL ") + case["name"])
        if not ok:
            print(result.stdout, result.stderr)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf8")
    print(f"{len(results)-failures}/{len(results)} corpus cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.SubprocessError, zipfile.BadZipFile) as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
