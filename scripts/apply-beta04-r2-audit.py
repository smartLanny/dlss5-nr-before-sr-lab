#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
BRANCH_VERSION = "beta0.4-perfect-chain-r2"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one literal match, found {count}")
    return text.replace(old, new, 1)


def sub_once(text: str, pattern: str, replacement: str, label: str) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{label}: expected one regex match, found {count}")
    return updated


source_path = ROOT / "src" / "nr_before_sr.cpp"
source = source_path.read_text(encoding="utf-8")
source = replace_once(
    source,
    'constexpr const char *kAddonVersion = "beta0.4-perfect-chain-r1";',
    'constexpr const char *kAddonVersion = "beta0.4-perfect-chain-r2";',
    "add-on version",
)
source = replace_once(
    source,
    'constexpr const char *kBuildIdentity = "beta0.4-perfect-chain-r1";',
    'constexpr const char *kBuildIdentity = "beta0.4-perfect-chain-r2";',
    "build identity",
)
source = replace_once(
    source,
    "    // 1.0 follows the model, 2.0 compounds the model ratio twice, and 4.0 is\n"
    "    // deliberately exaggerated while the HDR and finite-value guards remain.\n",
    "    // 1.0 follows the model, 2.0 compounds the model ratio twice, 4.0 is\n"
    "    // deliberately strong, and 8.0 is an extreme diagnostic ceiling. HDR,\n"
    "    // finite-value and user-selected maximum-ratio guards remain authoritative.\n",
    "strength comment",
)
source = replace_once(
    source,
    "    config.transfer_strength = std::clamp(config.transfer_strength, 0.0f, 4.0f);",
    "    config.transfer_strength = std::clamp(config.transfer_strength, 0.0f, 8.0f);",
    "strength clamp",
)
source = replace_once(
    source,
    '    next.max_luma_ratio = ReadIniFloat(L"MaxLumaRatio", 2.0f);',
    '    next.max_luma_ratio = ReadIniFloat(L"MaxLumaRatio", 4.0f);',
    "maximum ratio fallback",
)

new_ratio_helper = r'''float ModelLumaRatioV2(float2 fullPixelCenter) {
    float2 mapped = fullPixelCenter * float2(ModelWidth, ModelHeight) /
        float2(max(Width, 1u), max(Height, 1u)) - 0.5;
    int2 pxy = int2(round(mapped));
    float p = max(dot(LoadProxyLinearV2(pxy), kLuma), 1.0 / 4096.0);
    float n = max(dot(LoadNeuralLinearV2(pxy), kLuma), 1.0 / 4096.0);
    return clamp(n / p, 1.0 / 256.0, 256.0);
}'''
source = sub_once(
    source,
    r"float ModelLogLumaRatioV2\(float2 fullPixelCenter\) \{.*?\n\}",
    new_ratio_helper,
    "lighting-ratio helper",
)

