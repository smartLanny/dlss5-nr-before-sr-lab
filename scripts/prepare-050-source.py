"""One-shot preparation only. Publishes blobs/tree, never commits or moves refs.
This file and its workflow are excluded from the resulting product tree.
"""
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request

BASE = 'd42a8eb23778bc602d0d638193872d41216c2203'
BASE_TREE = '58806734edef7fb32191c0f2c61e78ad3593e858'
REPO = 'smartLanny/dlss5-nr-before-sr-lab'
assert os.environ['GITHUB_REPOSITORY'] == REPO
assert os.environ['GITHUB_REF'] == 'refs/heads/codex/050-high-strength-preparation'
changes = {}

def read(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path]).decode('utf-8')

def once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError('Expected exactly one anchor: ' + repr(before[:130]))
    return text.replace(before, after, 1)

def save(path, text):
    changes[path] = text
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(text, encoding='utf-8', newline='\n')

source = read('src/nr_before_sr.cpp')
data = source.encode('utf-8')
assert hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() == 'abaf4eec0c5da804712466674ef2604196a347f4'
source = once(source,
    '    bool motion_stability = false; // Requested only; requires a proven depth/history contract.',
    '    bool high_strength_protection = true; // Mode 2 composition only; does not reset NR history.\n    bool motion_stability = false; // Requested only; requires a proven depth/history contract.')
source = once(source, '           a.detail_stability == b.detail_stability &&',
    '           a.detail_stability == b.detail_stability &&\n           a.high_strength_protection == b.high_strength_protection &&')
source = once(source, '    next.detail_stability = ReadIniFloat(L"DetailStability", 0.0f);',
    '    next.detail_stability = ReadIniFloat(L"DetailStability", 0.0f);\n    next.high_strength_protection = GetPrivateProfileIntW(\n        L"NRBeforeSR", L"HighStrengthProtection", 1, g_ini_path) != 0;')
source = once(source, '    record_write(WriteIniFloat(L"DetailStability", config.detail_stability));',
    '    record_write(WriteIniFloat(L"DetailStability", config.detail_stability));\n    record_write(WriteIniInt(L"HighStrengthProtection", config.high_strength_protection ? 1 : 0));')
source = once(source, '        Log("Config: enabled=%d mode=%d identity=%d preTransfer=%.3f "',
    '        Log("HighStrengthProtection requested=%d; effective only in Mode 2 above 1x; codec flag=1024.",\n            g_config.high_strength_protection ? 1 : 0);\n        Log("Config: enabled=%d mode=%d identity=%d preTransfer=%.3f "')
source = once(source, '    Log("Config updated from %s: enabled=%d nrBeforeSr=%d transfer=%.3f "',
    '    Log("HighStrengthProtection requested=%d; effective only in Mode 2 above 1x; codec flag=1024.",\n        applied.high_strength_protection ? 1 : 0);\n    Log("Config updated from %s: enabled=%d nrBeforeSr=%d transfer=%.3f "')
# Reserve an unused flag instead of changing the root-constant/descriptor ABI.
assert '1024u' not in source[source.index('const char kCodecShader'):source.index('ID3DBlob *CompileCodecShader')]
source = once(source, '    const unsigned int base_codec_flags = source_flags |',
    '    const unsigned int base_codec_flags = source_flags |\n        ((v2_combine && g_config.high_strength_protection) ? 1024u : 0u) |')
helper = '''// V3_BOOST_SCALAR_BEGIN
// Preserve the one-times anchor. Only the extra edit approaches the remaining
// budget asymptotically; no extra magnitude is invented or sign reversed.
float SoftExtraMagnitudeV3(float base, float extraStrength, float limit) {
    base = min(max(base, 0.0), max(limit, 0.0));
    float room = max(limit - base, 0.0);
    float extra = base * max(extraStrength, 0.0);
    if (room <= 0.0 || extra <= 0.0) return base;
    return base + room * (extra / (room + extra));
}
// V3_BOOST_SCALAR_END

'''
source = once(source, '[numthreads(8, 8, 1)]\nvoid CombineV2CS', helper + '[numthreads(8, 8, 1)]\nvoid CombineV2CS')
source = once(source,
    '    float modelProxyTargetLuma = fullProxyLuma * exp2(poweredLog);',
    '''    // beta0.5: the legacy path is retained exactly for <=1x and A/B off.
    // This is a spatial composite guard, not a model/history or exposure fix.
    const bool protectBoost = (Passthrough & 1024u) != 0u &&
        TransferStrength > 1.0;
    if (protectBoost) {
        float baseLog = clamp(logRatio * confidence, -logLimit, logLimit);
        float budget = logLimit;
        if (baseLog > 0.0 && (Passthrough & (1u | 16u | 32u)) != 0u) {
            // Only extra brightening is constrained. A pre-existing 1x clip is
            // not silently tone-mapped, and HDR never receives an SDR ceiling.
            float sourcePeak = max(source.r, max(source.g, source.b));
            float sdrHeadroom = max(log2(rcp(max(sourcePeak, 0.000001))), 0.0);
            budget = max(baseLog, min(logLimit, sdrHeadroom));
        }
        poweredLog = sign(baseLog) * SoftExtraMagnitudeV3(
            abs(baseLog), TransferStrength - 1.0, budget);
    }
    float modelProxyTargetLuma = fullProxyLuma * exp2(poweredLog);''')
