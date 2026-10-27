#!/usr/bin/env python3
"""Verify native DB6 framing and project boundaries; no semantic truth claim."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

import corpus


def independent_layout(data):
    """Length-driven Python walk, independently checked against native counts."""
    def words(offset, count):
        return struct.unpack_from('<' + 'I' * count, data, offset)
    assert words(0, 6) == (10014, 1, 0xdbcec0bc, 0, 1, 13)
    contexts = words(24, 1)[0]
    pos = 32
    for ordinal in range(1, contexts):
        assert words(pos, 1)[0] == ordinal
        pos += 24
    shift, count = words(pos, 2)
    assert shift == 5
    pos += 8 + count * 8
    assert words(pos, 1)[0] == contexts
    pos += 20
    first = pos
    tables, rows = 0, 0
    while pos < len(data):
        magic, width, count = words(pos, 3)
        assert magic == 0xdbcec066 and width > 0 and count > 0
        assert set(words(pos + 12, count)) <= {0, 1}
        pos += 12 + 4 * count
        while data[pos] != 0:
            assert data[pos] == 4
            pos += 1 + width + 8
            rows += 1
        pos += 1
        tables += 1
    assert pos == len(data)
    assert (first, tables, rows, len(data)) == (47004, 86, 79, 57113)
    fingerprint = 14695981039346656037
    for value in data:
        fingerprint = ((fingerprint ^ value) * 1099511628211) & ((1 << 64) - 1)
    return first, f'version=DB6-10014 tables={tables} records={rows} opaque=0 bytes={len(data)} fingerprint={fingerprint:x}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--download', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / 'tests/corpus.json').read_text(encoding='utf-8'))
    specs = [f for f in manifest['files'] if f['path'].startswith('analysis-defaults/')]
    assert len(specs) == 2
    for spec in specs:
        source = corpus.safe_path(args.data, spec['path'])
        if not corpus.verify(source, spec):
            if not args.download:
                raise ValueError(f'missing/changed source: {source}')
            corpus.materialize(spec, source, manifest['archives'], args.data)
    args.work.mkdir(parents=True, exist_ok=True)
    checks = 0
    def run(mode, path, expected=None, fail=False):
        nonlocal checks
        p = subprocess.run([str(args.exe.resolve()), mode, str(path)], capture_output=True, encoding='utf-8', timeout=120)
        if fail:
            assert p.returncode != 0 and p.stderr.strip(), (mode, p.stdout, p.stderr)
        else:
            assert p.returncode == 0 and p.stdout.strip() == expected, (mode, p.stdout, p.stderr, expected)
        checks += 1
    with tempfile.TemporaryDirectory(prefix='db6-', dir=args.work.resolve()) as temp:
        project = Path(temp)
        for spec in specs:
            shutil.copyfile(corpus.safe_path(args.data, spec['path']), project / Path(spec['path']).name)
        path = project / 'AnalysisPartDefaults.db6'
        compressed = path.read_bytes()
        data = gzip.decompress(compressed)
        first, expected = independent_layout(data)
        run('raw', path, expected)
        path.write_bytes(data)
        run('raw', path, expected)
        renamed = project / 'renamed.bin'
        renamed.write_bytes(data)
        run('raw', renamed, expected)
        renamed.unlink()
        baseline = 'analysis_files=1 raw=1 failed=0 discovered=0 rows=79 associations=0'
        run('analysis_project', project, baseline)
        run('analysis_project_disabled', project, 'analysis_files=1 raw=0 failed=0 discovered=1 rows=0 associations=0')
        run('analysis_project_strict', project, baseline)
        # Discovery under mixed-case Analysis also works on case-sensitive hosts.
        nested = project / 'aNaLySiS'
        nested.mkdir()
        target = nested / 'unrelated-name.DB6'
        path.rename(target)
        run('analysis_project', project, baseline)
        run('analysis_project_disabled', project, 'analysis_files=1 raw=0 failed=0 discovered=1 rows=0 associations=0')
        target.rename(path)
        mutations = []
        for offset in (0, 4, 8, 24, 32, 128, 132, 46984, first, first + 8, first + 12):
            mutated = bytearray(data)
            struct.pack_into('<I', mutated, offset, 0xffffffff)
            mutations.append(bytes(mutated))
        mutations.extend((data[:-1], data + b'\0', compressed[:-6]))
        for mutated in mutations:
            path.write_bytes(mutated)
            run('raw', path, fail=True)
        run('analysis_project', project, 'analysis_files=1 raw=0 failed=1 discovered=0 rows=0 associations=0')
        run('analysis_project_strict', project, fail=True)
        run('analysis_project_disabled', project, 'analysis_files=1 raw=0 failed=0 discovered=1 rows=0 associations=0')
        path.write_bytes((project / 'Defining drawing views steel.db1').read_bytes())
        run('analysis_project', project, 'analysis_files=1 raw=0 failed=1 discovered=0 rows=0 associations=0')
        run('analysis_project_strict', project, fail=True)
        path.write_bytes(compressed)
        run('analysis_project', project, baseline)
    print(f'DB6 independent layout/reconstruction and {checks} parser/project checks passed; semantics remain unverified')


if __name__ == '__main__':
    main()