new_encode = r'''[numthreads(8, 8, 1)]
void EncodeV2CS(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= Width || tid.y >= Height ||
        SourceWidth == 0u || SourceHeight == 0u) return;

    // ModelEncodedValueDomain deliberately preserves the live bounded R8
    // numbers without guessing a gamma curve. At 1:1, encoded contracts stay
    // byte-for-byte equivalent; scaled R8 is area-filtered in that same value
    // domain instead of being accidentally encoded a second time.
    const bool modelValueDomain = (Passthrough & 1u) != 0u;
    const bool srgbSdrStorage = (Passthrough & 32u) != 0u;
    const bool encodedStorage = modelValueDomain || srgbSdrStorage;
    if (SourceWidth == Width && SourceHeight == Height && encodedStorage) {
        float4 exact = LoadSourceStoredV2(int2(tid.xy));
        ProxyOut[tid.xy] = float4(exact.rgb, 1.0);
        return;
    }

    float2 sourceStart = float2(tid.xy) *
        float2(SourceWidth, SourceHeight) / float2(Width, Height);
    float2 sourceEnd = float2(tid.xy + 1u) *
        float2(SourceWidth, SourceHeight) / float2(Width, Height);
    int2 first = int2(floor(sourceStart));
    int2 last = int2(ceil(sourceEnd));
    float3 total = 0.0.xxx;
    float totalWeight = 0.0;
    [loop]
    for (int y = first.y; y < last.y; ++y) {
        float wy = max(0.0, min(sourceEnd.y, float(y + 1)) -
                            max(sourceStart.y, float(y)));
        [loop]
        for (int x = first.x; x < last.x; ++x) {
            float wx = max(0.0, min(sourceEnd.x, float(x + 1)) -
                                max(sourceStart.x, float(x)));
            float w = wx * wy;
            float3 sampleValue = modelValueDomain
                ? saturate(LoadSourceStoredV2(int2(x, y)).rgb)
                : LoadSourceLinearV2(int2(x, y));
            total += sampleValue * w;
            totalWeight += w;
        }
    }
    float3 filteredValue = total / max(totalWeight, 0.000001);
    float3 proxyValue = EncodeProxyLinearV2(filteredValue);
    float3 storedProxy = modelValueDomain
        ? saturate(proxyValue) : LinearToSRGB(proxyValue);
    ProxyOut[tid.xy] = float4(storedProxy, 1.0);
}'''
source = sub_once(
    source,
    r"\[numthreads\(8, 8, 1\)\]\nvoid EncodeV2CS\(uint3 tid : SV_DispatchThreadID\) \{.*?\n\}\n\n\[numthreads\(8, 8, 1\)\]\nvoid EncodeCS",
    new_encode + "\n\n[numthreads(8, 8, 1)]\nvoid EncodeCS",
    "V2 encoder",
)

source = replace_once(
    source,
    "    if (result.output_width == 0 || result.output_height == 0) {\n"
    "        result.output_width = render_width;\n"
    "        result.output_height = render_height;\n"
    "    }\n",
    "    if (result.output_width == 0 || result.output_height == 0) {\n"
    "        // Output-relative presets require an authoritative live output\n"
    "        // extent. Falling back to the render extent would silently turn a\n"
    "        // Quality bucket into 58% of *render* resolution. Keep the full\n"
    "        // render raster instead, preserving image quality and history.\n"
    "        static unsigned int unresolved_output_extents = 0;\n"
    "        ++unresolved_output_extents;\n"
    "        if (unresolved_output_extents <= 3 ||\n"
    "            (unresolved_output_extents % 600) == 0) {\n"
    "            Log(\"NR work preset could not prove the live SR output extent; \"\n"
    "                \"using the full render raster %ux%u (count=%u).\",\n"
    "                render_width, render_height, unresolved_output_extents);\n"
    "        }\n"
    "        return result;\n"
    "    }\n",
    "safe unresolved output fallback",
)

source = replace_once(
    source,
    "        float localMean = ModelLogLumaRatioV2(center);\n"
    "        localMean += ModelLogLumaRatioV2(center + float2(radius, 0.0));\n"
    "        localMean += ModelLogLumaRatioV2(center - float2(radius, 0.0));\n"
    "        localMean += ModelLogLumaRatioV2(center + float2(0.0, radius));\n"
    "        localMean += ModelLogLumaRatioV2(center - float2(0.0, radius));\n"
    "        localMean *= 0.2;\n"
    "        logRatio = lerp(logRatio, logRatio - localMean,\n"
    "                        saturate(LightingLock));\n",
    "        // The centre ratio is already available. Average four sparse\n"
    "        // neighbours in linear-ratio space, then take one logarithm. This\n"
    "        // preserves the broad-drift estimate while avoiding five extra\n"
    "        // log2 operations and a redundant centre texture pair per pixel.\n"
    "        float localRatio = 0.40 * (neuralLuma / proxyLuma);\n"
    "        localRatio += 0.15 * ModelLumaRatioV2(center + float2(radius, 0.0));\n"
    "        localRatio += 0.15 * ModelLumaRatioV2(center - float2(radius, 0.0));\n"
    "        localRatio += 0.15 * ModelLumaRatioV2(center + float2(0.0, radius));\n"
    "        localRatio += 0.15 * ModelLumaRatioV2(center - float2(0.0, radius));\n"
    "        float localMean = clamp(log2(max(localRatio, 1.0 / 256.0)),\n"
    "                                -8.0, 8.0);\n"
    "        logRatio = lerp(logRatio, logRatio - localMean,\n"
    "                        saturate(LightingLock));\n",
    "lighting-lock optimization",
)

