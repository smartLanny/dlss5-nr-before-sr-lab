"""Build and execute post transport plus the legacy six-shader regression.

No NGX model, game install or Core integration. Use a fresh private output.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

PACKAGE = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--nvidia', action='store_true')
    parser.add_argument('--generated-core', type=Path)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Use an empty directory; previous failures are evidence')
    output.mkdir(parents=True, exist_ok=True)
    sources = {str(p.relative_to(PACKAGE)): sha(p) for p in PACKAGE.rglob('*')
               if p.is_file() and p.suffix in {'.cpp', '.hpp', '.hlsl', '.hlsli', '.py', '.inl'}}
    report = dict(schema='post-final-residual-gpu-v1', source_hashes=sources,
                  compiler_sha256=sha(compiler), commands=[], result='running',
                  scope='FP16 SDR code-value GPU transport; analytic reconstructed carrier; no NR/private SR inference, runtime integration, game or performance claim')

    def save():
        (output/'validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

    def run(command, name, timeout=180):
        try:
            result = subprocess.run(list(map(str, command)), cwd=output, capture_output=True,
                                    timeout=timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            code, log = result.returncode, result.stdout + result.stderr
        except subprocess.TimeoutExpired as error:
            code, log = 124, (error.stdout or b'') + (error.stderr or b'') + b'\nTIMEOUT; not a pass\n'
        path = output/(name+'.log')
        path.write_bytes(log)
        report['commands'].append(dict(name=name, command=list(map(str, command)), exit_code=code,
                                       log=path.name, sha256=sha(path)))
        save()
        print(name, code, log.decode('utf-8', 'replace')[-2200:], flush=True)
        if code:
            raise RuntimeError(name+' failed: '+str(code))

    save()
    try:
        shader_compiler = output/'compile-edit-shader.exe'
        run([compiler, '-std=c++20', '-O2', '-static', '-municode',
             PACKAGE/'tools/compile_edit_shader.cpp', '-ld3dcompiler', '-o', shader_compiler], 'build-shader-compiler')
        shaders = output/'shaders'
        run([sys.executable, PACKAGE/'tools/compile_edit_artifacts.py', '--compiler', shader_compiler,
             '--source', PACKAGE/'shaders', '--output', shaders], 'compile-eight-shaders')
        common = [compiler, '-std=c++20', '-O2', '-static', '-municode', '-I'+str(PACKAGE/'src')]
        libs = ['-ld3d12', '-ldxgi', '-ldxguid', '-ld3dcompiler', '-lole32', '-luuid']
        for name, source in [('legacy', 'windows_shader_validation.cpp'), ('post', 'post_residual_gpu.cpp')]:
            binary = output/(name+'.exe')
            run([*common, PACKAGE/'tests'/source, PACKAGE/'src/nr_edit_dx12.cpp',
                 PACKAGE/'src/nr_post_residual_dx12.cpp', *libs, '-o', binary], 'build-'+name)
            run([binary, shaders], 'warp-'+name)
            if args.nvidia:
                run([binary, shaders, '--nvidia'], 'nvidia-'+name)
        cpu = output/'private-lifecycle.exe'
        run([compiler, '-std=c++20', '-O2', '-static', '-I'+str(PACKAGE/'src'),
             PACKAGE/'tests/runtime_contract_test.cpp', '-o', cpu], 'build-private-lifecycle')
        run([cpu], 'private-lifecycle')
        if args.generated_core:
            run([sys.executable, PACKAGE/'tests/test_integration.py', '--generated-root',
                 args.generated_core.resolve(strict=True)], 'legacy-full-source-staging')
        report['sources_unchanged'] = all(sha(PACKAGE/path) == digest for path, digest in sources.items())
        if not report['sources_unchanged']:
            raise RuntimeError('Sources changed during validation')
        report['result'] = 'passed'
    except Exception as error:
        report['result'] = 'failed'
        report['error'] = str(error)
    report['artifacts'] = {p.name: sha(p) for p in output.iterdir() if p.is_file() and p.name != 'validation.json'}
    save()
    return 0 if report['result'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
