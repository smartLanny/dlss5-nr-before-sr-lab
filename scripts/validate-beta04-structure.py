#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / "src" / "nr_before_sr.cpp").read_text(encoding="utf-8")
guides = (root / "src" / "nr_guide_contract.h").read_text(encoding="utf-8")
config = (root / "config" / "nr_before_sr.ini").read_text(encoding="utf-8")

# The raw stage deliberately remains uniform3. The final reviewed colour
# exporter stamps unified2; historical dev4 packaging/docs are not active inputs.
current_identity_files = {
    "source": source,
    "resource": (root / "src/nr_before_sr.rc").read_text(encoding="utf-8"),
    "build": (root / "scripts/build.ps1").read_text(encoding="utf-8-sig"),
    "overlay": (root / "scripts/patch-colour-edge.py").read_text(encoding="utf-8"),
    "generator": (root / "scripts/prepare-multi-api-beta.py").read_text(encoding="utf-8"),
    "handoff": (root / "docs/interop/UNIFIED_HANDOFF.md").read_text(encoding="utf-8"),
}
identity_requirements = {
    "source": ['kAddonVersion = "beta0.5-dline21-uniform3"',
               'kBuildIdentity = "beta0.5-dline21-uniform3"'],
    "resource": ["FILEVERSION 0,5,1,26", "PRODUCTVERSION 0,5,1,26",
                 "beta0.5-dline21-uniform3-@"],
    "build": ["DLSS5-AI渲染超分版-beta0.5-dline21-uniform3-@",
              "DLSS5-AI-Rendering-Upscaling-beta0.5-dline21-uniform3-@"],
    "overlay": ["VERSION = 'beta0.5-dline21-unified2'"],
    "generator": ["INTEGRATION_CORE_PINS", "m.VERSION = VERSION"],
    "handoff": ["scripts/build-unified-candidate.ps1", "next/0.5", "#294"],
}
for label, tokens in identity_requirements.items():
    missing_identity = [token for token in tokens if token not in current_identity_files[label]]
    if missing_identity:
        raise SystemExit(f"{label} has stale or incomplete release identity: {missing_identity}")

required = [
    'kAddonVersion = "beta0.5-dline21-uniform3"',
    'kBuildIdentity = "beta0.5-dline21-uniform3"',
    'ID3D12PipelineState *encode_v2',
    'ID3D12PipelineState *combine_v2',
    'void EncodeV2CS(',
    'void CombineV2CS(',
    'ResolveNrWorkExtent(',
    'g_codec.encode_v2',
    'g_codec.combine_v2',
    'AllowUnverifiedHdrColor',
    'SuppressResetFrame',
    'modelValueDomain',
    'should_sample_delta',
    'SelectAlignedWorkExtent',
    'source_to_work_x',
    'Jitter.Offset.X',
    'CompressBoundedTowardNeutral',
]
missing = [item for item in required if item not in source and item not in config]
if missing:
    raise SystemExit("missing beta0.4 markers: " + ", ".join(missing))

match = re.search(r"constexpr UINT kCodecRootConstantCount = (\d+);", source)
if not match or int(match.group(1)) != 60:
    raise SystemExit("kCodecRootConstantCount must be 60")

hlsl = re.search(
    r'const char kCodecShader\[\] = R"HLSL\((?P<body>[\s\S]*?)\)HLSL";',
    source,
)
if not hlsl:
    raise SystemExit("embedded HLSL not found")
body = hlsl.group("body")
cbuffer = re.search(r"cbuffer Constants : register\(b0\) \{(?P<body>[\s\S]*?)\};", body)
cpp = re.search(r"struct CodecConstants \{(?P<body>[\s\S]*?)\n\};", source)
if not cbuffer or not cpp:
    raise SystemExit("constant layouts not found")