source = replace_once(
    source,
    "        float l = dot(LoadSourceLinearV2(p + int2(-1, 0)), kLuma);\n"
    "        float r = dot(LoadSourceLinearV2(p + int2(1, 0)), kLuma);\n"
    "        float u = dot(LoadSourceLinearV2(p + int2(0, -1)), kLuma);\n"
    "        float d = dot(LoadSourceLinearV2(p + int2(0, 1)), kLuma);\n"
    "        float gradient = max(abs(r - l), abs(d - u)) /\n"
    "            max(sourceLuma, 0.02);\n",
    "        // Two forward taps are enough for a conservative edge guard and\n"
    "        // halve full-resolution source loads versus the old four-tap\n"
    "        // central difference. Boundary loads are clamped by the helper.\n"
    "        float r = dot(LoadSourceLinearV2(p + int2(1, 0)), kLuma);\n"
    "        float d = dot(LoadSourceLinearV2(p + int2(0, 1)), kLuma);\n"
    "        float gradient = max(abs(r - sourceLuma),\n"
    "                             abs(d - sourceLuma)) /\n"
    "            max(sourceLuma, 0.02);\n",
    "edge-guard optimization",
)

source = replace_once(
    source,
    "    float max_luma_ratio = 1.0f;\n"
    "    float pre_exposure = 1.0f;\n",
    "    float max_luma_ratio = 1.0f;\n"
    "    int work_mode = 0;\n"
    "    float work_linear_scale = 1.0f;\n"
    "    float lighting_lock = 0.0f;\n"
    "    float edge_guard = 0.0f;\n"
    "    unsigned int proxy_curve = 0;\n"
    "    float frame_confidence = 1.0f;\n"
    "    float pre_exposure = 1.0f;\n",
    "stage-capture V2 fields",
)

source = replace_once(
    source,
    "        info.max_luma_ratio = g_config.max_luma_ratio;\n"
    "        info.pre_exposure = color_contract.pre_exposure;\n",
    "        info.max_luma_ratio = g_config.max_luma_ratio;\n"
    "        info.work_mode = g_config.work_mode;\n"
    "        info.work_linear_scale = std::min(\n"
    "            static_cast<float>(resolved_model_width) / std::max(width, 1u),\n"
    "            static_cast<float>(resolved_model_height) / std::max(height, 1u));\n"
    "        info.lighting_lock = v2_combine ? g_config.lighting_lock :\n"
    "            (g_config.preserve_lighting ? 1.0f : 0.0f);\n"
    "        info.edge_guard = v2_combine ? g_config.edge_guard : 0.0f;\n"
    "        info.proxy_curve = g_config.proxy_curve;\n"
    "        info.frame_confidence = std::clamp(frame_confidence, 0.0f, 1.0f);\n"
    "        info.pre_exposure = color_contract.pre_exposure;\n",
    "stage-capture V2 assignment",
)

source = replace_once(
    source,
    "    ArmStageCaptureLocked(context, slot);\n"
    "    if (!slot.delta_stats_pending && slot.delta_stats != nullptr &&\n"
    "        slot.delta_stats_readback != nullptr) {\n",
    "    ArmStageCaptureLocked(context, slot);\n"
    "    // The diagnostic UAV clear, atomics, copy and readback are valuable\n"
    "    // evidence but should not tax every rendered frame. Sample the first\n"
    "    // successful frames, every committed configuration change, and then\n"
    "    // one periodic checkpoint. One-shot stage capture remains immediate.\n"
    "    const unsigned int current_generation =\n"
    "        g_config_generation.load(std::memory_order_relaxed);\n"
    "    const bool should_sample_delta = context.evaluations < 3 ||\n"
    "        context.config_generation != current_generation ||\n"
    "        (context.evaluations % 120) == 0;\n"
    "    if (should_sample_delta && !slot.delta_stats_pending &&\n"
    "        slot.delta_stats != nullptr &&\n"
    "        slot.delta_stats_readback != nullptr) {\n",
    "diagnostic throttling",
)
source = replace_once(
    source,
    "        slot.diagnostic_preserve_lighting = g_config.preserve_lighting;",
    "        slot.diagnostic_preserve_lighting = context.mode == 2\n"
    "            ? g_config.lighting_lock > 0.000001f\n"
    "            : g_config.preserve_lighting;",
    "diagnostic lighting state",
)

