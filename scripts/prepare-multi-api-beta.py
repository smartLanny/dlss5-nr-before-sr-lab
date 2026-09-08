#!/usr/bin/env python3
"""Stage the merged 0.4.6 compatibility candidate without changing the checkout.

Requires #131, #132 and the completed #134 merge. Generated-source availability
is not game verification. MFG work is deferred for the current owner test batch.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = 'beta0.5-dline1'
DISPLAY_VERSION = '0.5-dline1'
CARRIER_NAME = 'dlss5-native-carrier-045-dx11-compat.addon64'
UPSTREAM = '28aed4099b0fe1c207b20b5fee5364c0773c25c2'
PR131 = 'f88adc33d04803982d98336a3d26e7081bb66f9f'
PR132 = '2ab31a17388fb9cfe3c6394397df4cb555b7a172'
PR134 = '145689aee077fa51c54d0b1dba2d79bc38633b2c'
STATE_132 = '5e067f23051cc313a668e91332e82eef9a5bbcf1'
STATE_CURRENT = 'f0f13b0a8ac243f01cdf0f93a517440ac65f1394'
PR137 = '7aa6500ee79deca0689ec85faf2bbb8301d181b7'
REQUIRED_ANCESTORS = (PR131, PR132, PR134, PR137, 'fefd447033b21cf0ae979aa53b7dddc1a8a3e20c', 'c6389f7ea2b7cf865cc23a3bf0afb2268678e3cd')
INTEGRATION_CORE_PINS = {
    'config/nr_before_sr.ini': '4324bab8be0b48792b1a001077b1517335d1715a',
    'scripts/patch-post-compat.py': '0542d143584a8eb56364a54c2ad8daae46fcc9c3',
    'src/nr_codec_descriptors.inl': '353c97c74bde56c72cda349fca2e50118000cbcb',
    'src/nr_feedback_core.inl': '2e2b54d62c09befdd3ba76cdc75f087225d38d5d',
    'src/nr_feedback_text.h': '2eb84a38dca9c72243bc2a4a0aa84007ec75097f',
    'src/nr_feedback_single_text.h': 'ac9c2a65879a667f6eeecb0560ccae44196b4edb',
    'src/nr_f8_sequence.inl': 'f4b2cd5ca77ea273c5f1dd92c6853a68980a2114',
    'src/nr_command_state.inl': 'f0f13b0a8ac243f01cdf0f93a517440ac65f1394',
    'src/nr_feedback_capture.inl': '99328d273b964509e344bfdbcd7bbc85f230619b',
    'src/nr_runtime_load_evidence.inl': '78e491e9465f4fd3cb453dbcf12a0cfd976ae514',
    'src/nr_settings_ui.inl': 'f15c6a34c6691c91bf3adbf45202a1c6a5c6dfc4',
    'src/nr_before_sr.cpp': '087dae326cb94aae611d199ff80c7548482048c0',
    'src/nr_recommended_picture.inl': '76cb0b36303366350ac2c85e26de850697ae27de',
    'src/nr_work_config.h': '2621edc3b84ae83cbdb31ea6056f5b6a5dd81ec6',
    'src/nr_before_sr.rc': '19fd691572c8f7c778671dcb0b3a5b9a73b9e4b1',
    'src/nr_color_policy.inl': '84d550d4ac39ae42f82abbabb2f46de7eebf144e',
    'src/nr_color_amplification.h': '1d386e71884c31093de3cb66f7273f8f43d74d90',
    'src/nr_precision_ui.h': 'a0ea17f649ee372bf1089e3ad4438414c5b87335',
    'scripts/build.ps1': '14d4db1979beb73fdd144aa380812a4336379585',
    'src/interop/nr_ngx_parameter_abi.h': '0036426de8c9ccb4a9e2b22aaf4d87d3e2e1b038',
    'src/interop/nr_post_settings.inl': 'be18e0f3b12120913bc6f0e38e3084a46fa5fbb2',
    'src/interop/nr_post_ui.inl': '250a7df38eb17a3ac36cc1055c5b4d134ed89d8f',
    'src/interop/nr_post_feedback.inl': '58f217a2c3d83d5af19cfdc198deaf9226e19488',
    'src/interop/nr_rr_post_runtime.inl': '6f8c98ca7943233f57e10276dbe9943631c9dd05',
    'src/interop/nr_rr_depth_evidence.inl': 'eb05e590bc4c29648bd5bac3fa7f81e3fb5b3661',
}
BRIDGE_BLOBS = {
    'src/bridge.inc': '244e3ae373b33b40763dc36e8ce967e152cbb505',
    'src/dlss5-bridge.cpp': 'f1337f31cfe3b42a950cb3df2fc83d2190ec5506',
    'src/vkmirror.inc': '8f21f435262b627b8445c215bc975e96205d6312',
}

def git(root: Path, *args: str) -> bytes:
    return subprocess.run(['git', '-C', str(root), *args], check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError(f'Cannot load source adapter {path.name}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def is_ancestor(root: Path, commit: str) -> bool:
    return subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor',
        commit, 'HEAD'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0

def configure(core: Path):
    for ancestor in REQUIRED_ANCESTORS:
        if not is_ancestor(core, ancestor):
            raise ValueError(f'Missing integration ancestor {ancestor}; merge, do not copy its files')
    # Resolve all reviewed overrides before exposing a configured transformer.
    # The library retains its original pins as a historical standalone fixture.
    for path, expected in INTEGRATION_CORE_PINS.items():
        actual = git(core, 'rev-parse', 'HEAD:' + path).decode().strip()
        if actual != expected:
            raise ValueError(f'Integrated source changed: {path}; review and repin, do not restore old UI')
    m = load('bridge_045_prerequisite', core / 'scripts/prepare-native-bridge-045.py')
    fmt = load('format_patch', core / 'scripts/patch-format-access.py')
    post = load('post_patch', core / 'scripts/patch-post-compat.py')
    m.EXPERIMENT = m.legacy.EXPERIMENT = VERSION
    m.VERSION = VERSION
    m.legacy.UPSTREAM = UPSTREAM
    m.legacy.UPSTREAM_BLOBS = dict(BRIDGE_BLOBS)
    m.legacy.CORE_BLOBS.update(INTEGRATION_CORE_PINS)
    prior_patch = m.patch_core
    prior_ui = m.legacy.patch_ui
    def patch_core(text: str) -> str:
        return post.patch_core(fmt.patch_core(prior_patch(text)))
    m.legacy.patch_core = patch_core
    m.legacy.patch_ui = lambda text: post.patch_ui(prior_ui(text))
    previous_prepare = m.prepare
    def prepare_with_native_abi(core_root: Path, upstream: Path, stage: Path) -> None:
        previous_prepare(core_root, upstream, stage)
        abi = load('native_parameter_abi', core_root / 'scripts/patch-ngx-parameter-abi.py')
        abi.apply(stage)
        exposure = load('exposure_adapter', core_root / 'scripts/patch-exposure-access.py')
        exposure.apply(stage)
        completion = load('carrier_completion', core_root / 'scripts/patch-carrier-completion.py')
        completion.apply(stage)
    m.prepare = prepare_with_native_abi
    return m

def file_hashes(root: Path) -> dict[str, str]:
    result = {}
    for p in sorted(root.rglob('*')):
        if p.is_symlink():
            raise ValueError('Generated sources must not contain symlinks')
        if p.is_file() and p != root / 'source-manifest.json':
            result[p.relative_to(root).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    return result

def finalize(core: Path, stage: Path, source: str) -> None:
    runtime = stage / 'core/src/nr_before_sr.cpp'
    runtime_text = runtime.read_text(encoding='utf-8')
    old_rr_meter = 'if (signature.rr_bypass) return "A1-native-RR-bypass";'
    if runtime_text.count(old_rr_meter) != 1:
        raise ValueError('RR cadence label anchor changed')
    runtime.write_text(runtime_text.replace(old_rr_meter,
        'if (signature.rr_bypass) return "A1-RR-owner-observed";'),
        encoding='utf-8', newline='\n')
    names = stage / 'core/src/interop/nr_deployment_names.h'
    text = names.read_text(encoding='utf-8')
    old = ('return Equal(name, L"dlss5-native-carrier-045-exp3.addon64") ||\n'
           '        Equal(name, L"dlss5-native-carrier-multiapi-dev3.addon64") ||\n'
           '        Equal(name, L"dlss5-native-carrier-045-dx11-compat.addon64");')
    if text.count(old) != 1:
        raise ValueError('Companion identity anchor changed')
    names.write_text(text.replace(old,
        f'return Equal(name, L"{CARRIER_NAME}");'),
        encoding='utf-8', newline='\n')
    deployment = stage / 'core/src/interop/nr_dx11_deployment.inl'
    text = deployment.read_text(encoding='utf-8')
    old = ('return nr_dx11_deployment::duplicate_carriers.load(std::memory_order_acquire) ||\n'
           '        !nr_dx11_deployment::Read().complete;')
    if text.count(old) != 1:
        raise ValueError('Deployment policy anchor changed')
    deployment.write_text(text.replace(old,
        'return nr_dx11_deployment::duplicate_carriers.load(std::memory_order_acquire);'),
        encoding='utf-8', newline='\n')
    post = load('post_finalize', core / 'scripts/patch-post-compat.py')
    feedback = stage / 'core/src/nr_feedback_capture.inl'
    feedback_text = feedback.read_text(encoding='utf-8')
    activity = 'void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {'
    if feedback_text.count(activity) != 1:
        raise ValueError('Post feedback activity anchor changed')
    feedback.write_text(feedback_text.replace(activity,
        activity + '\n#include "interop/nr_post_feedback.inl"', 1), encoding='utf-8', newline='\n')
    descriptors = stage / 'core/src/nr_codec_descriptors.inl'
    descriptors.write_text(post.patch_descriptors(descriptors.read_text(encoding='utf-8')),
                           encoding='utf-8', newline='\n')
    rc = stage / 'core/src/nr_before_sr.rc'
    resource_text = rc.read_text(encoding='utf-8')
    if '0.5.1 dline1 beta' not in resource_text:
        raise ValueError('Missing expected inherited PE description')
    rc.write_text(resource_text,
                  encoding='utf-8', newline='\n')
    ini = stage / 'core/config/nr_before_sr.ini'
    with ini.open('a', encoding='utf-8', newline='\n') as stream:
        stream.write('\n; NR work after SR/RR, not game render scale.\nRRPostEnabled=1\nPostWorkPercent=100\n')
    mf = stage / 'source-manifest.json'
    manifest = json.loads(mf.read_text(encoding='utf-8'))
    manifest['integration'] = {
        'version': VERSION, 'display_version': DISPLAY_VERSION, 'source_commit': source,
        'core_pe_version': '0.5.1.1',
        # Retain the original dual-parent field for existing artifact readers.
        'parents_in_history': [PR131, PR132],
        'required_ancestors': list(REQUIRED_ANCESTORS),
        'picture_pr134_ancestor': PR134,
        'picture_source_pins': INTEGRATION_CORE_PINS,
        'bridge_release': 'v1.4.12', 'bridge_commit': UPSTREAM,
        'ngx_parameter_abi': 'Microsoft x64 native 17-slot layout; MSVC and Windows-GNU declarations',
        'reset_132_source_blob': STATE_132, 'reset_source_blob': STATE_CURRENT,
        'feedback': 'F8 single TXT; F7 optional paired archive',
        'post_feedback': 'requested state and sampled RR counters; not GPU completion; same TXT',
        'blend_feedback': 'four observed native blend factors from the same locked state sample; explicit unobserved/unavailable',
        'format_path': 'typed FP16/FP32 SceneColor read -> existing FP16 codec',
        'exposure_path': '1x1 mip0 final-scale red channel; typed R16/R32 FLOAT-family view and actual device ShaderLoad; no source UAV requirement',
        'optional_inventory_blocks_on_failure': False,
        'vulkan_native': 'disabled: submit synchronization not implemented',
        'synthetic_legacy': 'not integrated',
        'carrier_filename': CARRIER_NAME,
        'carrier_completion': 'actual GPU progress before DX11 copyback; 500ms frame timeout quarantines resources until restart; no CPU completion signal',
        'carrier_faults': 'NGX hardware exceptions propagate; ordinary error codes retain native fallback',
        'rr_post': 'Feature13 successful native output, known hardware depth, same NR core; not game verified',
        'post_work': '50-100 percent Npost/O; 0 disables while retaining precision; invalid requests rejected',
        'rr_linear_depth': 'not converted; native RR output retained with F8 feedback reason',
        'jitter': 'resolved post color zero jitter; native guide phase requires runtime validation',
        'fg_control': 'not implemented', 'getdesc_spoof': 'not implemented',
        'mfg_scope': 'Manager FG/MFG is independent under plan 0838ffb; this 0.5 candidate adds no FG backend',
        'game_validation': 'owner will test final batch; old BG3 result retained separately',
    }
    manifest['files'] = file_hashes(stage)
    mf.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    if json.loads(mf.read_text(encoding='utf-8'))['files'] != file_hashes(stage):
        raise ValueError('Generated manifest mismatch')

def prepare(core: Path, upstream: Path, output: Path) -> None:
    if output.exists():
        raise ValueError('Output already exists')
    if git(core, 'status', '--porcelain', '--untracked-files=no').strip():
        raise ValueError('Commit reviewed source changes first')
    if git(upstream, 'rev-parse', 'HEAD').decode().strip() != UPSTREAM:
        raise ValueError('Upstream is not the pinned v1.4.12 commit')
    source = git(core, 'rev-parse', 'HEAD').decode().strip()
    m = configure(core)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nr-integration-', dir=output.parent) as tmp:
        stage = Path(tmp) / 'candidate'
        m.prepare(core, upstream, stage)
        finalize(core, stage, source)
        if git(core, 'rev-parse', 'HEAD').decode().strip() != source or \
                git(core, 'status', '--porcelain', '--untracked-files=no').strip():
            raise ValueError('Source changed during generation')
        if output.exists():
            raise ValueError('Output appeared during generation')
        m.legacy.publish_stage(stage, output)

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--core', type=Path, default=ROOT)
    ap.add_argument('--upstream', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    prepare(args.core.resolve(), args.upstream.resolve(), args.output.resolve())
    print(f'Staged {VERSION}: source only, not game-verified')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
