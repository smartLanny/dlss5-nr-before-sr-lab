#!/usr/bin/env python3
"""Idempotent compiler hotfixes for the generated beta0.4 HLSL."""
from __future__ import annotations

from pathlib import Path
import re

source_path = Path(__file__).resolve().parents[1] / "src" / "nr_before_sr.cpp"
text = source_path.read_text(encoding="utf-8")

signature = "float3 ApplySelectedProxyCurve(float3 linear) {"
fixed_signature = "float3 ApplySelectedProxyCurve(float3 sceneLinear) {"
if signature in text:
    start = text.index(signature)
    end_marker = "\n}\n\nfloat3 EncodeProxyLinearV2"
    end = text.index(end_marker, start) + 3
    block = text[start:end]
    block = re.sub(r"\blinear\b", "sceneLinear", block)
    text = text[:start] + block + text[end:]
elif fixed_signature not in text:
    raise RuntimeError("ApplySelectedProxyCurve block was not found")

source_path.write_text(text, encoding="utf-8", newline="\n")
print("beta0.4 HLSL compiler hotfixes applied")