source = replace_once(
    source,
    "            &next.transfer_strength, 0.0f, 4.0f, \"%.2f\")) {",
    "            &next.transfer_strength, 0.0f, 8.0f, \"%.2f\")) {",
    "strength UI range",
)
color_slider_anchor = (
    "    changed |= ImGui::SliderFloat(\n"
    "        ui(\"NR Color Follow\", \"NR 颜色跟随强度\"),\n"
    "        &next.color_strength, 0.0f, 2.0f, \"%.2f\");\n"
)
max_ratio_ui = (
    "    changed |= ImGui::SliderFloat(\n"
    "        ui(\"Maximum luminance ratio\", \"最大亮度倍率\"),\n"
    "        &next.max_luma_ratio, 1.0f, 16.0f, \"%.2f\");\n"
    "    ImGui::TextDisabled(\n"
    "        \"%s\", ui(\n"
    "            \"Hard safety ceiling for the powered NR luminance ratio. Raise it only \"\n"
    "            \"for controlled extreme-strength captures.\",\n"
    "            \"乘算后 NR 明暗倍率的硬上限；只有在受控的极限强度对比中才建议提高。\"));\n"
)
source = replace_once(source, color_slider_anchor, max_ratio_ui + color_slider_anchor,
                      "maximum ratio UI")

source_path.write_text(source, encoding="utf-8", newline="\n")

stage_path = ROOT / "src" / "nr_stage_capture.inl"
stage = stage_path.read_text(encoding="utf-8")
stage = replace_once(stage, '"format=NRStageCapture-v1\\nbuild=%s\\ncomplete=%d\\n"',
                     '"format=NRStageCapture-v2\\nbuild=%s\\ncomplete=%d\\n"',
                     "stage metadata version")
stage = replace_once(
    stage,
    '        "proxyScale=%.9g\\ntransfer=%.9g\\ncolorFollowEffective=%.9g\\nmaxLumaRatio=%.9g\\n"\n'
    '        "preExposure=%.9g\\nhasPreExposure=%d\\nscalarExposureScale=%.9g\\nhasExposureScale=%d\\n"\n',
    '        "proxyScale=%.9g\\ntransfer=%.9g\\ncolorFollowEffective=%.9g\\nmaxLumaRatio=%.9g\\n"\n'
    '        "workMode=%d\\nworkLinearScale=%.9g\\nlightingLock=%.9g\\nedgeGuard=%.9g\\n"\n'
    '        "proxyCurve=%u\\nframeConfidence=%.9g\\n"\n'
    '        "preExposure=%.9g\\nhasPreExposure=%d\\nscalarExposureScale=%.9g\\nhasExposureScale=%d\\n"\n',
    "stage metadata fields",
)
stage = replace_once(
    stage,
    "        static_cast<double>(info.max_luma_ratio), static_cast<double>(info.pre_exposure),\n"
    "        info.has_pre_exposure ? 1 : 0, static_cast<double>(info.exposure_scale),\n",
    "        static_cast<double>(info.max_luma_ratio), info.work_mode,\n"
    "        static_cast<double>(info.work_linear_scale),\n"
    "        static_cast<double>(info.lighting_lock),\n"
    "        static_cast<double>(info.edge_guard), info.proxy_curve,\n"
    "        static_cast<double>(info.frame_confidence),\n"
    "        static_cast<double>(info.pre_exposure),\n"
    "        info.has_pre_exposure ? 1 : 0, static_cast<double>(info.exposure_scale),\n",
    "stage metadata arguments",
)
stage_path.write_text(stage, encoding="utf-8", newline="\n")

