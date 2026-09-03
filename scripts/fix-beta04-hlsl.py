#!/usr/bin/env python3
"""Idempotent compiler hotfixes for the generated beta0.4 HLSL."""
from __future__ import annotations

from pathlib import Path
import re

source_path = Path(__file__).resolve().parents[1] / "src" / "nr_before_sr.cpp"
text = source_path.read_text(encoding="utf-8")

# `linear` is an HLSL interpolation modifier and therefore cannot be used as a
# local identifier. Rename standalone occurrences only inside the two V2
# regions; legacy HLSL stays byte-identical.
def rename_v2_span(source: str, start_marker: str, end_marker: str) -> str:
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    block = source[start:end]
    block = re.sub(r"\blinear\b", "linearValue", block)
    return source[:start] + block + source[end:]

text = rename_v2_span(
    text,
    "// beta0.4 V2: independent NR working raster",
    "[numthreads(8, 8, 1)]\nvoid EncodeCS",
)
text = rename_v2_span(
    text,
    "void WriteCombinedV2",
    "[numthreads(8, 8, 1)]\nvoid CopyCS",
)

source_path.write_text(text, encoding="utf-8", newline="\n")
print("beta0.4 HLSL compiler hotfixes applied")
