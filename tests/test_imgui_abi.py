"""CPU-only, real MSVC-host/MinGW-client vector-return ABI regression."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAINS = ROOT.parent / 'dlss5-lab' / 'toolchains'


def llvm_bin():
    configured = os.environ.get('NR_LLVM_BIN')
    if configured:
        return Path(configured)
    matches = sorted(TOOLCHAINS.glob('llvm-mingw-*/bin'))
    if matches:
        return matches[-1]
    found = shutil.which('clang++')
    return Path(found).parent if found else Path('missing-llvm')


class SourceGuard(unittest.TestCase):
    def test_no_direct_host_vec2_getters(self):
        sdk = Path(os.environ.get('NR_RESHADE_ROOT', TOOLCHAINS / 'reshade-source'))
        overlay = sdk / 'include' / 'reshade_overlay.hpp'
        if not overlay.is_file():
            self.skipTest('Set NR_RESHADE_ROOT to the pinned ReShade SDK')
        names = set(re.findall(r'ImVec2\s*\(\*(\w+)\)\s*\(', overlay.read_text(encoding='utf8')))
        self.assertIn('GetContentRegionAvail', names)
        violations = []
        for file in (ROOT / 'src').rglob('*'):
            if file.suffix not in ('.h', '.hpp', '.cpp', '.inl') or file.name == 'nr_imgui_abi.h':
                continue
            for line_no, line in enumerate(file.read_text(encoding='utf8').splitlines(), 1):
                if any(re.search(r'ImGui\s*::\s*' + re.escape(name) + r'\s*\(', line) for name in names):
                    violations.append(f'{file.relative_to(ROOT)}:{line_no}')
        self.assertEqual([], violations, 'Host ImVec2 returns must pass through the ABI adapter')


@unittest.skipUnless(os.name == 'nt', 'Cross-ABI executable test requires Windows x64')
class HostAbi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bin = llvm_bin()
        cls.clang = cls.bin / 'clang++.exe'
        cls.linker = cls.bin / 'ld.lld.exe'
        if not cls.clang.is_file() or not cls.linker.is_file():
            raise unittest.SkipTest('Set NR_LLVM_BIN to an LLVM-MinGW bin directory')
        cls.temp = tempfile.TemporaryDirectory(prefix='nr-imgui-abi-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.work = Path(cls.temp.name)
        cls.host = cls.work / 'host.dll'
        cls.run_checked([str(cls.clang), '--target=x86_64-pc-windows-msvc', '-std=c++17',
                         '-O1', '-c', str(ROOT / 'tests' / 'imgui_abi_host.cpp'),
                         '-o', str(cls.work / 'host.obj')])
        cls.run_checked([str(cls.linker), '-flavor', 'link', '/dll', '/noentry',
                         '/nodefaultlib', '/machine:x64', '/out:' + str(cls.host),
                         str(cls.work / 'host.obj')])
        client = cls.work / 'client.cpp'
        client.write_text(CLIENT, encoding='utf8')
        cls.client = cls.work / 'client.exe'
        cls.run_checked([str(cls.clang), '-std=c++17', '-O1', '-static', '-I', str(ROOT / 'src'),
                         str(client), '-o', str(cls.client)])

    @classmethod
    def run_checked(cls, args):
        result = subprocess.run(args, cwd=cls.work, capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise AssertionError(f'{args[0]} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}')
        return result

    def test_actual_adapter_zero_and_mixed_arguments(self):
        result = self.run_checked([str(self.client), str(self.host), 'adapted'])
        self.assertIn('adapted zero=13.25,-7.50 mixed=19.50,12.25', result.stdout)

    def test_unadapted_call_is_negative_control(self):
        # Fresh child only: RCX=17 is interpreted as an output pointer by the host.
        result = subprocess.run([str(self.client), str(self.host), 'unadapted'],
                                cwd=self.work, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(0, result.returncode, 'The actual incompatible DLL call unexpectedly succeeded')
        self.assertIn('calling unadapted', result.stdout, 'Failure must occur after loading the host export')
        self.assertIn(result.returncode & 0xffffffff, (4, 0xc0000005),
                      'Only a wrong vector result or access violation proves this ABI negative control')


CLIENT = r'''
#include <windows.h>
#include <cstdio>
#include <cstring>
struct ImVec2 {
    float x, y;
    ImVec2() : x(0), y(0) {}
    ImVec2(float a, float b) : x(a), y(b) {}
};
#include "nr_imgui_abi.h"
int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3) return 2;
    HMODULE host = LoadLibraryA(argv[1]);
    if (!host) return 2;
    auto zero = reinterpret_cast<ImVec2(*)()>(GetProcAddress(host, "HostVec2"));
    auto mixed = reinterpret_cast<ImVec2(*)(int,double,void*,float)>(GetProcAddress(host, "HostMixed"));
    if (!zero || !mixed) return 3;
    if (std::strcmp(argv[2], "unadapted") == 0) {
        std::puts("calling unadapted"); std::fflush(stdout);
        auto value = mixed(17, 2.5, reinterpret_cast<void*>(0x12340), 4.25f);
        return value.x == 19.5f && value.y == 12.25f ? 0 : 4;
    }
    const auto a = nr_imgui_abi::CallHostVec2(zero);
    const auto b = nr_imgui_abi::CallHostVec2(mixed, 17, 2.5, reinterpret_cast<void*>(0x12340), 4.25f);
    std::printf("adapted zero=%.2f,%.2f mixed=%.2f,%.2f\n", a.x, a.y, b.x, b.y);
    return a.x == 13.25f && a.y == -7.5f && b.x == 19.5f && b.y == 12.25f ? 0 : 4;
}
'''

if __name__ == '__main__':
    unittest.main()
