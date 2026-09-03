#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "nr_before_sr.cpp"
GUIDES = ROOT / "src" / "nr_guide_contract.h"
VALIDATOR = ROOT / "scripts" / "validate-beta04-structure.py"
MATH_TEST = ROOT / "scripts" / "test-beta04-math.py"
DOC = ROOT / "docs" / "BETA_0_4_PERFECT_CHAIN.md"
MATRIX = ROOT / "docs" / "BETA_0_4_TEST_MATRIX.md"
CONFIG = ROOT / "config" / "nr_before_sr.ini"
RC = ROOT / "src" / "nr_before_sr.rc"
BUILD = ROOT / "scripts" / "build.ps1"
WORKFLOW = ROOT / ".github" / "workflows" / "beta04-validate.yml"
README = ROOT / "README.md"
MARKER = "beta0.4-perfect-chain-r3"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def patch_source(text: str) -> str:
    if MARKER in text:
        return text
    text = text.replace("beta0.4-perfect-chain-r2", MARKER)
    text = text.replace("beta0.4 perfect-chain r2", "beta0.4 perfect-chain r3")

    old_gamut = '''float3 CompressNegativeTowardNeutral(float3 color) {
    if (all(color >= 0.0.xxx)) return color;
    float y = max(dot(color, kLuma), 0.0);
    float3 neutral = y.xxx;
    float scale = 1.0;
    if (color.r < 0.0 && neutral.r > color.r)
        scale = min(scale, neutral.r / max(neutral.r - color.r, 0.000001));
    if (color.g < 0.0 && neutral.g > color.g)
        scale = min(scale, neutral.g / max(neutral.g - color.g, 0.000001));
    if (color.b < 0.0 && neutral.b > color.b)
        scale = min(scale, neutral.b / max(neutral.b - color.b, 0.000001));
    return neutral + (color - neutral) * saturate(scale);
}
'''
    new_gamut = old_gamut + '''
// Bounded SDR cannot retain values outside [0,1]. A per-channel saturate would
// change hue precisely when 2x-8x colour strength is most likely to leave the
// display cube. Move the colour toward the equal-luminance neutral axis by one
// scalar instead. HDR/PQ paths keep their positive headroom and use only the
// negative-side guard above.
float3 CompressBoundedTowardNeutral(float3 color) {
    if (!all(color == color)) return 0.0.xxx;
    float y = dot(color, kLuma);
    if (!(y > 0.0)) return 0.0.xxx;
    if (y > 1.0) {
        color *= rcp(y);
        y = 1.0;
    }
    float3 neutral = y.xxx;
    float scale = 1.0;
    if (color.r < 0.0 && neutral.r > color.r)
        scale = min(scale, neutral.r / max(neutral.r - color.r, 0.000001));
    if (color.g < 0.0 && neutral.g > color.g)
        scale = min(scale, neutral.g / max(neutral.g - color.g, 0.000001));
    if (color.b < 0.0 && neutral.b > color.b)
        scale = min(scale, neutral.b / max(neutral.b - color.b, 0.000001));
    if (color.r > 1.0 && color.r > neutral.r)
        scale = min(scale, (1.0 - neutral.r) / max(color.r - neutral.r, 0.000001));
    if (color.g > 1.0 && color.g > neutral.g)
        scale = min(scale, (1.0 - neutral.g) / max(color.g - neutral.g, 0.000001));
    if (color.b > 1.0 && color.b > neutral.b)
        scale = min(scale, (1.0 - neutral.b) / max(color.b - neutral.b, 0.000001));
    return saturate(neutral + (color - neutral) * saturate(scale));
}
'''
    text = replace_once(text, old_gamut, new_gamut, "bounded neutral-axis gamut guard")
    text = replace_once(
        text,
        '''    result = CompressNegativeTowardNeutral(result);

    float3 storedResult = EncodeCompositionToStoredV2(result);
''',
        '''    const bool boundedSdrStorage = modelEncodedInput ||
        linearSdrStorage || srgbSdrStorage;
    result = boundedSdrStorage
        ? CompressBoundedTowardNeutral(result)
        : CompressNegativeTowardNeutral(result);

    float3 storedResult = EncodeCompositionToStoredV2(result);
''',
        "select bounded/HDR gamut guard",
    )

    align_anchor = '''unsigned int AlignWorkExtent(unsigned int value, unsigned int alignment,
                             unsigned int ceiling) {
    if (ceiling == 0) return 0;
    value = std::min(value, ceiling);
    if (value < alignment) return value;
    unsigned int aligned = value - value % alignment;
    return aligned != 0 ? aligned : std::min(value, alignment);
}
'''
    align_new = align_anchor + '''
void SelectAlignedWorkExtent(unsigned int desired_width,
                             unsigned int desired_height,
                             unsigned int render_width,
                             unsigned int render_height,
                             unsigned int &selected_width,
                             unsigned int &selected_height) {
    const unsigned int width_floor = AlignWorkExtent(
        desired_width, 32u, render_width);
    const unsigned int height_floor = AlignWorkExtent(
        desired_height, 8u, render_height);
    const unsigned int width_ceil = width_floor < desired_width
        ? std::min(render_width, width_floor + 32u) : width_floor;
    const unsigned int height_ceil = height_floor < desired_height
        ? std::min(render_height, height_floor + 8u) : height_floor;
    const unsigned int widths[2] = {
        std::max(1u, width_floor), std::max(1u, width_ceil)};
    const unsigned int heights[2] = {
        std::max(1u, height_floor), std::max(1u, height_ceil)};
    const double target_aspect = static_cast<double>(render_width) /
        std::max(render_height, 1u);
    const double target_area = std::max(
        static_cast<double>(desired_width) * desired_height, 1.0);
    double best_score = std::numeric_limits<double>::infinity();
    selected_width = widths[0];
    selected_height = heights[0];
    for (unsigned int candidate_width : widths) {
        for (unsigned int candidate_height : heights) {
            const double candidate_aspect =
                static_cast<double>(candidate_width) / candidate_height;
            const double candidate_area =
                static_cast<double>(candidate_width) * candidate_height;
            // Preserve geometry first, then stay close to the requested cost.
            const double aspect_error = std::fabs(std::log2(
                candidate_aspect / target_aspect));
            const double area_error = std::fabs(std::log2(
                candidate_area / target_area));
            const double score = aspect_error * 12.0 + area_error;
            if (score < best_score) {
                best_score = score;
                selected_width = candidate_width;
                selected_height = candidate_height;
            }
        }
    }
}
'''
    text = replace_once(text, align_anchor, align_new, "paired work-raster alignment")
    text = replace_once(
        text,
        '''    result.work_width = AlignWorkExtent(desired_width, 32u, render_width);
    result.work_height = AlignWorkExtent(desired_height, 8u, render_height);
    result.work_width = std::max(1u, result.work_width);
    result.work_height = std::max(1u, result.work_height);
    result.linear_scale = std::min(
        static_cast<float>(result.work_width) / render_width,
        static_cast<float>(result.work_height) / render_height);
''',
        '''    SelectAlignedWorkExtent(desired_width, desired_height,
        render_width, render_height, result.work_width, result.work_height);
    const double work_area = static_cast<double>(result.work_width) *
        result.work_height;
    const double render_area = static_cast<double>(render_width) *
        render_height;
    result.linear_scale = static_cast<float>(std::sqrt(
        work_area / std::max(render_area, 1.0)));
''',
        "use paired work-raster alignment",
    )

    text = replace_once(
        text,
        '''    bool valid_motion_scale_x = false, valid_motion_scale_y = false;
    int game_reset = 0;
};
''',
        '''    bool valid_motion_scale_x = false, valid_motion_scale_y = false;
    float jitter_offset_x = 0.0f;
    float jitter_offset_y = 0.0f;
    int game_reset = 0;
};
''',
        "jitter guide fields",
    )
    text = replace_once(
        text,
        '''    if (!binding.valid_motion_scale_x) s.motion_scale_x = 1.0f;
    if (!binding.valid_motion_scale_y) s.motion_scale_y = 1.0f;
    // Resampling changes locations, never the units of stored vectors.
    s.normalized_create_flags = feature.create_flags;
''',
        '''    if (!binding.valid_motion_scale_x) s.motion_scale_x = 1.0f;
    if (!binding.valid_motion_scale_y) s.motion_scale_y = 1.0f;

    // The game's MV.Scale converts stored vectors into the game's DLSS source
    // texel space. Mode 2 may use a smaller Feature 18 source, so convert those
    // displacements into NR working-raster texels after spatially resampling
    // the vector field. Reference mode remains exactly 1:1.
    const float source_to_work_x = mode == 2 && guide_width != 0
        ? static_cast<float>(width) / guide_width : 1.0f;
    const float source_to_work_y = mode == 2 && guide_height != 0
        ? static_cast<float>(height) / guide_height : 1.0f;
    s.motion_scale_x *= source_to_work_x;
    s.motion_scale_y *= source_to_work_y;

    float jitter_x = 0.0f, jitter_y = 0.0f;
    if (!GetFloat(params, "Jitter.Offset.X", &jitter_x) ||
        !std::isfinite(jitter_x)) jitter_x = 0.0f;
    if (!GetFloat(params, "Jitter.Offset.Y", &jitter_y) ||
        !std::isfinite(jitter_y)) jitter_y = 0.0f;
    // Jitter is also expressed in source-image pixels. Keep it in lockstep
    // with the colour and motion coordinate system seen by Feature 18.
    binding.jitter_offset_x = std::clamp(
        jitter_x * source_to_work_x, -2.0f, 2.0f);
    binding.jitter_offset_y = std::clamp(
        jitter_y * source_to_work_y, -2.0f, 2.0f);

    s.normalized_create_flags = feature.create_flags;
''',
        "motion/jitter source-to-work conversion",
    )
    text = replace_once(
        text,
        '''    SetFloat(params, "DLSSNR.MVecScaleX", mv_scale_x);
    SetFloat(params, "DLSSNR.MVecScaleY", mv_scale_y);

    const int depth_inverted = s.depth_inverted;
''',
        '''    SetFloat(params, "DLSSNR.MVecScaleX", mv_scale_x);
    SetFloat(params, "DLSSNR.MVecScaleY", mv_scale_y);
    SetFloat(params, "Jitter.Offset.X", guides.jitter_offset_x);
    SetFloat(params, "Jitter.Offset.Y", guides.jitter_offset_y);

    const int depth_inverted = s.depth_inverted;
''',
        "bind scaled jitter",
    )
    return text


