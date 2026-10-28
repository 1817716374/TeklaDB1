#!/usr/bin/env python3
"""Independently walk native U52 DB2 records; verify history and scoped joins."""
import argparse
import gzip
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import corpus


def walk(data):
    image = gzip.decompress(data) if data[:2] == b'\x1f\x8b' else data
    assert image[:12] in (b'Xsteel\x80 7.64', b'Xsteel  7.30')
    pos, tables, series = 12, [], []
    while pos < len(image):
        tid, count, width = struct.unpack_from('<III', image, pos)
        offset = pos
        pos += 12
        rows = []
        assert count * (width + 1) <= len(image) - pos
        for _ in range(count):
            assert image[pos] == 4  # observed native allocation tag only
            raw = image[pos + 1:pos + 1 + width]
            rows.append((pos, raw))
            pos += width + 1
            if tid == 22:
                assert width == 76
                key = raw[:52].split(b'\0', 1)[0].decode('ascii')
                series.append((key, int(key.rsplit('/', 1)[1]), *struct.unpack_from('<6I', raw, 52)))
        assert struct.unpack_from('<I', image, pos)[0] == 0xbc614f
        pos += 4
        tables.append((tid, offset, width, rows))
    assert pos == len(image)
    return image, tables, series


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--download', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / 'tests/corpus.json').read_text(encoding='utf-8'))
    names = {'u52_2007vierge.db1', 'xslib.db1', 'u52_2007vierge.db2', 'xslib.db2', 'u52_2007v4.db2.bak', 'numbering.history'}
    specs = [s for s in manifest['files'] if s['path'].startswith('u52-764/') and Path(s['path']).name in names]
    assert len(specs) == len(names)
    for spec in specs:
        dest = corpus.safe_path(args.data, spec['path'])
        if not corpus.verify(dest, spec):
            if not args.download:
                raise ValueError(f'missing/changed input: {dest}')
            corpus.materialize(spec, dest, manifest['archives'], args.data)
    args.work.mkdir(parents=True, exist_ok=True)
    checks = 0

    def run(mode, path, fail=False, expected=None):
        nonlocal checks
        p = subprocess.run([str(args.exe.resolve()), mode, str(path)], capture_output=True, encoding='utf-8', timeout=120)
        if fail:
            assert p.returncode != 0 and p.stderr.strip(), (mode, p.stdout, p.stderr)
        else:
            assert p.returncode == 0, (mode, p.stderr)
            if expected is not None:
                assert p.stdout.splitlines() == expected, (mode, p.stdout, expected)
        checks += 1

    source = args.data / 'u52-764'
    model_name = 'u52_2007vierge.db2'
    image, tables, series = walk((source / model_name).read_bytes())
    assert len(tables) == 9 and len(series) == 4
    _, old_tables, old_series = walk((source / 'u52_2007v4.db2.bak').read_bytes())
    assert sorted(series) == sorted(old_series) and series != old_series
    assert [(t[0], t[2]) for t in tables] == [(t[0], t[2]) for t in old_tables]
    assert walk((source / 'xslib.db2').read_bytes())[1:] == ([], [])
    fields = ['\t'.join(map(str, row)) for row in series]
    run('numbering764_fields', source / model_name, expected=fields)
    run('numbering764_fields', source / 'xslib.db2', expected=[])
    run('numbering764_evidence', source)
    run('numbering764_raw_off', source)
    with tempfile.TemporaryDirectory(prefix='numbering764-', dir=args.work) as temporary:
        work = Path(temporary)
        for spec in specs:
            shutil.copy2(args.data / spec['path'], work / Path(spec['path']).name)
        target = work / model_name
        original = target.read_bytes()
        target.write_bytes(image)
        run('numbering764_evidence', work)
        _, table_at, _, rows = next(t for t in tables if t[0] == 22)
        for label, offset, value in [('part counter', rows[0][0] + 1 + 52, 1), ('assembly counter', rows[0][0] + 1 + 56, 21)]:
            changed = bytearray(image)
            struct.pack_into('<I', changed, offset, value)
            target.write_bytes(gzip.compress(changed))
            run('numbering764_evidence', work, fail=True)
        changed = bytearray(image)
        struct.pack_into('<I', changed, rows[0][0] + 1 + 60, 123456)
        target.write_bytes(gzip.compress(changed))
        extra = [list(s) for s in series]
        extra[0][4] = 123456
        run('numbering764_fields', target, expected=['\t'.join(map(str, row)) for row in extra])
        # These slots remain uninterpreted; changing one must not alter pairing.
        run('numbering764_evidence', work)
        invalid = []
        changed = bytearray(image)
        struct.pack_into('<I', changed, table_at + 8, 80)
        invalid.append(changed)
        changed = bytearray(image)
        changed[rows[0][0]] = 0
        invalid.append(changed)
        changed = bytearray(image)
        changed[rows[1][0] + 1:rows[1][0] + 53] = changed[rows[0][0] + 1:rows[0][0] + 53]
        invalid.append(changed)
        invalid.extend([image[:-1], image.replace(b'7.64', b'7.63', 1), image[:12] + b' 01234567-89ab-cdef-0123-456789abcdef' + image[12:]])
        for changed in invalid:
            target.write_bytes(gzip.compress(changed))
            run('numbering', target, fail=True)
        target.write_bytes(gzip.compress(image.replace(b'7.64', b'7.82', 1)))
        run('numbering764_unpaired', work)
        target.unlink()
        run('numbering764_unpaired', work)
        target.write_bytes(original)
        history = work / 'numbering.history'
        original_history = history.read_bytes()
        mutations = [
            (rb'(A/1 Max Part\s+0 Max Rebar\s+0 Max Assembly\s+)20', rb'\g<1>21'),
            (rb'(Assembly id:\s*11705\s+series:A/1\s+A/0 -> A/)20', rb'\g<1>21'),
            (rb'(Part\s+id:\s*446\s+series:/1\s+[^\r\n]* -> /)\d+', rb'\g<1>9999'),
        ]
        for pattern, replacement in mutations:
            changed, count = re.subn(pattern, replacement, original_history)
            assert count > 0 and changed != original_history, pattern
            history.write_bytes(changed)
            run('numbering764_evidence', work, fail=True)
        history.write_bytes(original_history)
        run('numbering764_evidence', work)
    print(f'7.64 DB2 four-series independent values, 103 history objects, 105 scoped links and {checks} parser/project checks passed')


if __name__ == '__main__':
    main()