source = once(source, '        float3 chromaGain = exp2(poweredChromaLog);',
    '''        if (protectBoost) {
            // Keep the existing 1x chroma anchor. Scale its entire log-vector
            // together, so extra amplification cannot clip each axis at a
            // different strength and thereby rotate the edit direction.
            float3 baseChroma = clamp(chromaLog * (ColorStrength * confidence),
                                      -logLimit.xxx, logLimit.xxx);
            float peakLog = max(abs(baseChroma.r),
                max(abs(baseChroma.g), abs(baseChroma.b)));
            float scale = peakLog > 0.0 ? SoftExtraMagnitudeV3(
                peakLog, TransferStrength - 1.0, logLimit) / peakLog : 1.0;
            poweredChromaLog = baseChroma * scale;
        }
        float3 chromaGain = exp2(poweredChromaLog);''')
source = source.replace('beta0.4.3r3', 'beta0.5-dev1')
save('src/nr_before_sr.cpp', source)

ui = read('src/nr_settings_ui.inl')
ui = once(ui, '    int style = static_cast<int>(next.style);', '''    ImGui::BeginDisabled(next.mode != 2);
    changed |= ImGui::Checkbox(ui("High-strength protection (experimental)",
        "高强度保护（实验）"), &next.high_strength_protection);
    SettingsTooltip(ui("Before-SR only, above 1x. Soft-limits the extra brightness/chroma edit; SDR brightening uses remaining source headroom. 0-1x and the full-resolution after-SR reference are unchanged. Off restores the old amplification for A/B. This does not fix an already clipped 1x model result or guarantee stable 8x pictures.",
        "仅前置、强度高于 1 时生效。对额外明暗/颜色增强做软限制，SDR 提亮参考原图剩余空间；0-1 倍和完整后置参考不变。关闭可对比旧算法。不能修复 1 倍已经剪裁的模型结果，也不保证 8 倍画面稳定。"));
    ImGui::EndDisabled();
    int style = static_cast<int>(next.style);''')
save('src/nr_settings_ui.inl', ui)
ini = read('config/nr_before_sr.ini').replace('beta0.4.3r3 community maintenance release', 'beta0.5-dev1 experimental candidate, not an OTA release')
ini = once(ini, 'DetailStability=0.00', 'DetailStability=0.00\n; Mode 2 only: soft-limit extra (>1x) edit; 0 restores beta0.4 amplification.\nHighStrengthProtection=1')
save('config/nr_before_sr.ini', ini)
for path in ['src/nr_before_sr.rc', 'scripts/build.ps1', 'scripts/package-beta03.ps1', 'scripts/validate-beta04-structure.py']:
    text = read(path)
    assert 'beta0.4.3r3' in text, path
    text = text.replace('beta0.4.3r3', 'beta0.5-dev1')
    if path.endswith('.rc'):
        text = text.replace('0,4,3,3', '0,5,0,1').replace('0.4.3r3 beta', '0.5.0 dev1 beta')
    save(path, text)

# WARP runs the actual embedded shader, not a rewritten colour model.
harness = read('tests/codec_gpu_harness.cpp.in')
harness = once(harness, '    if (variant == 3) neural[0].x = std::numeric_limits<float>::quiet_NaN();', '''    if (variant == 8) {
        std::fill(original.begin(), original.end(), Float4{0.60f, 0.55f, 0.50f, 0.625f});
        std::fill(proxy.begin(), proxy.end(), Float4{0.60f, 0.55f, 0.50f, 0.75f});
        std::fill(neural.begin(), neural.end(), Float4{0.84f, 0.77f, 0.70f, 0.875f});
    }
    if (variant == 9)
        std::fill(original.begin(), original.end(), Float4{8.0f, 4.0f, 2.0f, 0.625f});
    if (variant == 10) {
        std::fill(original.begin(), original.end(), Float4{0.22f, 0.32f, 0.14f, 0.625f});
        std::fill(neural.begin(), neural.end(), Float4{0.45f, 0.40f, 0.25f, 0.875f});
    }
    if (variant == 3) neural[0].x = std::numeric_limits<float>::quiet_NaN();''')
harness = once(harness, 'int main() {', '// @V3_BOOST_TESTS@\n\nint main(int argc, char **argv) {')
harness = once(harness, '        ValidateProductionDescriptors(gpu);', '''        const bool negative_control = argc == 2 && std::strcmp(argv[1], "--negative-control") == 0;
        Require(argc == 1 || negative_control, "Unknown codec harness argument");
        ValidateProductionDescriptors(gpu);''')