def patch_guides(text: str) -> str:
    old_storage = '''    return a.depth_physical_width == b.depth_physical_width &&
           a.depth_physical_height == b.depth_physical_height &&
           a.depth_format == b.depth_format &&
           a.motion_physical_width == b.motion_physical_width &&
           a.motion_physical_height == b.motion_physical_height &&
           a.motion_format == b.motion_format &&
           a.color_physical_width == b.color_physical_width &&
           a.color_physical_height == b.color_physical_height &&
           a.color_format == b.color_format &&
'''
    new_storage = '''    return a.depth_format == b.depth_format &&
           a.motion_format == b.motion_format &&
           a.color_format == b.color_format &&
'''
    text = replace_once(text, old_storage, new_storage,
                        "remove physical DRS allocations from context identity")
    text = replace_once(
        text,
        '''    return SameNrGuideStorageContract(a, b) &&
           a.depth_width == b.depth_width && a.depth_height == b.depth_height &&
''',
        '''    return SameNrGuideStorageContract(a, b) &&
           a.depth_physical_width == b.depth_physical_width &&
           a.depth_physical_height == b.depth_physical_height &&
           a.motion_physical_width == b.motion_physical_width &&
           a.motion_physical_height == b.motion_physical_height &&
           a.color_physical_width == b.color_physical_width &&
           a.color_physical_height == b.color_physical_height &&
           a.depth_width == b.depth_width && a.depth_height == b.depth_height &&
''',
        "physical DRS allocations remain history reset boundaries",
    )
    return text


