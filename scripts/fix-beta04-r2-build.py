#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

root = Path(__file__).resolve().parents[1]
source_path = root / "src" / "nr_before_sr.cpp"
source = source_path.read_text(encoding="utf-8")
old = """        info.work_linear_scale = std::min(
            static_cast<float>(resolved_model_width) / std::max(width, 1u),
            static_cast<float>(resolved_model_height) / std::max(height, 1u));
"""
new = """        const unsigned int capture_model_width =
            model_width != 0 ? model_width : width;
        const unsigned int capture_model_height =
            model_height != 0 ? model_height : height;
        info.work_linear_scale = std::min(
            static_cast<float>(capture_model_width) / std::max(width, 1u),
            static_cast<float>(capture_model_height) / std::max(height, 1u));
"""
if source.count(old) != 1:
    raise SystemExit(f"expected one capture-scale block, found {source.count(old)}")
source = source.replace(old, new, 1)
source_path.write_text(source, encoding="utf-8", newline="\n")

validator_path = root / "scripts" / "validate-beta04-structure.py"
validator = validator_path.read_text(encoding="utf-8")
needle = "if \"using the full render raster\" not in source:\n    raise SystemExit(\"unresolved output extent must fall back to reference\")\n"
addition = needle + "if \"capture_model_width\" not in source:\n    raise SystemExit(\"stage-capture model extent resolution missing\")\n"
if needle not in validator:
    raise SystemExit("validator insertion anchor missing")
validator = validator.replace(needle, addition, 1)
validator_path.write_text(validator, encoding="utf-8", newline="\n")

Path(__file__).unlink()
print("beta0.4-r2 compile hotfix applied")
