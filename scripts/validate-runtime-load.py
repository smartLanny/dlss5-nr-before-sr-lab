"""Compile the actual loader error helper; no NVIDIA runtime is simulated."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--negative-control', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'src/nr_runtime_load_evidence.inl').read_text(encoding='utf-8')
    start = source.index('DWORD RuntimeLoadError(bool succeeded) {')
    end = source.index('\n}', start) + 2
    helper = source[start:end]
    if args.negative_control:
        if helper.count('ERROR_GEN_FAILURE') != 1:
            raise RuntimeError('Loader negative-control anchor changed')
        helper = helper.replace('ERROR_GEN_FAILURE', 'ERROR_MOD_NOT_FOUND')
    harness = r'''
#include <cstdio>
using DWORD = unsigned long;
constexpr DWORD ERROR_SUCCESS=0, ERROR_GEN_FAILURE=31, ERROR_MOD_NOT_FOUND=126;
DWORD last_error=0;
unsigned reads=0;
DWORD GetLastError() { ++reads; return last_error; }
// @HELPER@
int main() {
    const DWORD errors[] = {0,5,126,193,1114};
    unsigned checks=0;
    for (DWORD value:errors) {
        last_error=value; reads=0;
        if (RuntimeLoadError(true)!=0 || reads!=0) {
            std::fputs("LOADER_SUCCESS_ORACLE\n",stderr); return 1;
        }
        ++checks;
        last_error=value; reads=0;
        const DWORD actual=RuntimeLoadError(false);
        if (actual!=(value?value:31)) {
            std::fputs("LOADER_FALLBACK_ORACLE\n",stderr); return 1;
        }
        ++checks;
        if (reads!=1 || last_error!=value) {
            std::fputs("LOADER_IMMEDIATE_ERROR_ORACLE\n",stderr); return 1;
        }
        ++checks;
    }
    std::printf("production loader error helper: %u checks passed\n",checks);
}
'''.replace('// @HELPER@', helper)
    with tempfile.TemporaryDirectory(prefix='nr-loader-') as temp:
        cpp = Path(temp) / 'loader.cpp'
        exe = Path(temp) / ('loader.exe' if os.name == 'nt' else 'loader')
        cpp.write_text(harness, encoding='utf-8')
        flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror']
        if os.name == 'nt': flags.append('-static')
        subprocess.run([args.compiler, *flags, str(cpp), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        if args.negative_control:
            if result.returncode != 1 or result.stderr.strip() != 'LOADER_FALLBACK_ORACLE':
                raise RuntimeError('Negative control did not detect the exact invented missing-DLL error')
            print('PASS old missing-DLL fallback rejected by LOADER_FALLBACK_ORACLE')
        else:
            print(result.stdout, end='')
            if result.returncode:
                raise RuntimeError('Production loader regression: ' + result.stderr)

if __name__ == '__main__': main()
