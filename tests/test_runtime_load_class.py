"""Host extraction of RuntimeClassifyLoad. No Windows LoadLibrary or game claim."""
from __future__ import annotations
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def definition(text: str, marker: str) -> str:
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class RuntimeLoadClass(unittest.TestCase):
    def test_classify_and_names(self) -> None:
        source = (ROOT / 'src/nr_runtime_load_evidence.inl').read_text(encoding='utf-8-sig')
        classify = definition(source, 'unsigned RuntimeClassifyLoad(')
        name = definition(source, 'const char *RuntimeLoadClassName(')
        code = f'''
#include <cstdio>
#include <cstring>
int checks=0, failures=0;
void check(bool ok, const char *why) {{ ++checks; if (!ok) {{ ++failures; std::fprintf(stderr, "FAIL %s\\n", why); }} }}
{classify}
{name}
int main() {{
    check(RuntimeClassifyLoad(true, true, 0) == 1, "loaded");
    check(RuntimeClassifyLoad(false, false, 2) == 2, "file-absent even if Win32 2");
    check(RuntimeClassifyLoad(false, false, 126) == 2, "absent file is not dependency-missing");
    check(RuntimeClassifyLoad(true, false, 126) == 3, "present + 126 is dependency-missing");
    check(RuntimeClassifyLoad(true, false, 193) == 4, "present + other Win32");
    check(RuntimeClassifyLoad(true, false, 0) == 4, "present + unknown error");
    check(std::strcmp(RuntimeLoadClassName(1), "loaded") == 0, "name loaded");
    check(std::strcmp(RuntimeLoadClassName(2), "file-absent") == 0, "name absent");
    check(std::strcmp(RuntimeLoadClassName(3), "dependency-missing") == 0, "name dep");
    check(std::strcmp(RuntimeLoadClassName(4), "other-failure") == 0, "name other");
    check(std::strcmp(RuntimeLoadClassName(0), "not-attempted") == 0, "name idle");
    if (failures) return 1;
    std::printf("runtime-load-class: %d checks, 0 failures\\n", checks);
    return 0;
}}
'''
        with tempfile.TemporaryDirectory(prefix='nr152-') as td:
            src = Path(td) / 'class.cpp'
            exe = Path(td) / ('class.exe' if os.name == 'nt' else 'class')
            compiler = os.environ.get('CXX', 'g++')
            src.write_text(code, encoding='utf-8')
            subprocess.run([compiler, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror',
                            str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
