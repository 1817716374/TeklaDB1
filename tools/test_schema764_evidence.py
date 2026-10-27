#!/usr/bin/env python3
"""Pinned 7.64/7.30/IFC controls and independent-evidence mutations."""
import argparse
import gzip
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import corpus


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--download', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / 'tests/corpus.json').read_text(encoding='utf-8'))
    specs = [s for s in manifest['files'] if s['path'].startswith('u52-764/')]
    assert len(specs) == 6
    for spec in specs:
        dest = corpus.safe_path(args.data, spec['path'])
        if not corpus.verify(dest, spec):
            if not args.download:
                raise ValueError(f'missing/changed source: {dest}')
            corpus.materialize(spec, dest, manifest['archives'], args.data)
    args.work.mkdir(parents=True, exist_ok=True)
    checks = 0

    def run(mode, path, fails=False, error=None):
        nonlocal checks
        result = subprocess.run([str(args.exe.resolve()), mode, str(path)], capture_output=True, encoding='utf-8', timeout=120)
        if fails:
            assert result.returncode != 0 and result.stderr.strip(), (mode, result.stdout, result.stderr)
            if error:
                assert error in result.stderr, (error, result.stderr)
        else:
            assert result.returncode == 0, (mode, result.stderr)
        checks += 1

    source = args.data / 'u52-764'
    run('schema764_evidence', source)
    with tempfile.TemporaryDirectory(prefix='schema764-', dir=args.work) as tmp:
        work = Path(tmp)
        for spec in specs:
            shutil.copy2(args.data / spec['path'], work / Path(spec['path']).name)
        ifc = work / 'U52_2007V4.ifc'
        original = ifc.read_text(encoding='ascii')

        def mutate(pattern, replacement):
            changed, count = re.subn(pattern, replacement, original, count=1)
            assert count == 1 and changed != original, pattern
            ifc.write_text(changed, encoding='ascii')
            run('schema764_evidence', work, True)

        mutate(r"'TS_11719'", "'TS_999999'")
        mutate(r"(IFCBEAM\()'[^']+'", r"\1'0000000000000000000000'")
        mutate(r"(IFCPROPERTYSINGLEVALUE\('Nom','Nom',IFCLABEL\()'plat'", r"\1'wrong'")
        mutate(r"IFCLABEL\('S235JR'\)", "IFCLABEL('S355JR')")
        mutate(r"(IFCPROPERTYSINGLEVALUE\('Classe','Classe',IFCLABEL\()'2'", r"\1'99'")
        mutate(r"(IFCPROPERTYSINGLEVALUE\('OrigineX','OrigineX',IFCLENGTHMEASURE\()[^)]*", r"\g<1>123456.")
        mutate(r"(IFCPROPERTYSINGLEVALUE\('Profil','Profil',IFCLABEL\()'IPE450'", r"\1'IPE999'")
        mutate(r"(IFCSIUNIT\([^;]*\.LENGTHUNIT\.,)\.MILLI\.", r"\1$")
        # The first exported fastener's independent placement, not a DB1-derived
        # expected coordinate. All byte/hash pins are checked before mutations.
        mutate(r"(IFCCARTESIANPOINT\(\()66\.44931,-156\.90818,5894\.0172", r"\g<1>99999.,-156.90818,5894.0172")
        ifc.write_text(original, encoding='ascii')
        run('schema764_evidence', work)
        model = work / 'u52_2007vierge.db1'
        original_model = model.read_bytes()
        decoded = gzip.decompress(original_model)
        assert b'7.64' in decoded[:100]
        model.write_bytes(gzip.compress(decoded.replace(b'7.64', b'7.63', 1)))
        run('model', model, True, 'unsupported')
        model.write_bytes(gzip.compress(decoded[:-1]))
        run('model', model, True)
        model.write_bytes(original_model)
        run('schema764_evidence', work)
    print(f'7.64 native controls and {checks} parser/evidence checks passed; no generic GUID migration inferred')


if __name__ == '__main__':
    main()
