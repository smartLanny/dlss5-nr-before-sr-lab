"""Compile the actual F8 null-binding helper with a recording-only GPU seam.

This is CPU evidence for descriptor coverage, not execution of D3D12 or NGX.
The canonical --gpu controller still runs the original WARP/debug-layer oracle.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tests/f8_gpu_integration.inl'
PREFIX = r'''
#include <array>
#include <cstdlib>
constexpr unsigned kCodecSrvCount=13, kCodecUavCount=8;
constexpr unsigned kCodecLanczosUavIndex=kCodecSrvCount+7;
constexpr unsigned DXGI_FORMAT_R16G16B16A16_FLOAT=10;
struct Gpu {
    std::array<unsigned,21> writes{};
    void Srv(unsigned index, void *resource) {
        if(index>=13 || resource) std::exit(2);
        ++writes[index];
    }
    void UavTexture(unsigned index, void *resource, unsigned format) {
        if(index<13 || index>=21 || resource || format!=10) std::exit(2);
        ++writes[index];
    }
};
'''
SUFFIX = r'''
int main() {
    static_assert(kCodecLanczosUavIndex==20);
    static_assert(DXGI_FORMAT_R16G16B16A16_FLOAT==10);
    Gpu gpu;
    InitializeF8InactiveCodecDescriptors(gpu);
    for(unsigned i=0;i<21;++i) {
        const unsigned expected=(i==10 || i==11 || i==12 || i==20)?1:0;
        if(gpu.writes[i]!=expected) return 1;
    }
}
'''


class F8DescriptorCoverage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding='utf-8-sig')
        cls.helper = re.search(r'void InitializeF8InactiveCodecDescriptors\(Gpu &gpu\) \{.*?\n\}', cls.source, re.S).group()
        cls.compiler = shutil.which(os.environ.get('CXX', 'g++'))

    def compile_run(self, helper):
        if not self.compiler:
            self.skipTest('C++ compiler unavailable; not GPU validation')
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder)/'helper.cpp'
            exe = Path(folder)/'helper.exe'
            cpp.write_text(PREFIX+helper+SUFFIX, encoding='utf-8')
            built = subprocess.run([self.compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            return subprocess.run([str(exe)], capture_output=True).returncode

    def test_actual_helper_covers_only_missing_slots(self):
        self.assertEqual(self.compile_run(self.helper), 0)

    def test_each_missing_descriptor_is_independently_detected(self):
        for line in self.helper.splitlines():
            if 'gpu.Srv(' in line or 'gpu.UavTexture(' in line:
                with self.subTest(line=line):
                    self.assertEqual(self.compile_run(self.helper.replace(line, '')), 1)

    def test_wrong_valid_slot_is_not_accepted(self):
        self.assertEqual(self.compile_run(self.helper.replace('gpu.Srv(10,', 'gpu.Srv(9,')), 1)

    def test_both_new_cpu_mirrors_initialize_before_arm(self):
        self.assertEqual(self.source.count('InitializeF8InactiveCodecDescriptors(gpu);'), 2)
        for name in ('TestGpuGroupBoundary', 'TestGpuPipeline'):
            section = self.source.split('void '+name+'() {', 1)[1].split('\nvoid ', 1)[0]
            create = section.index('gpu.device->CreateDescriptorHeap(')
            initialize = section.index('InitializeF8InactiveCodecDescriptors(gpu);')
            self.assertLess(create, initialize)
            self.assertLess(initialize, section.index('ring.Arm('))
            self.assertIn('gpu.Execute();', section)


if __name__ == '__main__':
    unittest.main(verbosity=2)
