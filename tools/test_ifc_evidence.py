#!/usr/bin/env python3
"""Check that the fixed IFC oracle rejects independently corrupted evidence.

Requires an already downloaded, pinned corpus. All copies and temporary files
are placed under the explicitly supplied work directory.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    original = (args.data / "btscm-ifc730/export.ifc").read_bytes()
    if hashlib.sha256(original).hexdigest() != "d02db80ded529334096fa911f2194978f96b4bc3a0720deee31247992cc295a3":
        raise ValueError("unexpected IFC input; download the pinned corpus first")
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ifc730-oracle-", dir=work) as temporary:
        root = Path(temporary).resolve()
        if not root.is_relative_to(work):
            raise ValueError("temporary path escaped the work directory")
        (root / "btscm-ifc730").mkdir()
        (root / "btscm-pechelire").mkdir()
        shutil.copyfile(args.data / "btscm-pechelire/pechelirejulV1.db1",
                        root / "btscm-pechelire/pechelirejulV1.db1")
        destination = root / "btscm-ifc730/export.ifc"

        def run(content, error=None):
            destination.write_bytes(content)
            result = subprocess.run([str(args.exe.resolve()), "ifc730_evidence", str(destination.parent)],
                                    capture_output=True, encoding="utf8", errors="replace", timeout=120)
            if error is None:
                if result.returncode != 0 or "corners=4288" not in result.stdout:
                    raise ValueError(f"unmodified control failed: {result.stdout} {result.stderr}")
            elif result.returncode != 1 or error not in result.stderr:
                raise ValueError(f"oracle missed {error}: {result.stdout} {result.stderr}")

        run(original)
        cases = [
            (b"IFCBEAM('1FyNJD000Jfp4pD34pCp0p'", b"IFCBEAM('1FyNJD000Jfp4pD34pCp0q'", "identity"),
            (b"IFCLABEL('C30 - BMASSIF')", b"IFCLABEL('Wrong material')", "material"),
            (b"#92= IFCCARTESIANPOINT((2270.0009,", b"#92= IFCCARTESIANPOINT((2271.0009,", "origin"),
            (b"#116= IFCCARTESIANPOINT((0.,15.,-62.5))", b"#116= IFCCARTESIANPOINT((0.,16.,-62.5))", "box corner"),
            (b".LENGTHUNIT.,.MILLI.,.METRE.", b".LENGTHUNIT.,.CENTI.,.METRE.", "length units"),
        ]
        for old, new, error in cases:
            if old not in original:
                raise ValueError(f"missing mutation anchor: {error}")
            run(original.replace(old, new, 1), error)
            print(f"PASS rejects changed {error}")
        run(original)
    print("5/5 IFC evidence mutations rejected; unmodified controls passed")


if __name__ == "__main__":
    main()