def patch_validator(text: str) -> str:
    text = text.replace('beta0.4-perfect-chain-r2', MARKER)
    text = replace_once(
        text,
        '''    'should_sample_delta',
]''',
        '''    'should_sample_delta',
    'SelectAlignedWorkExtent',
    'source_to_work_x',
    'Jitter.Offset.X',
    'CompressBoundedTowardNeutral',
]''',
        "r3 structural markers",
    )
    text = replace_once(
        text,
        '''for token in ("depth_width", "motion_source_width"):
    if token in storage.group(0):
        raise SystemExit(f"{token} must not be part of storage identity")
    if token not in mapping.group(0):
        raise SystemExit(f"{token} must remain part of history mapping")
''',
        '''for token in ("depth_width", "motion_source_width",
              "depth_physical_width", "motion_physical_width",
              "color_physical_width"):
    if token in storage.group(0):
        raise SystemExit(f"{token} must not be part of storage identity")
    if token not in mapping.group(0):
        raise SystemExit(f"{token} must remain part of history mapping")
''',
        "r3 DRS identity validation",
    )
    text = replace_once(
        text,
        '''if "CompressNegativeTowardNeutral" not in body:
    raise SystemExit("neutral-axis gamut protection missing")
''',
        '''if ("CompressNegativeTowardNeutral" not in body or
        "CompressBoundedTowardNeutral" not in body):
    raise SystemExit("neutral-axis HDR/SDR gamut protection missing")
if "source_to_work_x" not in source or "Jitter.Offset.X" not in source:
    raise SystemExit("motion/jitter source-to-work conversion missing")
''',
        "r3 gamut and guide validation",
    )
    return text


