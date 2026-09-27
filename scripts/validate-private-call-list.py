"""Exact-source CPU/COM regression for private NGX call-list identity."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(ok: bool, reason: str) -> None:
    if not ok:
        raise ValueError(reason)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    root, out = a.root.resolve(strict=True), a.output.resolve()
    require(not out.exists(), 'Preserve previous evidence; choose a new --output')
    resolver = root / 'src/nr_private_sr_call_list.inl'
    fixture = root / 'tests/private_sr_call_list_test.cpp'
    adapter = root / 'experiments/nr-edit/src/nr_private_sr_core_adapter.inl'
    product = root / 'src/nr_private_sr_product.inl'
    overlay = root / 'scripts/patch-private-sr.py'
    for file in (resolver, fixture, adapter, product, overlay):
        require(file.is_file(), 'Missing expected source: ' + str(file))
    a_text, p_text, o_text = adapter.read_text(encoding='utf-8-sig'), product.read_text(encoding='utf-8-sig'), overlay.read_text(encoding='utf-8-sig')
    create = a_text.split('a.create=[](', 1)[1].split('a.evaluate=[](', 1)[0]
    evaluate = a_text.split('a.evaluate=[](', 1)[1].split('a.release=[](', 1)[0]
    for name, body, call in (('Create', create, '->create(call_list,1,'),
                             ('Evaluate', evaluate, '->evaluate(call_list,')):
        require(body.count('NrPrivateOriginalCallList(') == 1 and
                'if(!call_list)return 0xBAD00005u;' in body and call in body,
                name + ' must revalidate call list and use it only for original NGX')
    require(p_text.count('if(!NrPrivateOriginalCallList(list)){Refuse(State::OriginalInterface);') >= 2,
            'Capture and Finish must refuse missing current-call proof')
    require("'src/nr_private_sr_call_list.inl'" in o_text and
            '#include "nr_private_sr_call_list.inl"' in p_text,
            'Exact resolver must be staged and included in generated Core')
    out.mkdir(parents=True, exist_ok=False)
    binary = out / 'private_sr_call_list_test.exe'
    commands = [
        ('compile', [str(a.compiler), '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', '-static',
                     '-I' + str(root / 'src'), str(fixture), '-o', str(binary)]),
        ('run', [str(binary)]),
    ]
    runs = []
    for name, cmd in commands:
        r = subprocess.run(cmd, text=True, capture_output=True, timeout=90)
        (out / (name + '.log')).write_text(r.stdout + r.stderr, encoding='utf-8')
        runs.append({'name': name, 'exit': r.returncode, 'output': r.stdout + r.stderr})
        if r.returncode != 0:
            break
    report = {'passed': len(runs) == 2 and all(r['exit'] == 0 for r in runs),
              'source_sha256': {str(file): sha(file) for file in (resolver, fixture, adapter, product, overlay)},
              'structural_checks': ['Capture/Finish current-call preflight', 'Create current-call resolver + original NGX',
                                    'Evaluate current-call resolver + original NGX', 'generated Core staging'],
              'runs': runs, 'scope': 'Exact resolver included by CPU COM double; no GPU, game or real NGX inference'}
    (out / 'validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(runs[-1]['output'])
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
