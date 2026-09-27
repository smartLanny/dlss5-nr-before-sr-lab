#!/usr/bin/env python3
"""Build the private SR objects and eight real SM5 shaders for a generated Core."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'compiler', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'schema': 'private-sr-module-v2', 'passed': False,
              'objects': [], 'source_sha256': {}, 'commands': [],
              'gpu_execution': 'not-run'}

    def save() -> None:
        (out / 'module.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def run(name: str, command: list[str]) -> None:
        item = {'name': name, 'command': command, 'exit': None}
        report['commands'].append(item)
        save()
        result = subprocess.run(command, capture_output=True, text=True, errors='replace')
        log = out / (name + '.log')
        log.write_text(result.stdout + result.stderr, encoding='utf-8')
        item.update({'exit': result.returncode, 'log': str(log), 'log_sha256': sha(log)})
        save()
        if result.returncode:
            raise RuntimeError(name + ' failed: ' + (result.stdout + result.stderr)[-4000:])

    save()  # Invalidate any previous successful receipt before resolving inputs.
    try:
        root = args.root.resolve(strict=True)
        compiler = args.compiler.resolve(strict=True)
        package = root / 'experiments/nr-edit'
        src = package / 'src'
        shader_dir = package / 'shaders'
        helper = package / 'tools/compile_edit_shader.cpp'
        generator = package / 'tools/compile_edit_artifacts.py'
        units = [src / (name + '.cpp') for name in
                 ('nr_edit_dx12', 'nr_post_residual_dx12', 'nr_post_private_sr_dx12')]
        units.append(root / 'src/nr_private_sr_guides.cpp')
        inputs = set(units + [helper, generator, root / 'src/nr_private_sr_guides.h',
                             root / 'src/nr_guide_contract.h', root / 'src/nr_private_sr_artifacts.h'])
        inputs.update(src.glob('*.hpp'))
        inputs.update(src.glob('*.h'))
        inputs.update((root / 'src/private_sr').glob('*.hpp'))
        inputs.update(shader_dir.glob('*.hlsl'))
        inputs.update(shader_dir.glob('*.hlsli'))
        report.update({'root': str(root), 'compiler': str(compiler),
                       'compiler_sha256': sha(compiler),
                       'builder_sha256': sha(Path(__file__).resolve()),
                       'source_sha256': {p.relative_to(root).as_posix(): sha(p)
                                         for p in sorted(inputs)}})
        run('compiler-identity', [str(compiler), '--version'])
        if 'clang' not in (out / 'compiler-identity.log').read_text(encoding='utf-8').lower():
            raise RuntimeError('An LLVM clang compiler is required for the Core ABI')
        run('compiler-target', [str(compiler), '-dumpmachine'])
        target = (out / 'compiler-target.log').read_text(encoding='utf-8').strip()
        if not target.startswith('x86_64-') or not any(x in target for x in ('mingw', 'windows-gnu', 'w64')):
            raise RuntimeError('Expected x86_64 LLVM-MinGW target, got ' + target)
        report['compiler_target'] = target
        common = [str(compiler), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror']
        shader_compiler = out / 'compile-edit-shader.exe'
        shader_compiler.unlink(missing_ok=True)
        run('build-shader-compiler', common + ['-static', '-municode', str(helper),
                                              '-ld3dcompiler', '-o', str(shader_compiler)])
        bytecode = out / 'bytecode'
        run('build-shaders', [sys.executable, str(generator), '--compiler', str(shader_compiler),
                              '--source', str(shader_dir), '--output', str(bytecode)])
        receipt_path = bytecode / 'edit-shader-manifest.json'
        receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
        expected = {'rr_guides', 'rr_accumulate', 'rr_compose', 'deferred_encode',
                    'deferred_compose', 'normalize', 'post_extract', 'post_compose'}
        if set(receipt['entries']) != expected or receipt['compiler_sha256'] != sha(shader_compiler):
            raise RuntimeError('Unexpected shader set or shader compiler identity')
        shaders = {}
        for name, entry in receipt['entries'].items():
            path = bytecode / (name + '.cso')
            if sha(path) != entry['dxbc_sha256'] or path.read_bytes()[:4] != b'DXBC':
                raise RuntimeError('Shader hash/container mismatch: ' + name)
            shaders[name] = {'path': str(path), 'sha256': sha(path), 'entry': entry['entry']}
        header = bytecode / 'nr_edit_binaries.hpp'
        text = header.read_text(encoding='utf-8')
        anchor = '#include "../src/nr_edit_dx12.hpp"'
        if text.count(anchor) != 1:
            raise RuntimeError('Generated header include contract changed')
        header.write_text(text.replace(anchor, '#include "nr_edit_dx12.hpp"'), encoding='utf-8')
        report.update({'header': str(header), 'header_sha256': sha(header),
                       'shaders': shaders, 'shader_manifest': str(receipt_path),
                       'shader_manifest_sha256': sha(receipt_path)})
        # Keep build-directory bytecode out of the Core translation unit. This
        # generated definition is hashed and linked as a separate module object.
        artifacts_source = out / 'artifacts.cpp'
        artifacts_source.write_text(
            '#include "' + (root / 'src/nr_private_sr_artifacts.h').as_posix() + '"\n'
            '#include "nr_edit_binaries.hpp"\n'
            'namespace nr_private_sr_artifacts {\n'
            'std::array<nr_edit_dx12::Bytecode, static_cast<unsigned>(nr_edit_dx12::Kernel::Count)> Kernels() {\n'
            '    auto kernels = nr_edit_binaries::PostKernels();\n'
            '    kernels[static_cast<unsigned>(nr_edit_dx12::Kernel::NormalizeGuides)] = nr_edit_binaries::Normalize();\n'
            '    return kernels;\n'
            '}\n}\n', encoding='utf-8')
        report['generated_source_sha256'] = {str(artifacts_source): sha(artifacts_source)}
        units.append(artifacts_source)
        for source in units:
            obj = out / (source.stem + '.o')
            obj.unlink(missing_ok=True)
            run('build-' + source.stem, common + ['-I', str(root / 'src/private_sr'),
                 '-I', str(src), '-I', str(root / 'src'),
                 '-I', str(bytecode), '-c', str(source), '-o', str(obj)])
            report['objects'].append(str(obj))
        report['object_sha256'] = {p: sha(Path(p)) for p in report['objects']}
        if any(sha(root / path) != digest for path, digest in report['source_sha256'].items()):
            raise RuntimeError('Module source changed during compilation')
        if any(sha(Path(path)) != digest for path, digest in report['generated_source_sha256'].items()):
            raise RuntimeError('Generated artifact definition changed during compilation')
        report['passed'] = True
        save()
        print('Private SR module: five objects and eight SM5 shaders built; no GPU execution.')
    except Exception as error:
        report['passed'] = False
        report['error'] = str(error)
        save()
        raise


if __name__ == '__main__':
    main()