config_path = ROOT / "config" / "nr_before_sr.ini"
config = config_path.read_text(encoding="utf-8")
config = config.replace("; beta0.4 perfect-chain experiment.",
                        "; beta0.4 perfect-chain r2 experiment.", 1)
config = config.replace(
    "; Multiplicative strength: 1 follows the model, 2 compounds twice, 4 is exaggerated.",
    "; Multiplicative strength: 1 normal, 2 doubled, 4 strong, 8 extreme/diagnostic.",
    1,
)
config = config.replace("MaxLumaRatio=4.00",
                        "; Hard powered-ratio ceiling; keep 4 for normal testing.\nMaxLumaRatio=4.00", 1)
config_path.write_text(config, encoding="utf-8", newline="\n")

build_path = ROOT / "scripts" / "build.ps1"
build = build_path.read_text(encoding="utf-8").replace(
    "beta0.4-perfect-chain-r1", "beta0.4-perfect-chain-r2")
build_path.write_text(build, encoding="utf-8", newline="\n")

rc_path = ROOT / "src" / "nr_before_sr.rc"
rc = rc_path.read_text(encoding="utf-8")
rc = rc.replace("beta0.4-perfect-chain-r1", "beta0.4-perfect-chain-r2")
rc = rc.replace("FILEVERSION 0,4,0,1", "FILEVERSION 0,4,0,2")
rc = rc.replace("PRODUCTVERSION 0,4,0,1", "PRODUCTVERSION 0,4,0,2")
rc = rc.replace('"0.4.0.1 perfect chain r1\\0"',
                '"0.4.0.2 perfect chain r2\\0"')
rc_path.write_text(rc, encoding="utf-8", newline="\n")

validator_path = ROOT / "scripts" / "validate-beta04-structure.py"
validator = validator_path.read_text(encoding="utf-8")
validator = validator.replace(
    '    \'kBuildIdentity = "beta0.4-perfect-chain-r1"\',',
    '    \'kBuildIdentity = "beta0.4-perfect-chain-r2"\',',
)
validator = validator.replace(
    '    \'SuppressResetFrame\',\n',
    '    \'SuppressResetFrame\',\n'
    '    \'modelValueDomain\',\n'
    '    \'Maximum luminance ratio\',\n'
    '    \'should_sample_delta\',\n',
)
validator = validator.replace(
    'if "logRatio * TransferStrength" not in body:\n'
    '    raise SystemExit("multiplicative log-ratio strength missing")\n',
    'if "logRatio * TransferStrength" not in body:\n'
    '    raise SystemExit("multiplicative log-ratio strength missing")\n'
    'if "modelValueDomain ? saturate(proxyValue)" not in body:\n'
    '    raise SystemExit("bounded R8 value-domain preservation missing")\n'
    'if "std::clamp(config.transfer_strength, 0.0f, 8.0f)" not in source:\n'
    '    raise SystemExit("8x strength ceiling missing")\n'
    'if "using the full render raster" not in source:\n'
    '    raise SystemExit("unresolved output extent must fall back to reference")\n',
)
validator_path.write_text(validator, encoding="utf-8", newline="\n")

