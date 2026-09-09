"""Execute production NGX lifecycle hooks/registry/gate on real Win32 locks.

The fake native runtime supplies results and nested calls; no D3D/NGX model or
game is claimed. Unlike observer_harness, lifecycle hooks are compiled verbatim.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def definition(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def generate(source: str, feedback_source: str, template: str, negative: str = '', rr_source: str = '') -> str:
    declarations = source[source.index('enum class NgxLayer'):source.index('\nvoid LogUInt(')]
    # The actual module include, FeatureRecord, gate, and registry wrappers stay
    # intact. Only unrelated logging/types and the opaque native runtime are fake.
    types = definition(source, 'enum class ModuleObserverFault') + ';\n'
    types += definition(source, 'enum class BypassReason') + ';\n'
    names = ['CallNativeCreatePassThrough', 'IsUnknownOutermostEvaluateHandle',
             'ObserveNullEvaluateHandle', 'RecordModuleObserverFault',
             'LatchUnknownFeatureHandle', 'FinishFeature1CreateObservation',
             'OtherNrOwnerActive', 'OtherNrOwnerBypassReason',
             'HookCreate', 'HookCoreCreate', 'HookDlssdCreate',
             'DispatchEvaluate', 'CallNativeEvaluatePassThrough',
             'HookEvaluate', 'HookEvaluateC', 'HookCoreEvaluate', 'HookDlssdEvaluate',
             'HookRelease', 'HookCoreRelease', 'HookDlssdRelease']
    functions = [definition(source, 'void DrainNgxFeatureRetirementsLocked(')]
    for name in names:
        prefix = ('NgxResult ' if name.startswith(('Hook', 'Dispatch', 'CallNative'))
                  else 'bool ' if name in {'IsUnknownOutermostEvaluateHandle', 'OtherNrOwnerActive'}
                  else 'BypassReason ' if name == 'OtherNrOwnerBypassReason' else 'void ')
        body = definition(source, prefix + name + '(')
        if negative == 'drop-inner-create' and name in {'HookCreate', 'HookCoreCreate', 'HookDlssdCreate'}:
            body = body.replace('    observed.Finish(result, handle, params);',
                                '    if (!outermost) return result;\n    observed.Finish(result, handle, params);')
        if negative == 'disable-unknown-guard' and name == 'HookCoreEvaluate':
            body = body.replace('g_core_unknown_handle_guard.load(std::memory_order_acquire)', 'false')
        functions.append(body)
    # Compile the exact production admission prefix, up to rendering work.
    process = definition(source, 'NgxResult ProcessEvaluate(')
    process = process[:process.index('    // This is a classified outermost Feature 1 call.')]
    process += '    (void)all; ++plugin_attempts; return original(command_list, handle, params, callback);\n}\n'
    if 'return ProcessRrPost(' in process:
        # Staged Core dispatches classified RR through this actual helper. Keep
        # its non-rendering admission prefix verbatim, with RR work disabled by
        # the fixture. Reaching rendering is an error, never a simulated pass.
        rr = definition(rr_source, 'NgxResult ProcessRrPost(')
        rr = rr[:rr.index('    // RR calls must also train the existing real queue-affinity observer.')]
        rr += '    Check(false,"RR rendering outside lifecycle fixture"); return 0;\n}\n'
        process = rr + '\n' + process
    feedback = definition(feedback_source, 'void FeedbackAppendNgxLifecycle(')
    return (template.replace('// @TYPES@', types)
            .replace('// @DECLARATIONS@', declarations)
            .replace('// @PROCESS@', process)
            .replace('// @HOOKS@', '\n\n'.join(functions))
            .replace('// @FEEDBACK@', feedback))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--source', type=Path, default=ROOT / 'src/nr_before_sr.cpp')
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    paths = {
        'core': args.source,
        'lifecycle': args.source.parent / 'nr_ngx_lifecycle.inl',
        'feedback': args.source.parent / 'nr_feedback_capture.inl',
        'rr_dispatch': args.source.parent / 'interop/nr_rr_post_runtime.inl',
        'harness': ROOT / 'tests/ngx_lifecycle_harness.cpp.in',
        'runner': Path(__file__),
    }
    captured = {name: path.read_bytes() for name, path in paths.items()}
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in captured.items()}
    def git(*command: str) -> str:
        return subprocess.run(['git', '-C', str(ROOT), *command], capture_output=True,
                              text=True, check=True, creationflags=subprocess.CREATE_NO_WINDOW).stdout.strip()
    identity = {'source_commit': git('rev-parse', 'HEAD'),
                'source_dirty': bool(git('status', '--porcelain', '--untracked-files=normal')),
                'tested_source_sha256': hashes,
                'scope': 'production hook/registry/gate/admission-prefix/F8 functions; fake native runtime; real Win32 locks; no GPU or game'}
    (args.output_dir / 'source-identity.json').write_text(json.dumps(identity, indent=2), encoding='utf-8')
    # Freeze the included production module too. No later checkout edit can
    # silently change one variant's compiled registry relative to another.
    (args.output_dir / 'nr_ngx_lifecycle.inl').write_bytes(captured['lifecycle'])
    source = captured['core'].decode('utf-8-sig')
    feedback_source = captured['feedback'].decode('utf-8-sig')
    template = captured['harness'].decode('utf-8-sig')
    results = []
    for variant in ['', 'drop-inner-create', 'disable-unknown-guard']:
        leaf = variant or 'production'
        cpp = args.output_dir / (leaf + '.cpp')
        exe = args.output_dir / (leaf + '.exe')
        cpp.write_text(generate(source, feedback_source, template, variant,
                                captured['rr_dispatch'].decode('utf-8-sig')), encoding='utf-8')
        command = [args.compiler, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror',
                   '-static', '-I' + str(args.source.parent), str(cpp), '-o', str(exe)]
        built = subprocess.run(command, capture_output=True, text=True,
                               creationflags=subprocess.CREATE_NO_WINDOW)
        (args.output_dir / (leaf + '-build.log')).write_text(built.stdout + built.stderr, encoding='utf-8')
        if built.returncode:
            print(built.stdout + built.stderr)
            raise SystemExit(built.returncode)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        (args.output_dir / (leaf + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        print(result.stdout + result.stderr, end='')
        if variant:
            expected = 'INNER_CREATE_OBSERVED' if variant == 'drop-inner-create' else 'UNKNOWN_REMAINS_CLOSED'
            if result.returncode != 1 or expected not in result.stderr:
                raise SystemExit('Negative control missed its specific regression: ' + variant)
            print('PASS negative control:', variant)
        elif result.returncode:
            raise SystemExit(result.returncode)
        results.append({'variant': leaf, 'command': command, 'exit_code': result.returncode,
                        'expected_negative': bool(variant),
                        'generated_sha256': hashlib.sha256(cpp.read_bytes()).hexdigest(),
                        'binary_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()})
    if any(path.read_bytes() != captured[name] for name, path in paths.items()):
        raise SystemExit('Source changed during lifecycle validation; results are not a frozen-source pass')
    (args.output_dir / 'validation.json').write_text(
        json.dumps({'status': 'PASS', 'identity': identity, 'results': results}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
