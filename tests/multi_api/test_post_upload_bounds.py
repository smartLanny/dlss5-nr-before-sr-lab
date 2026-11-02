"""Run the actual WARP fixture upload function against guarded CPU storage.

Map/GetDesc are controlled buffer seams, not D3D12 execution. Storage includes
the complete tail guard so even the historical overwrite stays within malloc.
The declared widths use Microsoft's documented unpadded final-row footprint.
This regression does not identify the cause of any Windows process exit code.
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('upload_extract', ROOT / 'scripts/extract-post-gpu.py')
EXTRACT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACT)

HARNESS = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using UINT64 = std::uint64_t;
constexpr int DXGI_FORMAT_R16G16B16A16_FLOAT = 10;
constexpr int DXGI_FORMAT_R32G32B32A32_FLOAT = 2;
constexpr std::size_t guard_bytes = 512;
constexpr std::uint8_t sentinel = 0xA5;
struct Float4 { float x, y, z, w; };
struct Description { UINT64 Width; };
struct MappedUpload {
    UINT64 width;
    std::vector<std::uint8_t> storage;
    unsigned maps = 0, unmaps = 0;
    explicit MappedUpload(UINT64 bytes) : width(bytes), storage(bytes + 2 * guard_bytes, sentinel) {}
    Description GetDesc() const { return {width}; }
    int Map(unsigned, const void*, void **mapped) {
        ++maps; *mapped = storage.data() + guard_bytes; return 0;
    }
    void Unmap(unsigned, const void*) { ++unmaps; }
    bool guards_intact() const {
        const auto same = [](std::uint8_t byte) { return byte == sentinel; };
        return std::all_of(storage.begin(), storage.begin() + guard_bytes, same) &&
            std::all_of(storage.begin() + guard_bytes + width, storage.end(), same);
    }
};
struct Texture {
    MappedUpload *upload;
    struct { UINT64 Offset; struct { unsigned RowPitch; } Footprint; } footprint;
    unsigned width, height;
    int format;
};
void Check(int result, const char *message) {
    if (result) throw std::runtime_error(message);
}
// @ACTUAL_FLOAT_TO_HALF@
// @ACTUAL_FILL_UPLOAD@
int main() {
    struct Case { const char *name; unsigned width, height; int format; UINT64 offset; };
    const Case cases[] = {
        {"one-pixel-fp32", 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0},
        {"result-fp16", 69, 52, DXGI_FORMAT_R16G16B16A16_FLOAT, 0},
        {"result-fp32", 69, 52, DXGI_FORMAT_R32G32B32A32_FLOAT, 0},
        {"offset-fp16", 13, 3, DXGI_FORMAT_R16G16B16A16_FLOAT, 512},
        {"aligned-fp16", 32, 24, DXGI_FORMAT_R16G16B16A16_FLOAT, 0},
        {"aligned-fp32", 16, 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0},
    };
    unsigned failures = 0;
    for (const auto &test : cases) {
        const unsigned pixel_bytes = test.format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 16;
        const unsigned row_bytes = test.width * pixel_bytes;
        const unsigned pitch = (row_bytes + 255u) & ~255u;
        // Same final-row extent used by D3DX12GetCopyableFootprints. No GPU
        // allocation is inferred from this guarded host buffer.
        const UINT64 declared = test.offset + UINT64(pitch) * (test.height - 1) + row_bytes;
        MappedUpload resource(declared);
        Texture texture{&resource, {test.offset, {pitch}}, test.width, test.height, test.format};
        std::vector<Float4> pixels(test.width * test.height);
        for (unsigned y = 0; y < test.height; ++y)
            for (unsigned x = 0; x < test.width; ++x)
                pixels[y * test.width + x] = (x + y) % 2 ? Float4{.5f, 1.f, 0.f, -2.f} : Float4{1.f, .5f, -2.f, 0.f};
        FillUpload(texture, pixels);
        bool payload = resource.maps == 1 && resource.unmaps == 1;
        for (unsigned y = 0; y < test.height; ++y)
            for (unsigned x = 0; x < test.width; ++x) {
                const auto *actual = resource.storage.data() + guard_bytes + test.offset + UINT64(y) * pitch + x * pixel_bytes;
                const std::uint16_t even[] = {0x3C00, 0x3800, 0xC000, 0};
                const std::uint16_t odd[] = {0x3800, 0x3C00, 0, 0xC000};
                const void *expected = pixel_bytes == 8 ? static_cast<const void *>((x + y) % 2 ? odd : even) :
                    static_cast<const void *>(&pixels[y * test.width + x]);
                payload = payload && std::memcmp(actual, expected, pixel_bytes) == 0;
            }
        const bool bounded = resource.guards_intact();
        std::printf("%s upload guard case=%s declared=%llu padded=%llu payload=%u\n",
            bounded && payload ? "PASS" : "FAIL", test.name,
            static_cast<unsigned long long>(declared),
            static_cast<unsigned long long>(test.offset + UINT64(pitch) * test.height), unsigned(payload));
        if (!bounded || !payload) ++failures;
    }
    return failures ? 7 : 0;
}
'''


class UploadBounds(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which(os.environ.get('CXX', 'g++'))
        if not cls.compiler:
            raise unittest.SkipTest('C++ compiler unavailable')
        cls.temporary = tempfile.TemporaryDirectory(prefix='nr-post-upload-bounds-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder = Path(cls.temporary.name)
        template = (ROOT / 'tests/post_scaled_work_gpu.cpp.in').read_text(encoding='utf-8')
        cls.upload = EXTRACT.function(template, 'void FillUpload(')
        cls.half = EXTRACT.function(template, 'std::uint16_t FloatToHalf(')

    def execute(self, name, upload):
        source = self.folder / (name + '.cpp')
        binary = self.folder / (name + ('.exe' if os.name == 'nt' else ''))
        source.write_text(HARNESS.replace('// @ACTUAL_FLOAT_TO_HALF@', self.half)
                          .replace('// @ACTUAL_FILL_UPLOAD@', upload), encoding='utf-8')
        flags = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror']
        if os.name == 'nt':
            flags += ['-static']
        compile_result = subprocess.run([self.compiler, *flags, str(source), '-o', str(binary)],
                                        capture_output=True, text=True, timeout=60)
        self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        print(result.stdout, end='')
        return result

    def test_actual_upload_preserves_tail_guards_and_pixel_rows(self):
        result = self.execute('bounded', self.upload)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count('PASS upload guard'), 6)

    def test_previous_full_pitch_clear_overwrites_declared_tail(self):
        anchor = 'texture.upload->GetDesc().Width'
        self.assertEqual(self.upload.count(anchor), 1)
        previous = self.upload.replace(anchor,
            'texture.footprint.Offset + texture.footprint.Footprint.RowPitch * static_cast<UINT64>(texture.height)')
        result = self.execute('old-pitch-clear', previous)
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        for case in ('one-pixel-fp32', 'result-fp16', 'result-fp32', 'offset-fp16'):
            self.assertIn('FAIL upload guard case=' + case, result.stdout)
        self.assertEqual(result.stdout.count('FAIL upload guard'), 4)
        self.assertEqual(result.stdout.count('PASS upload guard'), 2)
        self.assertNotIn('payload=0', result.stdout)


if __name__ == '__main__':
    unittest.main()
