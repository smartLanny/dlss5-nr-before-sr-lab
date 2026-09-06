"""One-shot source-object preparation; never creates commits or moves refs.
Excluded from the product tree. Changes to large production files are anchored
against exact reviewed Git blobs; CI validates the resulting source normally.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.request

BASE = '14957312ad42debd70bd5c8040fc433e05c502e9'
CAP = '2f070400b19595774bf3fa6b4d46ea2e6c64adf3'
REPO = 'smartLanny/dlss5-nr-before-sr-lab'
assert os.environ['GITHUB_REPOSITORY'] == REPO
assert os.environ['GITHUB_REF'] == 'refs/heads/codex/050-layered-source-preparation'
changes = {}

def read(path, ref=BASE):
    return subprocess.check_output(['git', 'show', ref + ':' + path]).decode('utf-8-sig')

def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Expected one anchor: ' + repr(old[:180]) + ' got ' + str(text.count(old)))
    return text.replace(old, new, 1)

def put(path, text):
    changes[path] = text
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding='utf-8', newline='\n')

# Adopt the complete small cap PR on the 0.5 tree without overwriting its
# experimental identity or prior high-strength protection code.
source = read('src/nr_before_sr.cpp')
data = source.encode('utf-8')
assert hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() == 'e1c801a12a2402f0fd10a246aabd9a9e1d2846fa'
source = once(source,
    'config.transfer_strength = std::clamp(FiniteOr(config.transfer_strength, 0.0f), 0.0f, 8.0f);',
    'config.transfer_strength = std::clamp(FiniteOr(config.transfer_strength, 0.0f), 0.0f, 4.0f);')
source = once(source,
    'std::isfinite(blend) ? blend : 0.0f, 0.0f, 8.0f',
    'std::isfinite(blend) ? blend : 0.0f, 0.0f, 4.0f')
put('src/nr_before_sr.cpp', source)
ui = once(read('src/nr_settings_ui.inl'),
    'constexpr float maximum_strength = 8.0f;', 'constexpr float maximum_strength = 4.0f;')
# Copy the exact shared cap tooltip, not a divergent second policy.
cap_ui = read('src/nr_settings_ui.inl', CAP)
for language in ['Final NR blend strength.', '控制最终 NR 混合强度。']:
    pattern = r'"' + re.escape(language) + r'[^"\n]*"'
    old = re.findall(pattern, ui)
    new = re.findall(pattern, cap_ui)
    assert len(old) == len(new) == 1
    ui = once(ui, old[0], new[0])
put('src/nr_settings_ui.inl', ui)
ini = once(read('config/nr_before_sr.ini'),
    '; Multiplicative strength: 1 normal, 2 doubled, 4 strong, 8 extreme/diagnostic.',
    '; Final blend range 0..4: 0 native bypass, 1 normal, 2 doubled, 4 maximum (not recommended).\n; Existing values above 4 are clamped on load. Model Intensity remains a separate 0..2 control.')
put('config/nr_before_sr.ini', ini)
structure = read('scripts/validate-beta04-structure.py')
structure = once(structure, r'0\.0f,\s*8\.0f\s*\)', r'0\.0f,\s*4\.0f\s*\)')
structure = structure.replace('8x strength ceiling missing', '4x public strength ceiling missing')
put('scripts/validate-beta04-structure.py', structure)
for path in ['scripts/validate-strength-cap.py', 'docs/fixes/FINAL_STRENGTH_CAP.md', '.github/workflows/strength-cap-validate.yml']:
    put(path, read(path, CAP))
cap_changes = dict(changes)

# New composition-only settings. They do not enter the Feature18 tuning or
# history signatures. Normalize after work_mode so the cap's existing isolated
# production harness remains the exact same shared test.
source = once(source,
    '    bool high_strength_protection = true; // Mode 2 composition only; does not reset NR history.',
    '    bool high_strength_protection = true; // Mode 2 composition only; does not reset NR history.\n'
    '    bool layered_enhancement = false; // One NR inference; independent composite contributions.\n'
    '    float layer_tone = 1.0f;\n    float layer_structure = 1.0f;\n'
    '    float layer_bright = 1.0f;\n    float layer_shadow = 1.0f;')
source = once(source, '           a.high_strength_protection == b.high_strength_protection &&',
    '           a.high_strength_protection == b.high_strength_protection &&\n'
    '           a.layered_enhancement == b.layered_enhancement &&\n'
    '           a.layer_tone == b.layer_tone && a.layer_structure == b.layer_structure &&\n'
    '           a.layer_bright == b.layer_bright && a.layer_shadow == b.layer_shadow &&')
anchor = '    config.mode = config.mode == 2 ? 2 : 1;'
source = once(source, anchor, anchor + '\n'
    '    config.layer_tone = std::clamp(FiniteOr(config.layer_tone, 1.0f), 0.0f, 2.0f);\n'
    '    config.layer_structure = std::clamp(FiniteOr(config.layer_structure, 1.0f), 0.0f, 2.0f);\n'
    '    config.layer_bright = std::clamp(FiniteOr(config.layer_bright, 1.0f), 0.0f, 2.0f);\n'
    '    config.layer_shadow = std::clamp(FiniteOr(config.layer_shadow, 1.0f), 0.0f, 2.0f);')
source = once(source, '    next.detail_stability = ReadIniFloat(L"DetailStability", 0.0f);',
    '    next.detail_stability = ReadIniFloat(L"DetailStability", 0.0f);\n'
    '    next.layered_enhancement = GetPrivateProfileIntW(L"NRBeforeSR", L"LayeredEnhancement", 0, g_ini_path) != 0;\n'
    '    next.layer_tone = ReadIniFloat(L"LayerTone", 1.0f);\n'
    '    next.layer_structure = ReadIniFloat(L"LayerStructure", 1.0f);\n'
    '    next.layer_bright = ReadIniFloat(L"LayerBright", 1.0f);\n'
    '    next.layer_shadow = ReadIniFloat(L"LayerShadow", 1.0f);')
source = once(source, '    record_write(WriteIniFloat(L"DetailStability", config.detail_stability));',
    '    record_write(WriteIniFloat(L"DetailStability", config.detail_stability));\n'
    '    record_write(WriteIniInt(L"LayeredEnhancement", config.layered_enhancement ? 1 : 0));\n'
    '    record_write(WriteIniFloat(L"LayerTone", config.layer_tone));\n'
    '    record_write(WriteIniFloat(L"LayerStructure", config.layer_structure));\n'
    '    record_write(WriteIniFloat(L"LayerBright", config.layer_bright));\n'
    '    record_write(WriteIniFloat(L"LayerShadow", config.layer_shadow));')
for prefix, cfg in [('        ', 'g_config'), ('    ', 'applied')]:
    anchor = prefix + 'Log("HighStrengthProtection requested=%d; effective only in Mode 2 above 1x; codec flag=1024.",'
    new = prefix + 'Log("LayeredEnhancement requested=%d mode=%d tone=%.3f structure=%.3f bright=%.3f shadow=%.3f; single NR; effective luma exponent capped at 4; codec flag=2048.",\n' + prefix + '    ' + ', '.join(cfg + '.' + name for name in ['layered_enhancement', 'mode', 'layer_tone', 'layer_structure', 'layer_bright', 'layer_shadow']) + ');\n'
    source = once(source, anchor, new + anchor)
# Correct stale comments; the historical stress fixtures can still inject 8x
# directly into a shader, but no INI or UI path can request it.
source = source.replace('// deliberately strong, and 8.0 is an extreme diagnostic ceiling. HDR,',
                        '// the public maximum (not a recommendation). HDR,')
source = once(source, 'constexpr UINT kCodecRootConstantCount = 31;',
                      'constexpr UINT kCodecRootConstantCount = 35;')
# Append to the ABI, leaving all existing offsets intact. Existing aggregate
# initializers used by copy/encode/motion and test fixtures remain neutral.
match = re.search(r'struct CodecConstants \{[\s\S]*?\n\};', source)
assert match
cpp = match.group(0)
cpp_new = cpp[:-3] + '\n    float layer_tone = 1.0f;\n    float layer_structure = 1.0f;\n    float layer_bright = 1.0f;\n    float layer_shadow = 1.0f;\n};'
source = once(source, cpp, cpp_new)
source = once(source, '    uint CaptureRoiHeight;\n};',
    '    uint CaptureRoiHeight;\n    float LayerTone;\n    float LayerStructure;\n    float LayerBright;\n    float LayerShadow;\n};')
helper = '''// LAYERED_SCALAR_BEGIN
// The broad/fine split is a bounded heuristic, not a semantic material mask.
// Same-sign partitioning avoids invented opposite-sign halos. Gains weight
// two parts of ONE edit; they do not add repeated NR passes or evade the cap.
float LayeredLumaStrengthV3(float edit, float mean, float master,
                          float tone, float structure, float bright, float shadow) {
    master = clamp(master, 0.0, 4.0);
    if (master <= 0.0) return 0.0;
    if (tone == 1.0 && structure == 1.0 && bright == 1.0 && shadow == 1.0)
        return master;
    float magnitude = abs(edit);
    float broad = clamp(mean, min(edit, 0.0), max(edit, 0.0));
    float fraction = magnitude > 0.0 ? saturate(abs(broad) / magnitude) : 0.0;
    float gain = tone == structure ? tone : tone * fraction + structure * (1.0 - fraction);
    float directional = edit > 0.0 ? bright : (edit < 0.0 ? shadow : 1.0);
    return clamp(master * gain * directional, 0.0, 4.0);
}
// LAYERED_SCALAR_END

'''
source = once(source, '[numthreads(8, 8, 1)]\nvoid CombineV2CS', helper + '[numthreads(8, 8, 1)]\nvoid CombineV2CS')
source = once(source, '    logRatio = StabilizeDetailLogV2(center, logRatio, proxyLuma);',
    '    logRatio = StabilizeDetailLogV2(center, logRatio, proxyLuma);\n'
    '    const bool layeredActive = (Passthrough & 2048u) != 0u;\n'
    '    float layerMean = 0.0;')
source = once(source, '    if (LightingLock > 0.000001) {',
    '    if (LightingLock > 0.000001 || (layeredActive && LayerTone != LayerStructure)) {')
source = once(source, '        // Protection may only attenuate this pixel\'s existing edit.',
    '        layerMean = localMean;\n        // Protection may only attenuate this pixel\'s existing edit.')
source = once(source, '    float maxRatio = max(MaxLumaRatio, 1.0);\n    float logLimit = log2(maxRatio);',
    '    float lumaStrength = TransferStrength;\n'
    '    if (layeredActive)\n'
    '        lumaStrength = LayeredLumaStrengthV3(logRatio, layerMean, TransferStrength,\n'
    '            LayerTone, LayerStructure, LayerBright, LayerShadow);\n'
    '    float maxRatio = max(MaxLumaRatio, 1.0);\n    float logLimit = log2(maxRatio);')
source = once(source, '    // beta0.5: the legacy path is retained exactly for <=1x and A/B off.',
    '    if (layeredActive)\n'
    '        poweredLog = clamp(logRatio * lumaStrength * confidence, -logLimit, logLimit);\n'
    '    // With layers off/neutral, preserve dev1 arithmetic inside the public 0..4 range.')
# Retain protectBoost for independent colour; only luma uses lumaStrength.
source = once(source, '    if (protectBoost) {\n        float baseLog',
    '    if ((Passthrough & 1024u) != 0u && lumaStrength > 1.0) {\n        float baseLog')
source = once(source, '            abs(baseLog), TransferStrength - 1.0, budget);',
    '            abs(baseLog), lumaStrength - 1.0, budget);')
source = once(source, '        ((v2_combine && g_config.high_strength_protection) ? 1024u : 0u) |',
    '        ((v2_combine && g_config.high_strength_protection) ? 1024u : 0u) |\n'
    '        ((v2_combine && g_config.layered_enhancement) ? 2048u : 0u) |')
source = once(source, '        f8_armed?f8_info.region_width:0u,f8_armed?f8_info.region_height:0u};',
    '        f8_armed?f8_info.region_width:0u,f8_armed?f8_info.region_height:0u,\n'
    '        v2_combine ? g_config.layer_tone : 1.0f,\n'
    '        v2_combine ? g_config.layer_structure : 1.0f,\n'
    '        v2_combine ? g_config.layer_bright : 1.0f,\n'
    '        v2_combine ? g_config.layer_shadow : 1.0f};')
# Snapshot actual GPU constants with each capture; do not consult live g_config
# when asynchronously serializing an older captured frame.
source = once(source, '    bool motion_requested = false;',
    '    float layer_tone = 1.0f;\n    float layer_structure = 1.0f;\n'
    '    float layer_bright = 1.0f;\n    float layer_shadow = 1.0f;\n'
    '    bool motion_requested = false;')
anchor = '        f8_info.detail_stability=v2_combine?g_config.detail_stability:0.f;f8_info.motion_requested=g_config.motion_stability;'
source = once(source, anchor, anchor + '\n' + ''.join(
    '        f8_info.' + n + '=v2_combine?g_config.' + n + ':1.f;\n' for n in ['layer_tone','layer_structure','layer_bright','layer_shadow']).rstrip())
anchor = '        info.edge_guard = v2_combine ? g_config.edge_guard : 0.0f;'
source = once(source, anchor, anchor + '\n' + ''.join(
    '        info.' + n + ' = v2_combine ? g_config.' + n + ' : 1.0f;\n' for n in ['layer_tone','layer_structure','layer_bright','layer_shadow']).rstrip())
put('src/nr_before_sr.cpp', source)

ui = ui.replace('or guarantee stable 8x pictures.', 'or guarantee stable pictures at the 4x public maximum.')
ui = ui.replace('也不保证 8 倍画面稳定。', '也不保证上限 4 倍画面稳定。')
ui = ui.replace('Before-SR only, above 1x.', 'Before-SR only, above 1x effective strength (including enabled layers).')
ui = ui.replace('仅前置、强度高于 1 时生效。', '仅前置、有效强度高于 1 时生效（含分层增益）。')
ui = once(ui, '    int style = static_cast<int>(next.style);', '''    if (ImGui::CollapsingHeader(ui("Layered composition (one NR)", "分层强化（单次 NR）"))) {
        ImGui::BeginDisabled(next.mode != 2);
        changed |= ImGui::Checkbox(ui("Enable layered composition", "启用分层强化"), &next.layered_enhancement);
        ImGui::BeginDisabled(!next.layered_enhancement);
        changed |= ImGui::SliderFloat(ui("Broad tone contribution", "大范围影调贡献"), &next.layer_tone, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Structure contribution", "局部结构贡献"), &next.layer_structure, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Brightening contribution", "提亮贡献"), &next.layer_bright, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Darkening contribution", "压暗贡献"), &next.layer_shadow, 0.f, 2.f, "%.2f");
        ImGui::EndDisabled();
        if (ImGui::Button(ui("Reset only these layers", "仅恢复分层默认"))) {
            next.layered_enhancement = false;
            next.layer_tone = next.layer_structure = next.layer_bright = next.layer_shadow = 1.f;
            changed = true;
        }
        ImGui::TextWrapped("%s", ui("Experimental Before-SR composition, not repeated model inference. 1 is neutral. Combined luminance strength cannot exceed 4. Colour follow remains independent under HDR and colour. The split is not a face/material mask and cannot recover detail absent from the model. No extra NR pass; GPU cost still needs measurement.",
            "实验前置合成，不是多次运行模型。1 为中性，组合后的有效明暗强度最多 4。颜色跟随仍在 HDR 与颜色中独立控制。这不是人脸或材质遮罩，不能恢复模型没有的细节。不增加 NR 次数；实际 GPU 开销仍需测量。"));
        ImGui::EndDisabled();
    }
    int style = static_cast<int>(next.style);''')
put('src/nr_settings_ui.inl', ui)
ini = once(ini, 'HighStrengthProtection=1', '''HighStrengthProtection=1
; One-inference Mode2 composition, not cascaded NR. Off/neutral preserves the old path.
LayeredEnhancement=0
; Each gain is 0..2; combined luminance exponent is capped at 4, not multiplied without limit.
LayerTone=1.00
LayerStructure=1.00
LayerBright=1.00
LayerShadow=1.00''')
put('config/nr_before_sr.ini', ini)
stream = read('src/nr_f8_stream.inl')
stream = once(stream, '    b.Float("edgeGuard",c.edge_guard);b.Float("detailStability",c.detail_stability);b.Append("}");',
    '    b.Float("edgeGuard",c.edge_guard);b.Float("detailStability",c.detail_stability);\n'
    '    b.Append(",\\\"layeredEnhancement\\\":%d",c.layered_enhancement);\n'
    '    b.Float("layerTone",c.layer_tone);b.Float("layerStructure",c.layer_structure);\n'
    '    b.Float("layerBright",c.layer_bright);b.Float("layerShadow",c.layer_shadow);b.Append("}");')
stream = once(stream, '    b.Float("detailStability",n.detail_stability);',
    '    b.Float("detailStability",n.detail_stability);\n'
    '    b.Append(",\\\"layeredEffective\\\":%d",(n.codec_flags & 2048u) != 0u);\n'
    '    b.Float("layerTone",n.layer_tone);b.Float("layerStructure",n.layer_structure);\n'
    '    b.Float("layerBright",n.layer_bright);b.Float("layerShadow",n.layer_shadow);')
put('src/nr_f8_stream.inl', stream)

# Keep existing full-layout validation, append four fields, do not relax it.
structure = structure.replace('31', '35')
structure = once(structure,
    '([A-Za-z0-9_]+)\\s*;", cpp.group("body"), re.M)',
    '([A-Za-z0-9_]+)(?:\\s*=\\s*[^;]+)?\\s*;", cpp.group("body"), re.M)')
put('scripts/validate-beta04-structure.py', structure)
boost = read('scripts/validate-beta05-boost.py').replace('kCodecRootConstantCount = 31;', 'kCodecRootConstantCount = 35;')
put('scripts/validate-beta05-boost.py', boost)

# Execute new oracle on the production shader through the existing WARP path.
harness = read('tests/codec_gpu_harness.cpp.in')
harness = once(harness, 'float requested_strength = -1.0f) {',
    'float requested_strength = -1.0f, Float4 layers = {1.f,1.f,1.f,1.f}) {')
harness = once(harness, '    if (variant == 3) neural[0].x = std::numeric_limits<float>::quiet_NaN();', '''    if (variant == 11) {
        std::fill(original.begin(), original.end(), Float4{.60f,.55f,.50f,.625f});
        std::fill(proxy.begin(), proxy.end(), Float4{.60f,.55f,.50f,.75f});
        std::fill(neural.begin(), neural.end(), Float4{.42f,.385f,.35f,.875f});
    }
    if (variant == 3) neural[0].x = std::numeric_limits<float>::quiet_NaN();''')
# Only change the Run constants, not other fixtures.
start = harness.index('RunResult Run(')
end = harness.index('int main(', start)
part = harness[start:end]
part = once(part, '    const CodecConstants constants = {width, height, 1.0f,',
    '    CodecConstants constants = {width, height, 1.0f,')
# Insert before the very first command-list root binding after constants.
constant_start = part.index('    CodecConstants constants =')
constant_end = part.index(';', constant_start) + 1
part = part[:constant_end] + '\n    constants.layer_tone = layers.x; constants.layer_structure = layers.y;\n    constants.layer_bright = layers.z; constants.layer_shadow = layers.w;' + part[constant_end:]
harness = harness[:start] + part + harness[end:]
harness = once(harness, '// @V3_BOOST_TESTS@', '// @V3_BOOST_TESTS@\n// @LAYER_TESTS@')
harness = once(harness, '        Require(argc == 1 || negative_control, "Unknown codec harness argument");',
    '        const bool layer_negative = argc == 2 && std::strcmp(argv[1], "--layer-negative-control") == 0;\n'
    '        Require(argc == 1 || negative_control || layer_negative, "Unknown codec harness argument");')
harness = once(harness, '        unsigned int scenarios = ValidateHighStrengthV3(gpu, negative_control);',
    '        unsigned int scenarios = ValidateHighStrengthV3(gpu, negative_control);\n'
    '        scenarios += ValidateLayeredComposition(gpu, layer_negative);')
put('tests/codec_gpu_harness.cpp.in', harness)
validator = read('scripts/validate-codec-gpu.ps1')
validator = once(validator, '[switch]$NegativeControl)', '[switch]$NegativeControl, [switch]$LayerNegativeControl)')
validator = once(validator, "$harness = $harness.Replace('// @V3_BOOST_TESTS@', $boostTests)",
    "$harness = $harness.Replace('// @V3_BOOST_TESTS@', $boostTests)\n$layerTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\\layered_composite_gpu.inl') -Raw\n$harness = $harness.Replace('// @LAYER_TESTS@', $layerTests)")
validator = once(validator, 'if ($NegativeControl) {\n    $negative = & $binary --negative-control 2>&1',
    '''if ($LayerNegativeControl) {
    $negative = & $binary --layer-negative-control 2>&1
    if ($LASTEXITCODE -ne 1 -or ($negative -join "`n") -notmatch 'LAYER_DIRECTION_ORACLE') {
        throw 'Layer negative control must reject the precise unattenuated bright edit.'
    }
    $global:LASTEXITCODE = 0
    Write-Host 'Layer negative control rejected the expected unchanged bright edit.'
} elseif ($NegativeControl) {
    $negative = & $binary --negative-control 2>&1''')
put('scripts/validate-codec-gpu.ps1', validator)
workflow = read('.github/workflows/beta050-validate.yml')
workflow = once(workflow, '  pull_request:\n    branches: [next/0.5]',
    '  pull_request:\n    branches: [next/0.5, codex/050-high-strength-guard]')
workflow = once(workflow, '          python3 scripts/validate-beta05-boost.py --compiler g++',
    '          python3 scripts/validate-layered-composite.py --compiler g++\n'
    '          python3 scripts/validate-strength-cap.py\n'
    '          python3 scripts/validate-beta05-boost.py --compiler g++')
workflow = once(workflow, '          ./scripts/validate-codec-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -NegativeControl',
    '          ./scripts/validate-codec-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -NegativeControl\n'
    '          ./scripts/validate-codec-gpu.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -LayerNegativeControl')
workflow = once(workflow, '          Copy-Item docs/BETA_0_5_DEVELOPMENT.md dist/README-TESTING.md',
    '          Copy-Item docs/BETA_0_5_LAYERS.md dist/README-TESTING.md')
put('.github/workflows/beta050-validate.yml', workflow)
feedback = read('.github/workflows/feedback-integrity.yml')
feedback = once(feedback, '      - next/0.5\n', '      - next/0.5\n      - codex/050-high-strength-guard\n')
put('.github/workflows/feedback-integrity.yml', feedback)
capworkflow = changes['.github/workflows/strength-cap-validate.yml'].replace(
    'branches: [main, release/0.4]', 'branches: [main, release/0.4, next/0.5, codex/050-high-strength-guard]')
put('.github/workflows/strength-cap-validate.yml', capworkflow)

# Consistent dev2 executable/package identity. Historical documents are marked
# historical, not retroactively changed into claims about the new candidate.
for path in ['src/nr_before_sr.cpp','src/nr_before_sr.rc','config/nr_before_sr.ini',
             'scripts/build.ps1','scripts/package-beta03.ps1','scripts/validate-beta04-structure.py',
             '.github/workflows/beta050-validate.yml','docs/UPDATE.en.txt','docs/UPDATE.zh-CN.txt',
             'docs/INSTALL.en.md','docs/INSTALL.en.txt','docs/INSTALL.zh-CN.md','docs/INSTALL.zh-CN.txt']:
    text = changes.get(path, read(path))
    assert 'beta0.5-dev1' in text, path
    text = text.replace('beta0.5-dev1', 'beta0.5-dev2').replace('beta050-dev1-', 'beta050-dev2-')
    if path.endswith('.rc') or path.endswith('validate-beta04-structure.py'):
        text = text.replace('0,5,0,1', '0,5,0,2').replace('0.5.0 dev1 beta','0.5.0 dev2 beta')
    put(path, text)
put('docs/BETA_0_5_DEVELOPMENT.md',
    '> Historical dev1 design below. Current dev2 adds opt-in single-inference layers and a hard public strength cap of 4; see [BETA_0_5_LAYERS.md](BETA_0_5_LAYERS.md). Historical 8x fixture discussion is not a supported user setting.\n\n' + read('docs/BETA_0_5_DEVELOPMENT.md'))
put('README.md', '# beta0.5-dev2 experimental layered composition\n\n单次 NR 分层强化与最终强度 0–4；并非 PR26 的多次模型推理。默认分层关闭，游戏与性能仍待验证。\n\n开发、回退、真实范围：[BETA_0_5_LAYERS.md](docs/BETA_0_5_LAYERS.md)。\n\n---\n\n以下是历史 dev1 / 0.4 基线说明，不代表当前候选的新功能：\n\n' + read('README.md'))
for path in ['scripts/validate-layered-composite.py','tests/layered_composite_gpu.inl','docs/BETA_0_5_LAYERS.md']:
    put(path, Path(path).read_text(encoding='utf-8'))
# Preserve original archived evidence bytes on clean Windows checkouts. This
# matches the archive-only correction in #92, not its unrelated runtime changes.
attrs = read('.gitattributes')
if 'bug-inbox/** -text !eol' not in attrs:
    put('.gitattributes', attrs.rstrip() + '\n# Archived evidence is immutable; never normalize its mixed original line endings.\nbug-inbox/** -text !eol\n')

for command in [
    ['python3','scripts/validate-strength-cap.py'],
    ['python3','scripts/validate-layered-composite.py','--compiler','g++'],
    ['python3','scripts/validate-beta05-boost.py','--compiler','g++'],
    ['python3','scripts/validate-beta04-structure.py'],
    ['python3','scripts/test-beta04-math.py'],
    ['git','diff','--check'],
]:
    subprocess.run(command, check=True)

# Unreferenced blobs only. GitHub connector will construct the reviewed trees,
# preserve both source parents for the cap adoption, then publish the feature PR.
def blob(text):
    request = urllib.request.Request('https://api.github.com/repos/' + REPO + '/git/blobs',
        data=json.dumps({'content':text,'encoding':'utf-8'}).encode(),method='POST',headers={
        'Authorization':'Bearer '+os.environ['GH_TOKEN'], 'Accept':'application/vnd.github+json',
        'Content-Type':'application/json','X-GitHub-Api-Version':'2022-11-28'})
    with urllib.request.urlopen(request, timeout=45) as response:
        return json.load(response)['sha']
print('BASE_TREE ' + subprocess.check_output(['git','rev-parse',BASE+'^{tree}']).decode().strip(), flush=True)
for group, files in [('CAP',cap_changes),('LAYERS',changes)]:
    for path,text in sorted(files.items()):
        print(group+'_BLOB '+json.dumps({'path':path,'mode':'100644','type':'blob','sha':blob(text)}), flush=True)
print('PREPARATION_COMPLETE',flush=True)
