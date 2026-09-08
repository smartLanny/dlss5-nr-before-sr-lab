"""Compare actual external jitter binding with the existing native contract."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def definition(text, marker):
    start = text.index(marker)
    pos = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos]


class ExternalJitter(unittest.TestCase):
    def test_actual_binding(self):
        cxx = os.environ.get('CXX')
        if not cxx:
            self.skipTest('CXX required')
        source = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        code = r'''
#include <algorithm>
#include <cmath>
#include <cassert>
#include "src/interop/nr_external_provider_abi.h"
struct NgxParams {float x,y;};
bool GetFloat(const NgxParams *p,const char *key,float *v) { if(!p)return false; *v=key[14]=='X'?p->x:p->y;return true; }
enum class NrJitterMode {Legacy,GameInputPixel};
'''
        code += definition(source, 'struct NrJitterBinding {') + ';\n'
        code += definition(source, 'NrJitterBinding ResolveNrJitter(') + '\n'
        code += '#include "src/interop/nr_external_jitter.inl"\n'
        code += r'''
int main() {
    for(auto provenance:{NrGuideProvenance::Native,NrGuideProvenance::Extracted,NrGuideProvenance::Synthetic})
    for(unsigned width:{500u,750u,1000u}) for(unsigned height:{250u,375u,500u}) {
        NrExternalResourceV1 color{}; color.provenance=provenance; color.jitter_x=.5f;color.jitter_y=-.25f;
        NgxParams params{color.jitter_x,color.jitter_y};
        auto native=ResolveNrJitter(&params,NrJitterMode::GameInputPixel,1000,500,width,height);
        auto ext=ResolveExternalJitter(color,NrJitterMode::GameInputPixel,1000,500,width,height);
        assert(ext.known && ext.x==native.x && ext.y==native.y);
        assert(ext.source_x==native.source_x && ext.source_y==native.source_y);
        assert(!ResolveExternalJitter(color,NrJitterMode::Legacy,1000,500,width,height).known);
    }
    NrExternalResourceV1 color{}; color.provenance=NrGuideProvenance::Native;
    assert(ResolveExternalJitter(color,NrJitterMode::GameInputPixel,1000,500,500,250).known);
    color.provenance=NrGuideProvenance::Synthetic;
    assert(!ResolveExternalJitter(color,NrJitterMode::GameInputPixel,1000,500,500,250).known);
    for(float bad:{NAN,INFINITY,2000.f}) {
        color.jitter_x=bad;
        assert(!ResolveExternalJitter(color,NrJitterMode::GameInputPixel,1000,500,500,250).known);
    }
    color.provenance=NrGuideProvenance::Native;color.jitter_x=.5f;
    assert(!ResolveExternalJitter(color,NrJitterMode::GameInputPixel,0,500,500,250).known);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp) / 'jitter.cpp', Path(tmp) / 'jitter.exe'
            cpp.write_text(code, encoding='utf-8')
            subprocess.run([cxx, '-std=c++20', '-static', '-I' + str(ROOT), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