def patch_math(text: str) -> str:
    text = text.replace(
        'print("beta0.4-r2 multiplicative/HDR math validation passed")',
        '''# Motion vectors and jitter are expressed in source-image pixels.
# A smaller Feature 18 source therefore needs proportionally smaller scales.
def source_to_work(value: float, work: int, render: int) -> float:
    return value * work / render

assert math.isclose(source_to_work(1.0, 1440, 1920), 0.75)
assert math.isclose(source_to_work(2.0, 1280, 2560), 1.0)
assert math.isclose(source_to_work(0.25, 1920, 1920), 0.25)

# Paired alignment must keep geometry much closer to the render aspect than
# flooring width and height independently at awkward scales.
def paired_extent(dw: int, dh: int, rw: int, rh: int) -> tuple[int, int]:
    wf = max(1, min(dw, rw) - min(dw, rw) % 32)
    hf = max(1, min(dh, rh) - min(dh, rh) % 8)
    wc = min(rw, wf + 32) if wf < dw else wf
    hc = min(rh, hf + 8) if hf < dh else hf
    target_aspect = rw / rh
    target_area = max(dw * dh, 1)
    return min(((w, h) for w in (wf, wc) for h in (hf, hc)),
               key=lambda p: abs(math.log2((p[0] / p[1]) / target_aspect)) * 12
               + abs(math.log2((p[0] * p[1]) / target_area)))

for dw, dh, rw, rh in ((1114, 626, 1920, 1080),
                       (2227, 1253, 2560, 1440),
                       (1613, 907, 2560, 1440)):
    w, h = paired_extent(dw, dh, rw, rh)
    assert abs((w / h) / (rw / rh) - 1.0) < 0.012

# Bounded SDR chroma is compressed with one neutral-axis scalar rather than
# per-channel clipping. Every result remains displayable and finite.
def bounded_neutral(color: tuple[float, float, float]) -> tuple[float, float, float]:
    kr, kg, kb = 0.2126, 0.7152, 0.0722
    y = color[0] * kr + color[1] * kg + color[2] * kb
    if not y > 0:
        return (0.0, 0.0, 0.0)
    c = list(color)
    if y > 1:
        c = [v / y for v in c]
        y = 1.0
    scale = 1.0
    for v in c:
        if v < 0 and y > v:
            scale = min(scale, y / max(y - v, 1e-6))
        if v > 1 and v > y:
            scale = min(scale, (1 - y) / max(v - y, 1e-6))
    return tuple(max(0.0, min(1.0, y + (v - y) * scale)) for v in c)

for color in ((1.8, 0.2, -0.1), (0.2, 1.6, 0.1), (4.0, 2.0, 0.5)):
    out = bounded_neutral(color)
    assert all(math.isfinite(v) and 0.0 <= v <= 1.0 for v in out)

print("beta0.4-r3 multiplicative/HDR/guide math validation passed")''',
    )
    return text