math_test = r'''#!/usr/bin/env python3
from __future__ import annotations

import math


def powered_ratio(proxy: float, neural: float, strength: float,
                  confidence: float, max_ratio: float) -> float:
    proxy = max(proxy, 1.0 / 4096.0)
    neural = max(neural, 1.0 / 4096.0)
    log_ratio = max(-8.0, min(8.0, math.log2(neural / proxy)))
    limit = math.log2(max(max_ratio, 1.0))
    powered = max(-limit, min(limit, log_ratio * strength * confidence))
    return 2.0 ** powered


def target_luma(source: float, full_proxy: float, ratio: float,
                max_ratio: float) -> float:
    full_proxy = max(full_proxy, 1.0 / 4096.0)
    model_target = full_proxy * ratio
    target = (model_target + (source - full_proxy)
              if source > full_proxy
              else model_target * source / full_proxy)
    if source > 1.0 / 65536.0:
        target = max(source / max_ratio, min(source * max_ratio, target))
    else:
        target = source
    return target


# Strength zero and a no-op model must be exact identities.
for source in (0.001, 0.05, 0.5, 1.0, 4.0, 20.0):
    assert math.isclose(powered_ratio(0.5, 1.0, 0.0, 1.0, 16.0), 1.0)
    assert math.isclose(powered_ratio(0.5, 0.5, 8.0, 1.0, 16.0), 1.0)
    assert math.isclose(target_luma(source, min(source, 0.8), 1.0, 16.0), source)

# Integer strength is mathematically repeated multiplicative application until
# the explicit maximum-ratio guard becomes authoritative.
base = 1.18
for strength in (1.0, 2.0, 4.0, 8.0):
    actual = powered_ratio(1.0, base, strength, 1.0, 16.0)
    expected = min(16.0, base ** strength)
    assert math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-12)

# Positive and negative model changes remain monotonic as strength increases.
positive = [powered_ratio(1.0, 1.1, s, 1.0, 16.0)
            for s in (0, 1, 2, 4, 8)]
negative = [powered_ratio(1.0, 0.9, s, 1.0, 16.0)
            for s in (0, 1, 2, 4, 8)]
assert positive == sorted(positive)
assert negative == sorted(negative, reverse=True)

# Confidence continuously fades the exponent rather than blending two already
# clipped outputs.
conf = [powered_ratio(1.0, 1.5, 4.0, c, 16.0)
        for c in (0.0, 0.25, 0.5, 0.75, 1.0)]
assert conf == sorted(conf)
assert math.isclose(conf[0], 1.0)

# HDR headroom above the model proxy is additive: a neutral model leaves it
# untouched, while the model only edits the bounded proxy component.
source = 6.0
proxy = 0.9
for ratio in (0.5, 1.0, 2.0, 4.0):
    target = target_luma(source, proxy, ratio, 16.0)
    expected = proxy * ratio + (source - proxy)
    assert math.isclose(target, expected, rel_tol=1e-12, abs_tol=1e-12)

# The two composition branches meet continuously at source == full proxy.
for ratio in (0.25, 0.75, 1.0, 2.0, 8.0):
    p = 0.8
    left = target_luma(p * (1.0 - 1e-9), p, ratio, 16.0)
    right = target_luma(p * (1.0 + 1e-9), p, ratio, 16.0)
    assert abs(left - right) < 1e-7

# The hard ratio guard contains all extreme model/strength combinations.
for source in (0.001, 0.1, 1.0, 10.0):
    for neural in (1e-9, 1e-4, 1.0, 1e4):
        ratio = powered_ratio(0.5, neural, 8.0, 1.0, 4.0)
        target = target_luma(source, min(source, 0.8), ratio, 4.0)
        assert math.isfinite(target)
        assert source / 4.0 - 1e-12 <= target <= source * 4.0 + 1e-12

print("beta0.4-r2 multiplicative/HDR math validation passed")
'''
(ROOT / "scripts" / "test-beta04-math.py").write_text(
    math_test, encoding="utf-8", newline="\n")

