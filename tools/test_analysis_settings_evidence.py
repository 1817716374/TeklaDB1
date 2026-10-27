#!/usr/bin/env python3
"""Independent literal-value comparison for the native analysis preset."""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import corpus


def oracle(data):
    result = []
    lines = data.splitlines()
    assert lines[0] == b'encoding 1252'
    def literal(raw):
        text = raw.decode('cp1252')
        if text.startswith('"'):
            assert text.endswith('"') and '"' not in text[1:-1]
            return 's', text[1:-1].encode('utf-8').hex()
        if re.fullmatch(r'[+-]?\d+', text):
            return 'i', str(int(text))
        return 'd', f'{struct.unpack("<Q", struct.pack("<d", float(text)))[0]:x}'
    for number, raw in enumerate(lines[1:], 2):
        if not raw.strip():
            continue
        if b';' in raw and raw.split(b';', 1)[0].endswith(b'Table'):
            table, engine, field, token = raw.split(b';', 3)
            kind, value = literal(token)
            result.append('\t'.join(('D', table.decode('ascii'), engine.decode('cp1252').encode('utf-8').hex(), field.decode('ascii'), str(number), token.hex(), kind, value)))
        else:
            key, token = raw.split(None, 1)
            kind, value = literal(token)
            result.append('\t'.join(('P', key.decode('ascii'), str(number), token.hex(), kind, value)))
    result.append('R\t' + data.hex())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--download', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / 'tests/corpus.json').read_text(encoding='utf-8'))
    specs = [f for f in manifest['files'] if f['path'].startswith('analysis-settings/') or f['path'] == 'analysis-defaults/AnalysisPartDefaults.db6']
    assert len(specs) == 3
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
            assert p.returncode == 0, (mode, p.stderr)
            if isinstance(expected, list):
                assert p.stdout.splitlines() == expected, (mode, 'field comparison failed')
            else:
                assert p.stdout.strip() == expected, (mode, p.stdout, expected)
        checks += 1
    with tempfile.TemporaryDirectory(prefix='admodel-', dir=args.work.resolve()) as temp:
        project = Path(temp)
        for spec in specs:
            if spec['path'].startswith('analysis-settings/'):
                destination = project / spec['path'].removeprefix('analysis-settings/')
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(corpus.safe_path(args.data, spec['path']), destination)
        path = project / 'attributes/Click here to see A_D wire frame.admodel'
        original = path.read_bytes()
        rows = oracle(original)
        assert sum(r.startswith('P\t') for r in rows) == 125
        assert sum(r.startswith('D\t') for r in rows) == 3
        run('analysis_settings_fields', path, rows)
        baseline = 'files=1 parsed=1 failed=0 discovered=0 fields=125 designs=3 associations=0'
        disabled = 'files=1 parsed=0 failed=0 discovered=1 fields=0 designs=0 associations=0'
        run('analysis_settings_project', project, baseline)
        run('analysis_settings_project_strict', project, baseline)
        run('analysis_settings_project_disabled', project, disabled)
        run('analysis_settings_project_raw_disabled', project, baseline)
        upper = project / 'AtTrIbUtEs'
        (project / 'attributes').rename(project / 'attributes-intermediate')
        (project / 'attributes-intermediate').rename(upper)
        path = upper / path.name
        run('analysis_settings_project', project, baseline)
        target = project / 'renamed.ADMODEL'
        path.rename(target)
        path = target
        run('analysis_settings_project', project, baseline)
        for old, new in ((b'ModelName "Model 1"', b'ModelName "caf\xe9;\x80"'),
                         (b'ModeCount 6', b'ModeCount 8'),
                         (b'AccuracyOfIteration 0.001000', b'AccuracyOfIteration 0.012500'),
                         (b'SteelDesignTable;NeutralFileIOLib;code;0', b'SteelDesignTable;NeutralFileIOLib;code;5')):
            changed = original.replace(old, new)
            assert changed != original
            path.write_bytes(changed)
            run('analysis_settings_fields', path, oracle(changed))
        changed = original + b'ModelName "duplicate"\r\nSteelDesignTable;NeutralFileIOLib;code;9\r\n'
        path.write_bytes(changed)
        # API groups the two record kinds while retaining per-record line numbers.
        expected = oracle(changed)
        expected = [r for r in expected if r.startswith('P\t')] + [r for r in expected if r.startswith('D\t')] + [expected[-1]]
        run('analysis_settings_fields', path, expected)
        run('analysis_settings_project', project, 'files=1 parsed=1 failed=0 discovered=0 fields=126 designs=4 associations=0')
        for old, new in ((b'encoding 1252', b'encoding 65001'),
                         (b'ModeCount 6', b'ModeCount 9223372036854775808'),
                         (b'AccuracyOfIteration 0.001000', b'AccuracyOfIteration nan'),
                         (b'AccuracyOfIteration 0.001000', b'AccuracyOfIteration 1e-9999'),
                         (b'ModelName "Model 1"', b'ModelName "unterminated'),
                         (b'ModelName "Model 1"', b'ModelName "\x81"'),
                         (b'SteelDesignTable;NeutralFileIOLib;code;0', b'SteelDesignTable;;code;0')):
            changed = original.replace(old, new)
            assert changed != original
            path.write_bytes(changed)
            run('analysis_settings_fields', path, fail=True)
        run('analysis_settings_project', project, 'files=1 parsed=0 failed=1 discovered=0 fields=0 designs=0 associations=0')
        run('analysis_settings_project_strict', project, fail=True)
        run('analysis_settings_project_disabled', project, disabled)
        # Deliberately give a preset a matching DB6 filename/model name/engine.
        # This combination comes from different source projects, so it must not
        # create an inferred application or identity link.
        changed = original.replace(b'ModelName "Model 1"', b'ModelName "AnalysisPartDefaults"').replace(b'AnalysisEngine "NeutralFileIOLib"', b'AnalysisEngine "XStaad"')
        path.write_bytes(changed)
        target = project / 'AnalysisPartDefaults.admodel'
        path.rename(target)
        shutil.copyfile(corpus.safe_path(args.data, 'analysis-defaults/AnalysisPartDefaults.db6'), project / 'AnalysisPartDefaults.db6')
        run('analysis_settings_project', project, baseline)
        run('analysis_project', project, 'analysis_files=1 raw=1 failed=0 discovered=0 rows=79 associations=0')
        target.write_bytes(original)
        run('analysis_settings_fields', target, rows)
    print(f'125 native fields and 3 design settings independently matched; {checks} parser/project checks passed')


if __name__ == '__main__':
    main()