harness = once(harness, '        unsigned int scenarios = 0;', '        unsigned int scenarios = ValidateHighStrengthV3(gpu, negative_control);')
save('tests/codec_gpu_harness.cpp.in', harness)
validator = read('scripts/validate-codec-gpu.ps1')
validator = once(validator, 'param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT)',
    'param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT, [switch]$NegativeControl)')
validator = once(validator, "$harness = $harness.Replace('// @PRODUCTION_SHADER@', $shader.Value)",
    "$harness = $harness.Replace('// @PRODUCTION_SHADER@', $shader.Value)\n$boostTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\\high_strength_gpu.inl') -Raw\n$harness = $harness.Replace('// @V3_BOOST_TESTS@', $boostTests)")
validator = once(validator, "& $binary\nif ($LASTEXITCODE -ne 0) { throw 'GPU codec validation failed.' }", '''if ($NegativeControl) {
    $negative = & $binary --negative-control 2>&1
    if ($LASTEXITCODE -ne 1 -or ($negative -join "`n") -notmatch 'V3_SDR_EXTRA_BOOST_COLOUR') {
        throw 'Negative control must reject the exact unprotected SDR colour result, not a crash.'
    }
    $global:LASTEXITCODE = 0
    Write-Host 'V3 negative control rejected the expected old clipped SDR result.'
} else {
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'GPU codec validation failed.' }
}''')
save('scripts/validate-codec-gpu.ps1', validator)

workflow = read('.github/workflows/beta042-validate.yml')
workflow = once(workflow, '''  pull_request:
    branches:
      - codex/beta0.4.2
      - codex/beta0.4.2-bug-sweep
      - codex/043-feedback-and-triage
  push:
    branches:
      - codex/beta0.4.2''', '''  pull_request:
    branches: [codex/beta0.5]
  push:
    branches: [codex/beta0.5]''')
workflow = workflow.replace('beta0.4.3r3', 'beta0.5-dev1').replace('beta043-', 'beta050-').replace('beta043r3-compatibility-candidate', 'beta050-dev1-experimental-candidate').replace('channel=compatibility-candidate-not-release', 'channel=experimental-not-release')
workflow = once(workflow, '          python3 scripts/validate-bug-sweep.py --compiler g++',
    '          python3 scripts/validate-beta05-boost.py --compiler g++\n          python3 scripts/validate-bug-sweep.py --compiler g++')
workflow = once(workflow, '        run: ./scripts/validate-codec-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT',
    '        run: |\n          ./scripts/validate-codec-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT\n          ./scripts/validate-codec-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -NegativeControl')
workflow = once(workflow, '          Copy-Item docs/COMPAT_FG_UI_SWEEP.md dist/README-TESTING.md',
    '          Copy-Item docs/BETA_0_5_DEVELOPMENT.md dist/README-TESTING.md')
save('.github/workflows/beta050-validate.yml', workflow)
feedback = read('.github/workflows/feedback-integrity.yml')
feedback = once(feedback, '      - main\n', '      - main\n      - codex/beta0.5\n')
save('.github/workflows/feedback-integrity.yml', feedback)

# New reviewed files arrive in the preparation branch through the connector.
for path in ['tests/high_strength_gpu.inl', 'scripts/validate-beta05-boost.py', 'docs/BETA_0_5_DEVELOPMENT.md']:
    save(path, Path(path).read_text(encoding='utf-8'))
# Verify before making even unreferenced Git objects.
subprocess.run(['python3', 'scripts/validate-beta05-boost.py', '--compiler', 'g++'], check=True)
subprocess.run(['python3', 'scripts/validate-beta04-structure.py'], check=True)
subprocess.run(['python3', 'scripts/test-beta04-math.py'], check=True)
subprocess.run(['git', 'diff', '--check'], check=True)

# No branch writes, commits, tags, releases or binary publication here.
def api(endpoint, payload):
    request = urllib.request.Request('https://api.github.com/repos/' + REPO + '/' + endpoint,
        data=json.dumps(payload).encode(), method='POST', headers={
            'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
            'Accept': 'application/vnd.github+json', 'Content-Type': 'application/json',
            'X-GitHub-Api-Version': '2022-11-28'})
    with urllib.request.urlopen(request, timeout=45) as response:
        return json.load(response)
entries = []
for path, text in sorted(changes.items()):
    blob = api('git/blobs', {'content': text, 'encoding': 'utf-8'})['sha']
    entries.append({'path': path, 'mode': '100644', 'type': 'blob', 'sha': blob})
    print('PREPARED_BLOB ' + json.dumps(entries[-1]), flush=True)
tree = api('git/trees', {'base_tree': BASE_TREE, 'tree': entries})
print('PREPARED_TREE ' + tree['sha'], flush=True)
print('BASE_PARENT ' + BASE, flush=True)
