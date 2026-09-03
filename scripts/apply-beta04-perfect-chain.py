#!/usr/bin/env python3
"""Temporary beta0.4 source patch bootstrap; removed after generation."""
from __future__ import annotations
from pathlib import Path
import base64
import gzip

parts = sorted((Path(__file__).parent / "beta04-payload").glob("part*.txt"))
payload = "".join(path.read_text(encoding="ascii").strip() for path in parts)
code = gzip.decompress(base64.b64decode(payload)).decode("utf-8")
exec(compile(code, __file__ + "::<payload>", "exec"), {"__name__": "__main__", "__file__": __file__})
