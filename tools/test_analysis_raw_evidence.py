#!/usr/bin/env python3
"""Verify native DB6 framing and project boundaries; no semantic truth claim."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

import corpus
import analyze_schema


def independent_layout(data, model_ids):
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
    direct_id_matches = sum(value in model_ids for value in words(pos + 8, count * 2))
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
            payload = pos + 1
            for field_offset in range(0, width - 3, 4):
                if words(payload + field_offset, 1)[0] in model_ids:
                    direct_id_matches += 1
            pos += 1 + width + 8
            rows += 1
        pos += 1
        tables += 1
    assert pos == len(data)
    assert (first, tables, rows, len(data)) == (47004, 86, 79, 57113)
    fingerprint = 14695981039346656037
    for value in data:
        fingerprint = ((fingerprint ^ value) * 1099511628211) & ((1 << 64) - 1)
    return (first,
            f'version=DB6-10014 tables={tables} records={rows} opaque=0 bytes={len(data)} fingerprint={fingerprint:x}',
            direct_id_matches)


def model_identity_ids(path):
    """Read the paired 9.52 DB1 identity keys without using the C++ parser."""
    database = analyze_schema.parse_database(path)
    if database.storage_version != '9.52' or len(database.tables) != 356:
        raise ValueError(f'unexpected paired DB1 schema: {path}')
    data = analyze_schema.read_payload(path)
    offsets = analyze_schema.section_offsets(data)
    table = database.tables[355]
    if table.payload_size != 72 or table.row_count != 277:
        raise ValueError(f'unexpected paired DB1 identity table: {path}')
    offset = offsets[355]
    field_count = struct.unpack_from('<I', data, offset + 8)[0]
    first = offset + 12 + field_count * 4
    stride = table.payload_size + 9
    ids = {struct.unpack_from('<I', data, first + row * stride + 1)[0]
           for row in range(table.row_count)}
    if len(ids) != 277:
        raise ValueError(f'unexpected duplicate DB1 identity IDs: {path}')
    return ids


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
        model_ids = model_identity_ids(project / 'Defining drawing views steel.db1')
        path = project / 'AnalysisPartDefaults.db6'
        compressed = path.read_bytes()
        data = gzip.decompress(compressed)
        first, expected, direct_id_matches = independent_layout(data, model_ids)
        assert direct_id_matches == 0, f'unexpected direct DB6/DB1 ID matches: {direct_id_matches}'
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
    print(f'DB6 independent layout/reconstruction, 0 aligned allocation/payload-u32 matches against {len(model_ids)} paired DB1 IDs, and {checks} parser/project checks passed; semantics remain unverified')


if __name__ == '__main__':
    main()