validation_workflow = r'''name: Validate beta0.4 perfect-chain r2

on:
  workflow_dispatch:
  push:
    branches:
      - codex/beta0.4-perfect-chain

permissions:
  contents: read

concurrency:
  group: beta04-r2-${{ github.ref }}
  cancel-in-progress: true

jobs:
  shader-structure-and-math:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Structural invariants
        run: python scripts/validate-beta04-structure.py
      - name: Multiplicative and HDR math invariants
        run: python scripts/test-beta04-math.py
      - name: Compile every embedded Shader Model 5 entry
        shell: pwsh
        run: ./scripts/validate-codec-shader.ps1

  full-build-and-package:
    needs: shader-structure-and-math
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Fetch public build inputs
        shell: pwsh
        run: |
          $ErrorActionPreference = 'Stop'
          $headers = @{ 'User-Agent' = 'beta04-r2-validator' }
          $release = Invoke-RestMethod -Headers $headers `
            'https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest'
          $asset = $release.assets | Where-Object {
            $_.name -match '^llvm-mingw-.*-ucrt-x86_64\.zip$'
          } | Select-Object -First 1
          if (-not $asset) { throw 'No llvm-mingw UCRT x86_64 archive found.' }
          Invoke-WebRequest $asset.browser_download_url -OutFile llvm-mingw.zip
          Expand-Archive llvm-mingw.zip -DestinationPath toolchain
          $toolchain = (Get-ChildItem toolchain -Directory | Select-Object -First 1).FullName
          git clone --depth 1 --recurse-submodules --shallow-submodules `
            https://github.com/crosire/reshade.git reshade-source
          "TOOLCHAIN_ROOT=$toolchain" | Out-File -FilePath $env:GITHUB_ENV -Append
          "RESHADE_ROOT=$pwd\reshade-source" | Out-File -FilePath $env:GITHUB_ENV -Append
      - name: Build English and Chinese add-ons
        shell: pwsh
        run: ./scripts/build.ps1 -Clean -ToolchainRoot $env:TOOLCHAIN_ROOT -ReShadeSourceRoot $env:RESHADE_ROOT
      - name: Assemble clean test package
        shell: pwsh
        run: |
          New-Item -ItemType Directory -Path dist -Force | Out-Null
          Copy-Item build/*.addon64 dist/
          Copy-Item build/nrchain_nvngx.dll dist/
          Copy-Item build/nr_before_sr.ini dist/
          Copy-Item docs/BETA_0_4_TEST_MATRIX.md dist/README-TESTING.md
          Copy-Item docs/BETA_0_4_PERFECT_CHAIN.md dist/TECHNICAL-NOTES.md
          Get-ChildItem dist -File | Get-FileHash -Algorithm SHA256 |
            Format-Table -AutoSize
      - uses: actions/upload-artifact@v4
        with:
          name: beta04-perfect-chain-r2-test-package
          path: dist/
          if-no-files-found: error
'''
(ROOT / ".github" / "workflows" / "beta04-validate.yml").write_text(
    validation_workflow, encoding="utf-8", newline="\n")

test_matrix = r'''# beta0.4 Perfect Chain r2 实机验证表

分支：`codex/beta0.4-perfect-chain`

## 安装

只使用构建包中的一种语言版 `.addon64`，并把 `nrchain_nvngx.dll`、`nr_before_sr.ini` 与你已有的签名 `nvngx_dlssnr.dll` 放在同一个 ReShade Add-ons 目录。不要同时加载中英文两个插件，也不要同时开启 ShortFuse/RenoDX 的 Direct Neural Rendering。

## 第一轮：先确认链路正确

1. 游戏内关闭 Ray Reconstruction，只保留普通 DLSS Super Resolution。
2. `Mode=2`，工作分辨率选 `Reference`，效果强度 `1.00`，最大亮度倍率 `4.00`，颜色跟随 `0`。
3. 观察面板必须显示 `Feature 18 success` 持续增加，且 `transfer recorded / bound / submitted` 三项都增加。
4. 用 F6 比较插件开关；关闭时应是真正的纯游戏 SR，不执行 Feature 18。
5. 按主键盘 `-` 截显示画面，再点一次 `Capture NR stages once` 采集原图、代理、NR 输出和合成图。

## 第二轮：性能档位

固定同一镜头、同一 DLSS 档位，依次测试：

- Reference：质量基准。
- Quality：推荐默认。
- Balanced：优先测试的性能档。
- Performance：观察细纹理、文字、发丝、远景闪烁。
- Ultra Performance：只作为极限性能/稳定性实验。

记录面板中的 `NR 工作分辨率 -> render -> SR output`、平均帧率、1% Low 与 GPU 占用。若面板无法证明 SR 输出尺寸，r2 会自动回到完整渲染分辨率，不会偷偷按渲染分辨率再降到 58%。

## 第三轮：乘算强度

在 Reference 或 Quality 下依次测试 `1 / 2 / 4 / 8`：

- 1：模型原始比例。
- 2：等价于把模型的明暗倍率乘算两次。
- 4：强效果。
- 8：极限诊断，不建议作为默认。

先保持最大亮度倍率 `4`，这样 8× 强度仍被硬限制；只有为了观察极端上限时再逐步提高到 `8` 或 `16`。颜色跟随先保持 `0`，确认亮度/结构稳定后再试 `0.5 / 1 / 2`。

## 必测压力场景

- HDR 关闭/开启与快速室内外曝光变化。
- DLSS Quality/Balanced/Performance 切换。
- DRS 波动与分辨率重建。
- RR 关闭 -> 开启 -> 关闭；若游戏保留 RR handle，按提示重启后再测。
- 镜头切换、读档、过场、快速转身。
- 霓虹灯、太阳、火焰等高亮运动物体。
- 人脸、头发、栅栏、树叶、电线、HUD/字幕。

## 反馈材料

每个异常至少保留：

- `nr-before-sr.log`
- 显示画面截图
- 对应一次 stage capture 文件夹及 `metadata.txt`
- 游戏名、分辨率、HDR、DLSS 档位、显卡/驱动
- WorkMode、Strength、MaxLumaRatio、LightingLock、EdgeGuard、ProxyCurve、ColorStrength

若发生崩溃，请同时说明是在启动、Feature 18 创建、切档、开关 RR、读档还是退出阶段。
'''
(ROOT / "docs" / "BETA_0_4_TEST_MATRIX.md").write_text(
    test_matrix, encoding="utf-8", newline="\n")