# Expand multi-declarations, vectors and the C++ region array into their
# actual DWORD order. Counting only single scalar lines missed newer fields.
def fields(block, language):
    result=[]
    for declaration in block.split(';'):
        declaration=declaration.strip()
        if not declaration:
            continue
        match=re.fullmatch(r'(unsigned int|unsigned|uint2|uint|float)\s+(.+)',declaration,re.S)
        if not match:
            raise SystemExit(f"Unknown {language} constant declaration: {declaration!r}")
        kind, names=match.groups()
        names=re.sub(r'\{[^}]*\}', '0', names)
        for name in names.split(','):
            name=name.split('=')[0].strip()
            if name=='region_gains[11]':
                result += ['RegionWideBrightLuma','RegionWideBrightColor',
                    'RegionFineBrightLuma','RegionFineBrightColor',
                    'RegionWideDarkLuma','RegionWideDarkColor',
                    'RegionFineDarkLuma','RegionFineDarkColor',
                    'RegionReflectionLuma','RegionReflectionColor','RegionPureColor']
            elif kind=='uint2' and name=='DualLowSize':
                result += ['dual_low_width','dual_low_height']
            else:
                result.append({'dual_residual':'DualResidualEnabled'}.get(name,name))
    return result
hlsl_fields = fields(cbuffer.group("body"), "HLSL")
cpp_fields = fields(cpp.group("body"), "C++")
if len(hlsl_fields) != 60 or len(cpp_fields) != 60:
    raise SystemExit(f"constant layout count mismatch: HLSL={len(hlsl_fields)} C++={len(cpp_fields)}")
normalize = lambda value: re.sub(r"[^a-z0-9]", "", value.lower())
if [normalize(x) for x in hlsl_fields] != [normalize(x) for x in cpp_fields]:
    raise SystemExit(f"constant layout order mismatch:\nHLSL={hlsl_fields}\nC++={cpp_fields}")

if "ConfigVersion=4" not in config:
    raise SystemExit("shipped config is not version 4")
if "WorkMode=0" not in config or "ProxyCurve=1" not in config:
    raise SystemExit("recommended perfect-chain defaults are missing")

# DRS active extents must reset mapping but not allocate a new temporal context.
storage = re.search(r"SameNrGuideStorageContract[\s\S]*?\n}\n", guides)
mapping = re.search(r"SameNrGuideMappingContract[\s\S]*?\n}\n", guides)
if not storage or not mapping:
    raise SystemExit("guide contract comparators not found")
for token in ("depth_width", "motion_source_width",
              "depth_physical_width", "motion_physical_width",
              "color_physical_width"):
    if token in storage.group(0):
        raise SystemExit(f"{token} must not be part of storage identity")
    if token not in mapping.group(0):
        raise SystemExit(f"{token} must remain part of history mapping")

if "sourceLuma > fullProxyLuma" not in body:
    raise SystemExit("HDR headroom branch missing")
if "logRatio * TransferStrength" not in body:
    raise SystemExit("multiplicative log-ratio strength missing")
if ("float3 storedProxy = modelValueDomain" not in body or
        "? saturate(proxyValue)" not in body):
    raise SystemExit("bounded R8 value-domain preservation missing")
if not re.search(
    r"config\.transfer_strength\s*=\s*std::clamp\(\s*"
    r"FiniteOr\(config\.transfer_strength,\s*0\.0f\),\s*0\.0f,\s*4\.0f\s*\)",
    source,
):
    raise SystemExit("4x public strength ceiling missing")
if "using the full render raster" not in source:
    raise SystemExit("unresolved output extent must fall back to reference")
if "capture_model_width" not in source:
    raise SystemExit("stage-capture model extent resolution missing")
if ("CompressNegativeTowardNeutral" not in body or
        "CompressBoundedTowardNeutral" not in body):
    raise SystemExit("neutral-axis HDR/SDR gamut protection missing")
if "source_to_work_x" not in source or "Jitter.Offset.X" not in source:
    raise SystemExit("motion/jitter source-to-work conversion missing")
if "FrameConfidence <= 0.000001" not in body:
    raise SystemExit("reset-frame confidence bypass missing")

print("unified raw/final identities and all 60 constant DWORDs validated; generated GPU checks remain separate")