def update_versions(text: str) -> str:
    return (text.replace("beta0.4-perfect-chain-r2", MARKER)
                .replace("beta0.4 perfect-chain r2", "beta0.4 perfect-chain r3")
                .replace("beta0.4 Perfect Chain r2", "beta0.4 Perfect Chain r3")
                .replace("beta04-r2", "beta04-r3")
                .replace("beta04-perfect-chain-r2", "beta04-perfect-chain-r3"))


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    if MARKER in source:
        print("beta0.4 r3 audit already applied")
        return 0

    SOURCE.write_text(patch_source(source), encoding="utf-8", newline="\n")
    GUIDES.write_text(patch_guides(GUIDES.read_text(encoding="utf-8")),
                      encoding="utf-8", newline="\n")
    VALIDATOR.write_text(patch_validator(VALIDATOR.read_text(encoding="utf-8")),
                         encoding="utf-8", newline="\n")
    MATH_TEST.write_text(patch_math(MATH_TEST.read_text(encoding="utf-8")),
                         encoding="utf-8", newline="\n")

    for path in (DOC, MATRIX, CONFIG, BUILD, WORKFLOW, README):
        path.write_text(update_versions(path.read_text(encoding="utf-8")),
                        encoding="utf-8", newline="\n")

    doc_text = DOC.read_text(encoding="utf-8")
    doc_text += '''\n## r3 final audit fixes\n\n- Motion-vector scale and camera jitter are converted from the game's render-source pixel domain to the independent Feature 18 working raster. This keeps temporal displacements correct below 100% working resolution.\n- Width/height alignment is selected as a pair, prioritizing the original render aspect ratio before small area differences.\n- Physical guide reallocations caused by DRS reset history but no longer force a new Feature 18 context when formats, working extent and normalized flags are unchanged.\n- Extreme SDR colour follow now compresses both negative and above-one channels toward an equal-luminance neutral axis instead of hard per-channel clipping. HDR positive headroom remains untouched.\n'''
    DOC.write_text(doc_text, encoding="utf-8", newline="\n")

    rc = update_versions(RC.read_text(encoding="utf-8"))
    rc = (rc.replace("FILEVERSION 0,4,0,2", "FILEVERSION 0,4,0,3")
            .replace("PRODUCTVERSION 0,4,0,2", "PRODUCTVERSION 0,4,0,3")
            .replace("0.4.0.2 perfect chain r2", "0.4.0.3 perfect chain r3"))
    RC.write_text(rc, encoding="utf-8", newline="\n")

    print("beta0.4 perfect-chain r3 audit patch applied")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"r3 audit patch failed: {exc}", file=sys.stderr)
        raise