notes_path = ROOT / "docs" / "BETA_0_4_PERFECT_CHAIN.md"
notes = notes_path.read_text(encoding="utf-8")
notes = notes.replace("beta0.4 Perfect Chain experiment",
                      "beta0.4 Perfect Chain r2 experiment", 1)
notes = notes.replace("`4`: deliberately exaggerated experimental result.",
                      "`4`: deliberately strong experimental result.\n- `8`: extreme diagnostic ceiling; keep the ratio guard conservative.", 1)
notes = notes.replace("Reference / 4.0 strength / Hybrid.",
                      "Reference / 4.0 strength / Hybrid.\n6. Reference / 8.0 strength with MaxLumaRatio still at 4.\n7. Reversible proxy only in a fixed bright-light scene.", 1)
notes = notes.replace("6. Reversible proxy only in a fixed bright-light scene.\n", "")
notes += "\n## r2 audit fixes\n\n- Scaled bounded-R8 inputs remain in their verified value domain instead of receiving a second sRGB encode.\n- Output-relative presets fail safely to the full render raster when the live SR output extent cannot be proven.\n- Lightweight GPU delta diagnostics are sampled after changes and periodically rather than every frame.\n- Lighting-lock and edge-guard composition reduce redundant full-resolution loads.\n- Strength now reaches 8x, while the independently adjustable 1–16 maximum luminance ratio remains the hard safety ceiling.\n"
notes_path.write_text(notes, encoding="utf-8", newline="\n")

# Remove one-time generation scaffolding and noisy compiler transcripts. The
# final branch keeps readable source plus reproducible validation only.
remove = [
    ROOT / ".github" / "workflows" / "beta04-self-patch.yml",
    ROOT / "scripts" / "apply-beta04-perfect-chain.py",
    ROOT / "scripts" / "fix-beta04-hlsl.py",
    ROOT / "docs" / "BETA_0_4_BUILD_REPORT.txt",
    ROOT / "docs" / "BETA_0_4_SHADER_REPORT.txt",
    ROOT / "docs" / "BETA_0_4_VALIDATION_REPORT.txt",
]
remove.extend((ROOT / "scripts" / "beta04-payload").glob("part*.txt"))
for path in remove:
    if path.exists():
        path.unlink()
try:
    (ROOT / "scripts" / "beta04-payload").rmdir()
except OSError:
    pass

# Delete this one-shot finalizer last. The running Python process already holds
# the code; its deletion is committed with the final source state.
Path(__file__).unlink()
print(f"finalized {BRANCH_VERSION}")
