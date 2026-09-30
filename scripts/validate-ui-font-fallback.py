"""Verify the Chinese UI's deterministic Latin-font fallback contract."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def definition(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
settings = (root / "src/nr_unified_settings.inl").read_text(encoding="utf-8-sig")
pages = (root / "src/nr_unified_pages.inl").read_text(encoding="utf-8-sig")

decision = definition(source, "constexpr bool ShouldUseChineseUi(")
# ReShade draws these exported fields before the add-on's own panel can choose
# a fallback language. Keep both language builds' public metadata ASCII-safe.
metadata = re.findall(
    r'(?:NAME|DESCRIPTION|AUTHOR)\s*=\s*((?:"(?:\\.|[^"])*"\s*)+);',
    source,
    re.DOTALL,
)
if len(metadata) != 5:
    raise SystemExit(f"expected five exported NAME/DESCRIPTION/AUTHOR values, got {len(metadata)}")
if any(ord(character) > 127 for payload in metadata for character in payload):
    raise SystemExit("exported NAME/DESCRIPTION/AUTHOR metadata is not ASCII-safe")
for glyph in ("0x4E2D", "0x6587", "0x94FE", "0x8F7D"):
    if f"font->IsGlyphInFont(static_cast<ImWchar>({glyph}))" not in source:
        raise SystemExit(f"missing Chinese UI glyph check: {glyph}")
if "const bool use_chinese_ui = UseChineseUi();" not in settings or \
   "return use_chinese_ui ? zh : en;" not in settings:
    raise SystemExit("unified settings must select English when the active font lacks Chinese glyphs")
if "UseChineseUi()?kAddonAuthor:kAddonAuthorAscii" not in pages:
    raise SystemExit("public author label must fall back to ASCII")
if "if(kChineseUi&&!use_chinese_ui)SettingsNote(\"Chinese glyphs are unavailable." not in settings:
    raise SystemExit("Chinese UI fallback must remain visible to users")

harness = f"""
#include <cstdio>
{decision}
int main() {{
    unsigned checks = 0;
    for (unsigned mask = 0; mask != 64; ++mask) {{
        const bool actual = ShouldUseChineseUi(
            mask & 1, mask & 2, mask & 4, mask & 8, mask & 16, mask & 32);
        const bool expected = mask == 63;
        ++checks;
        if (actual != expected) return 1;
    }}
    std::printf("UI font fallback: %u decisions PASS\\n", checks);
    return 0;
}}
"""
with tempfile.TemporaryDirectory(prefix="nr-ui-font-") as directory:
    cpp = Path(directory) / "font.cpp"
    exe = Path(directory) / "font.exe"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run(
        [args.compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
         "-static", str(cpp), "-o", str(exe)], check=True
    )
    subprocess.run([str(exe)], check=True)
