#!/usr/bin/env python3
"""Stage the renamed 0.4.7beta-corefix.4 compatibility candidate without changing the checkout.

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
VERSION = '0.4.7beta-corefix.4'
DISPLAY_VERSION = '0.4.7beta-corefix.4'
CARRIER_NAME = 'dlss5-native-carrier-045-dx11-compat.addon64'
UPSTREAM = '28aed4099b0fe1c207b20b5fee5364c0773c25c2'
PR131 = 'f88adc33d04803982d98336a3d26e7081bb66f9f'
PR132 = '2ab31a17388fb9cfe3c6394397df4cb555b7a172'
PR134 = '145689aee077fa51c54d0b1dba2d79bc38633b2c'
STATE_132 = '5e067f23051cc313a668e91332e82eef9a5bbcf1'
STATE_CURRENT = 'f7af44852dd6c2b39da215b027eaf3f524e06024'
PR137 = '7aa6500ee79deca0689ec85faf2bbb8301d181b7'
REQUIRED_ANCESTORS = (PR131, PR132, PR134, PR137, 'fefd447033b21cf0ae979aa53b7dddc1a8a3e20c', 'c6389f7ea2b7cf865cc23a3bf0afb2268678e3cd')
INTEGRATION_CORE_PINS = {
    'src/nr_timing_feedback.inl': '479daf02b3e915bfa35cb19dfc3620a125265327',
    'src/nr_submitted_fence_wait.inl': '300ae92b81217121790a24429f6b89d93d3aed3f',
    'src/nr_runtime_timing.inl': 'a7ea51edfc3ed345333588cfcec75dc40670e00a',
    'src/nr_runtime_timing.h': 'a92ee6aafad10d096fdec1c3ad963720ce39f847',
    'src/nr_config_policy_runtime.inl': 'e53599a24745bf8addc051b9a85387909931e2eb',
    'src/nr_config_policy.h': '7a01d6c3e35f732b8e62e0264822f03f4385c155',
    'src/nr_probe_policy.h': '79e7971268d8ba0535cb41cb8ae48d0540c9a177',
    'src/nr_post_boundary.h': '4b82c78a4a26c44547cce0089088e57af8daf65e',
    'src/nr_post_boundary_runtime.inl': '0535cbf754d1b95bed1f719353c0b2e63f2a5063',
    'src/nr_ui_activity.h': 'd647d17104c1b5d00ac2769dabde71abfc9ff59f',
    'src/nr_imgui_abi.h': 'd52148f7dc9999c8090947243b9fe6167f457240',
    'src/nr_f8_stream.inl': '8bf90f5ee2e32566428ee07c8fd9ac80491eeac7',
    'src/nr_guide_contract.h': '19c2af89af734af998e63e3f6251679c4b22ce38',
    'src/nr_rr_depth_adapter.inl': 'b606205e654bdcc9107a7266e71af23724377a86',
    'src/nr_rr_depth_shader.h': '2ea485b7cc58c3a1a17fbf636b8d3cbf2607690b',
    'src/nr_rr_depth_projection.h': '8cb9c11b7750935f425e6c1dc2d6786fa804c9ad',
    'src/nr_command_creation_observer.inl': 'fd5d0f4adf50cb1ea3becb6fa9e51f61bf79c828',
    'src/nr_native_command_record.inl': '91b4cf6b2c77396b464b2e28ae0093efd5558a68',
    'config/nr_before_sr.ini': 'a3c0d7f94b663a6b9b341e0ae9fec3b3ffc898d2',
    'scripts/patch-post-compat.py': 'dbf671d5a5f7f8f0edc4e16ba5400fb7fd16dab5',
    'src/nr_codec_descriptors.inl': 'dd468d234a417d41d5db0630a1bff1b97eef16a5',
    'src/nr_feedback_core.inl': '7c61aafa4687613452075334f50e9bb695020313',
    'src/nr_feedback_text.h': '6f3711a2d7be284e40d961fb5eb1632ddd92147e',
    'src/nr_feedback_single_text.h': 'ac9c2a65879a667f6eeecb0560ccae44196b4edb',
    'src/nr_f8_sequence.inl': 'f4b2cd5ca77ea273c5f1dd92c6853a68980a2114',
    'src/nr_command_state.inl': 'f7af44852dd6c2b39da215b027eaf3f524e06024',
    'src/nr_feedback_capture.inl': 'a84efdd55b4eaab01fb4cc71c2cacb9a9f8af3b8',
    'src/nr_runtime_load_evidence.inl': '78e491e9465f4fd3cb453dbcf12a0cfd976ae514',
    'src/nr_settings_ui.inl': 'f1f0f80ee39aee5c70aea13b17f94c270780efb1',
    'src/nr_before_sr.cpp': '9de582eb6afd2e2797ff0b8569cfb26f58ce2e98',
    'src/nr_recommended_picture.inl': 'ec39e98b859f37a4c033d4f08f55c180f616aaf8',
    'src/nr_work_config.h': '2621edc3b84ae83cbdb31ea6056f5b6a5dd81ec6',
    'src/nr_before_sr.rc': '009d8a408f2c13c6f9af353360f9006a6069f576',
    'src/nr_color_policy.inl': '7a7453d479ebf8a9666468d0e287a3c3310697b7',
    'src/nr_color_amplification.h': '6ed7ff9f9b86cbeafed6711f9fe90d602f1497bd',
    'src/nr_precision_ui.h': 'a0ea17f649ee372bf1089e3ad4438414c5b87335',
    'scripts/build.ps1': 'dfe34270569cd110e6cf7bd5a058331d49b3e77a',
    'src/interop/nr_ngx_parameter_abi.h': '0036426de8c9ccb4a9e2b22aaf4d87d3e2e1b038',
    'src/interop/nr_post_settings.inl': 'be18e0f3b12120913bc6f0e38e3084a46fa5fbb2',
    'src/interop/nr_post_ui.inl': '6e24852f73b462828c8a98360e489b2205f74ef9',
    'src/interop/nr_post_feedback.inl': '7f933d86d9a2d0d9f30fe74c4b6be7c6865bba70',
    'src/interop/nr_rr_post_runtime.inl': '23c7ddd0348e0db8438ba17323b2ba4de6db2090',
    'src/interop/nr_rr_depth_evidence.inl': 'eb05e590bc4c29648bd5bac3fa7f81e3fb5b3661',
    'scripts/patch-exposure-access.py': '6edb4379bb65e26f7928c0c0f598b18aa87857c7',
    'scripts/patch-format-access.py': '8d9e6c59470516045253a55cede7965ac3dd0f9f',
    'src/interop/nr_exposure_access_d3d12.inl': 'bb75d0d5294fd96f20e5419f7a9e8d7a74d9fa39',
    'src/nr_compatibility_runtime.inl': '92e8f7c8b32c06c3de5aa8772d52077a4271af31',
    'src/nr_compatibility_verdict.h': 'd49019e6078e4237ff65207f56f5dca3fda0048e',
    'src/nr_compatibility_verdict_feedback.inl': 'e2d233a67c8a7aa6e3aefc99cce596e57e730da3',
    'src/nr_diagnostic_bundle.inl': '3eea8e899aef4f54ab0da40ac758c2afe0a0c10b',
    'src/nr_exposure_semantic_evidence.inl': '405c6e41036bfcc9ac088541cbc9e61831ad7a13',
    'src/nr_feedback_limits.h': 'e22b258f4c26532c41ce090771b2d5e94a80a487',
    'src/nr_feedback_report.inl': '07ac914f64d1361cda0d1b4301077247d05bccfa',
    'src/nr_native_format.h': '6e2ed15171f527c0c8a0c5aeb0e696d0a4aa7d5d',
    'src/nr_native_format_hooks.inl': 'd46fda45d79373fca914e38e211edbeaaa524f87',
    'src/nr_native_format_runtime.inl': 'b56502a40dc201c4f2fa9b5eade9b9e02527031e',
    'src/nr_ngx_lifecycle.inl': '2438ea980f5f8bfdbf56f50eb717851e660dc250',
    'src/nr_post_boundary_native.inl': '28d50ae7e4199674a576424dd133f7632fd4ba2f',
    'src/nr_resource_recovery.h': '00ccad3695ed4bb38016938dfd764242a80122a1',
    'src/nr_resource_recovery_runtime.inl': '3349bc1cb33847cea0010147f4bc35fcf5bd068b',
    'src/nr_resource_recovery_semantics.inl': '63e362fbbd8c959f593d4e4288cde01426fb79de',
    'src/nr_resource_semantics.h': '81874dae21f36985d6b78a9b7e738f2c1d251f99',
    'src/nr_route_compatibility.h': '3e7006e48ea39639bb9a3ea59a6abe5414f9f377',
    'src/nr_route_compatibility_runtime.inl': '7c3b93dc457f896087d179d7d6b9ffe04ff907ad',
    'src/nr_descriptor_provenance.h': '4e9471d5ecf056d15d6474b9b7a5d042991f08af',
    'src/nr_descriptor_provenance_runtime.inl': '148fe3a50968dce4ce44011d8ae00fac12e037c7',
    'src/nr_descriptor_provenance_hooks.inl': '3f797734c2461704144ba2101f6e4801923ce5fc',
    'src/nr_descriptor_provenance_commands.inl': '3010e993e30d5443b0fb8eef870d436acb45a7bd',
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
    if '0.4.7beta-corefix.4' not in resource_text:
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
        'core_pe_version': '0.4.7.9',
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
        'format_path': 'Exact typed/native view -> unique explicit contract and successful device capability -> ambiguous native fallback; physical storage and typed access reported separately',
        'exposure_path': '1x1 mip0 final-scale red; typed R8_UNORM or established R16/R32 FLOAT-family reads with actual Texture2D/ShaderLoad proof; queryAttempted distinguishes missing query from driver failure; no source UAV',
        'resource_recovery': 'same-process normal-carrier retry only after semantics resolve; real descriptor writer, NR/native results, matching Execute and sparse actual stored-value read; failed interpretation latched until genuinely new evidence/lifetime',
        'compatibility_verdict': 'scoped current first block, retained failure and matching attempt/proof; technical proof requires three correlated carriers; final Present visual remains unobserved without separate F7 proof',
        'native_observer': 'layered Create/Evaluate/Release identity; external Feature18 ownership distinct from private NR; gaps remain fail-closed',
        'post_boundary': 'immutable scoped probe window with native barrier/write/submit evidence; no inferred final-screen write or automatic state rewrite',
        'optional_inventory_blocks_on_failure': False,
        'vulkan_native': 'disabled: submit synchronization not implemented',
        'synthetic_legacy': 'not integrated',
        'carrier_filename': CARRIER_NAME,
        'carrier_completion': 'actual GPU progress before DX11 copyback; 500ms frame timeout quarantines resources until restart; no CPU completion signal',
        'carrier_faults': 'NGX hardware exceptions propagate; ordinary error codes retain native fallback',
        'rr_post': 'Feature13 native output; hardware depth or current standard ViewSpace projection converted privately; same NR core; game pending',
        'post_work': '50-100 percent Npost/O; 0 disables while retaining precision; invalid requests rejected',
        'rr_linear_depth': 'positive view-Z standard perspective to private R32 HW depth; matrix-derived inversion; unknown projection stays native with F8',
        'jitter': 'resolved post color zero jitter; native guide phase requires runtime validation',
        'fg_control': 'not implemented', 'getdesc_spoof': 'not implemented',
        'mfg_scope': 'Manager FG/MFG is independent under plan 0838ffb; this Core candidate adds no FG backend',
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
