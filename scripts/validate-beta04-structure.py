#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / "src" / "nr_before_sr.cpp").read_text(encoding="utf-8")
guides = (root / "src" / "nr_guide_contract.h").read_text(encoding="utf-8")
config = (root / "config" / "nr_before_sr.ini").read_text(encoding="utf-8")

required = [
    'kBuildIdentity = "beta0.4.1"',
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
if not match or int(match.group(1)) != 31:
    raise SystemExit("kCodecRootConstantCount must be 31")

hlsl = re.search(
    r'const char kCodecShader\[\] = R"HLSL\((?P<body>[\s\S]*?)\)HLSL";',
    source,
)
if not hlsl:
    raise SystemExit("embedded HLSL not found")
body = hlsl.group("body")
cbuffer = re.search(r"cbuffer Constants : register\(b0\) \{(?P<body>[\s\S]*?)\};", body)
cpp = re.search(r"struct CodecConstants \{(?P<body>[\s\S]*?)\};", source)
if not cbuffer or not cpp:
    raise SystemExit("constant layouts not found")
hlsl_fields = re.findall(r"^\s*(?:uint|float)\s+([A-Za-z0-9_]+)\s*;", cbuffer.group("body"), re.M)
cpp_fields = re.findall(r"^\s*(?:unsigned int|float)\s+([A-Za-z0-9_]+)\s*;", cpp.group("body"), re.M)
if len(hlsl_fields) != 31 or len(cpp_fields) != 31:
    raise SystemExit(f"constant layout count mismatch: HLSL={len(hlsl_fields)} C++={len(cpp_fields)}")
normalize = lambda value: re.sub(r"[^a-z0-9]", "", value.lower())
if [normalize(x) for x in hlsl_fields] != [normalize(x) for x in cpp_fields]:
    raise SystemExit(f"constant layout order mismatch:\nHLSL={hlsl_fields}\nC++={cpp_fields}")

if "ConfigVersion=4" not in config:
    raise SystemExit("shipped config is not version 4")
if "WorkMode=1" not in config or "ProxyCurve=1" not in config:
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
    r"FiniteOr\(config\.transfer_strength,\s*0\.0f\),\s*0\.0f,\s*8\.0f\s*\)",
    source,
):
    raise SystemExit("8x strength ceiling missing")
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

print("beta0.4 perfect-chain structural validation passed")
