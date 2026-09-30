#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "tests" / "codec_gpu_harness.cpp.in"
MARKER = "Create V2 compute pipeline"


def one(text: str, pattern: str, replacement: str, label: str, flags: int = 0) -> str:
    out, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return out


def literal(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = PATH.read_text(encoding="utf-8")
    if MARKER in text:
        print("beta0.4 V2 GPU harness already upgraded")
        return 0

    text = literal(
        text,
        "    Com<ID3D12PipelineState> pipeline;\n",
        "    Com<ID3D12PipelineState> pipeline;\n"
        "    Com<ID3D12PipelineState> v2_pipeline;\n",
        "V2 pipeline member",
    )

    token = '"Create compute pipeline");'
    pos = text.find(token)
    if pos < 0:
        raise RuntimeError("legacy compute pipeline anchor missing")
    pos += len(token)
    v2_compile = r'''

        Com<ID3DBlob> v2_shader, v2_errors;
        result = D3DCompile(kCodecShader, sizeof(kCodecShader) - 1,
            "production-codec-v2", nullptr, nullptr, "CombineV2CS", "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &v2_shader.p, &v2_errors.p);
        if (FAILED(result) && v2_errors.p != nullptr)
            std::fprintf(stderr, "%s\n",
                static_cast<const char *>(v2_errors->GetBufferPointer()));
        Check(result, "Compile production CombineV2CS");
        pipeline_desc.CS = {v2_shader->GetBufferPointer(),
                            v2_shader->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pipeline_desc,
              IID_ID3D12PipelineState, v2_pipeline.address()),
              "Create V2 compute pipeline");'''
    text = text[:pos] + v2_compile + text[pos:]

    text = one(
        text,
        r"RunResult Run\(Gpu &gpu, UINT width, UINT height, UINT stride,\s*\n\s*UINT requested_stride, UINT flags, int variant\) \{",
        "RunResult Run(Gpu &gpu, UINT width, UINT height, UINT stride,\n"
        "              UINT requested_stride, UINT flags, int variant,\n"
        "              bool v2 = false, UINT requested_model_width = 0,\n"
        "              UINT requested_model_height = 0) {",
        "Run signature",
    )

    text = literal(
        text,
        "    constexpr UINT sx = 3, sy = 2, dx = 2, dy = 1;\n"
        "    const UINT source_width = width + sx + 2, source_height = height + sy + 2;\n",
        "    constexpr UINT sx = 3, sy = 2, dx = 2, dy = 1;\n"
        "    const UINT model_width = requested_model_width != 0\n"
        "        ? requested_model_width : width;\n"
        "    const UINT model_height = requested_model_height != 0\n"
        "        ? requested_model_height : height;\n"
        "    const UINT source_width = width + sx + 2, source_height = height + sy + 2;\n",
        "model extents",
    )

    text = literal(
        text,
        "    std::vector<Float4> proxy(width * height), neural(width * height);\n",
        "    std::vector<Float4> proxy(model_width * model_height),\n"
        "                        neural(model_width * model_height);\n",
        "model vectors",
    )

    text = one(
        text,
        r"    for \(UINT y = 0; y < height; \+\+y\)\s*\n\s*for \(UINT x = 0; x < width; \+\+x\) \{\s*\n\s*proxy\[y \* width \+ x\] = \{0\.25f, 0\.375f, 0\.5f, 0\.75f\};\s*\n\s*neural\[y \* width \+ x\] = variant == 1 \? proxy\[y \* width \+ x\]\s*\n\s*: Float4\{0\.3f, 0\.45f, 0\.6f, 0\.875f\};\s*\n\s*\}",
        "    for (UINT y = 0; y < model_height; ++y)\n"
        "        for (UINT x = 0; x < model_width; ++x) {\n"
        "            proxy[y * model_width + x] = {0.25f, 0.375f, 0.5f, 0.75f};\n"
        "            neural[y * model_width + x] = variant == 1\n"
        "                ? proxy[y * model_width + x]\n"
        "                : Float4{0.3f, 0.45f, 0.6f, 0.875f};\n"
        "        }",
        "model data initialization",
    )

    text = literal(
        text,
        "    auto proxy_texture = gpu.MakeTexture(width, height, proxy);\n"
        "    auto neural_texture = gpu.MakeTexture(width, height, neural);\n",
        "    auto proxy_texture = gpu.MakeTexture(model_width, model_height, proxy);\n"
        "    auto neural_texture = gpu.MakeTexture(model_width, model_height, neural);\n",
        "model textures",
    )

    text = one(
        text,
        r"    const CodecConstants constants = \{width, height, 1\.0f,\s*\n\s*variant == 2 \? 0\.0f : 0\.8f, 1\.0f, 0\.25f, 2\.0f, flags,\s*\n\s*sx, sy, dx, dy, 0, 0, 0, 0, 1\.0f, stride\};",
        "    const float transfer_strength = variant == 2 ? 0.0f\n"
        "        : (variant == 5 ? 8.0f : 0.8f);\n"
        "    const float color_strength = variant == 5 ? 2.0f : 0.25f;\n"
        "    const float max_luma_ratio = variant == 5 ? 16.0f : 2.0f;\n"
        "    const float frame_confidence = variant == 4 ? 0.0f : 1.0f;\n"
        "    const CodecConstants constants = {width, height, 1.0f,\n"
        "        transfer_strength, 1.0f, color_strength, max_luma_ratio, flags,\n"
        "        sx, sy, dx, dy, 0, 0, 0, 0, 1.0f, stride,\n"
        "        width, height, model_width, model_height,\n"
        "        0.0f, 0.0f, 1u, frame_confidence};",
        "26-field constants",
    )

    text = literal(
        text,
        "    gpu.list->SetPipelineState(gpu.pipeline.p);\n",
        "    gpu.list->SetPipelineState(v2 ? gpu.v2_pipeline.p : gpu.pipeline.p);\n",
        "pipeline selection",
    )

    text = one(
        text,
        r"\s*if \(variant == 1 \|\| variant == 2 \|\| \(variant == 3 && x == dx && y == dy\)\)\s*\n\s*Require\(std::memcmp\(&actual, &expected, 16\) == 0,\s*\n\s*\"Identity, zero-strength or invalid-neural fallback changed source\"\);",
        "\n                if (variant == 1 || variant == 2 || variant == 4 ||\n"
        "                    (variant == 3 && x == dx && y == dy))\n"
        "                    Require(std::memcmp(&actual, &expected, 16) == 0,\n"
        "                            \"Identity, zero-strength, reset suppression or invalid-neural fallback changed source\");\n"
        "                if (v2 && variant == 5)\n"
        "                    Require(actual.x >= 0.0f && actual.x <= 1.0f &&\n"
        "                            actual.y >= 0.0f && actual.y <= 1.0f &&\n"
        "                            actual.z >= 0.0f && actual.z <= 1.0f,\n"
        "                            \"8x V2 colour escaped the bounded SDR cube\");",
        "fallback/high-strength assertions",
    )

    text = one(
        text,
        r"\s*const Float4 \*expected\[\] = \{\s*\n\s*&original\[\(source_y \+ sy\) \* source_width \+ source_x \+ sx\],\s*\n\s*&proxy\[source_y \* width \+ source_x\],\s*\n\s*&neural\[source_y \* width \+ source_x\],\s*\n\s*&result\.output\[\(source_y \+ dy\) \* output_width \+ source_x \+ dx\]\};",
        "\n                const UINT model_index =\n"
        "                    model_width == width && model_height == height\n"
        "                        ? source_y * model_width + source_x : 0;\n"
        "                const Float4 *expected[] = {\n"
        "                    &original[(source_y + sy) * source_width + source_x + sx],\n"
        "                    &proxy[model_index],\n"
        "                    &neural[model_index],\n"
        "                    &result.output[(source_y + dy) * output_width + source_x + dx]};",
        "scaled capture expectation",
    )

    v2_scenarios = r'''        for (int variant : {0, 1, 2, 4, 5}) {
            const auto off = Run(gpu, 13, 9, 0, 2, 1, variant, true);
            const auto on = Run(gpu, 13, 9, 2, 2, 1, variant, true);
            Require(off.output.size() == on.output.size() &&
                std::memcmp(off.output.data(), on.output.data(),
                    off.output.size() * sizeof(Float4)) == 0,
                "V2 stage capture changes composited output");
            ++scenarios;
        }
        Run(gpu, 13, 9, 0, 2, 1, 3, true);
        ++scenarios;
        for (int variant : {0, 1, 2, 4, 5}) {
            const auto off = Run(gpu, 35, 17, 0, 4, 1, variant,
                                 true, 18, 9);
            const auto on = Run(gpu, 35, 17, 4, 4, 1, variant,
                                true, 18, 9);
            Require(off.output.size() == on.output.size() &&
                std::memcmp(off.output.data(), on.output.data(),
                    off.output.size() * sizeof(Float4)) == 0,
                "Scaled V2 stage capture changes composited output");
            ++scenarios;
        }
'''
    anchor = "        for (UINT stride : {1u, 4u, 8u}) {\n"
    count = text.count(anchor)
    if count != 1:
        raise RuntimeError(f"V2 scenario anchor: expected one match, found {count}")
    text = text.replace(anchor, v2_scenarios + anchor, 1)

    text = literal(
        text,
        '            "capture on/off bit identity, raw four-plane values, alpha, "\n'
        '            "subrect bounds, invalid/zero/identity paths, descriptor and root layout.\\n",\n',
        '            "legacy/V2 capture identity, scaled V2, 8x strength, reset "\n'
        '            "suppression, raw planes, alpha, subrects and root layout.\\n",\n',
        "success summary",
    )

    PATH.write_text(text, encoding="utf-8", newline="\n")
    print("beta0.4 V2 D3D12 GPU harness upgraded")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"GPU harness upgrade failed: {exc}", file=sys.stderr)
        raise
