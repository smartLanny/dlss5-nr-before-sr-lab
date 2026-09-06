#!/usr/bin/env python3
"""Host numeric tests compile the scalar helper extracted from production HLSL.
Full colour/subrect/alpha/identity validation is in high_strength_gpu.inl (WARP).
No game, temporal-image-quality or performance claim follows from these tests.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    source = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8')
    ui = (ROOT / 'src/nr_settings_ui.inl').read_text(encoding='utf-8')
    ini = (ROOT / 'config/nr_before_sr.ini').read_text(encoding='utf-8')
    match = re.search(r'// V3_BOOST_SCALAR_BEGIN\n(.*?)// V3_BOOST_SCALAR_END', source, re.S)
    require(match is not None, 'Production boost scalar helper missing')
    helper = match.group(1)
    for token in [
        'bool high_strength_protection = true;',
        'a.high_strength_protection == b.high_strength_protection',
        'L"NRBeforeSR", L"HighStrengthProtection", 1, g_ini_path',
        'WriteIniInt(L"HighStrengthProtection", config.high_strength_protection ? 1 : 0)',
        '((v2_combine && g_config.high_strength_protection) ? 1024u : 0u)',
        'const bool protectBoost = boostProtectionEnabled &&\n        TransferStrength > 1.0;',
        'budget = max(baseLog, min(logLimit, sdrHeadroom));',
        'poweredChromaLog = baseChroma * scale;',
        'sourceLuma > fullProxyLuma',
        'FrameConfidence <= 0.000001',
        'kCodecRootConstantCount = 35;',
    ]:
        require(token in source, 'Missing production integration: ' + token)
    history = re.search(r'bool SameHistoryConfig\([^;]*?\)\s*\{(.*?)\n\}', source, re.S)
    require(history is not None, 'History comparator missing')
    require('high_strength_protection' not in history.group(1), 'Composition knob must not reset NR history')
    require('&next.high_strength_protection' in ui and 'ImGui::BeginDisabled(next.mode != 2);' in ui,
            'Mode2 UI A/B switch missing')
    require('HighStrengthProtection=1' in ini, 'Beta default and persistent key missing')
    shader = source[source.index('const char kCodecShader'):source.index('ID3DBlob *CompileCodecShader')]
    require(shader.count('Passthrough & 1024u') == 1, 'Guard flag must have one runtime consumer')
    require('float scale = peakLog > 0.0 ? SoftExtraMagnitudeV3(' in shader,
            'Chroma must use a common log-vector scale, not extra per-axis clipping')
    prefix = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static float min(float a, float b) { return std::min(a,b); }
static float max(float a, float b) { return std::max(a,b); }
static unsigned checks = 0;
static void check(bool ok, const char *name) {
    ++checks;
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",name); std::exit(1); }
}
'''
    tests = r'''
int main() {
    for (float limit : {0.f, .1f, 1.f, 2.f, 4.f}) {
        for (float input : {0.f, 1e-8f, .1f*limit, .5f*limit, .99f*limit, limit, 2.f*limit}) {
            const float base = std::clamp(input, 0.f, limit);
            float previous = base;
            for (float extra : {0.f,.2f,1.f,3.f,7.f,10000.f}) {
                const float value = SoftExtraMagnitudeV3(input,extra,limit);
                check(std::isfinite(value), "finite");
                check(value+1e-6f >= base && value <= limit+1e-6f, "bounded extra edit");
                check(value+1e-6f >= previous, "monotonic strength");
                check(value <= base*(1.f+extra)+1e-6f, "cannot amplify beyond requested edit");
                if (extra == 0.f) check(value == base, "1x exact anchor");
                if (base == 0.f) check(value == 0.f, "zero edit identity");
                if (base == limit) check(value == base, "zero remaining headroom");
                previous = value;
            }
        }
    }
    check(SoftExtraMagnitudeV3(.5f,1.f,1.f) == .75f, "independent fixed 2x expectation");
    check(SoftExtraMagnitudeV3(.5f,3.f,1.f) == .875f, "independent fixed 4x expectation");
    check(SoftExtraMagnitudeV3(0.f,7.f,4.f) == 0.f, "8x zero stays zero");
    // Scaling the entire vector preserves its direction; no channel can
    // reach the cap early due to an independently clipped extra component.
    for (float strength : {1.f,1.2f,2.f,4.f,8.f}) {
        const float r=.4f, g=-.2f, b=.1f;
        const float scale=SoftExtraMagnitudeV3(.4f,strength-1.f,2.f)/.4f;
        check(std::abs((r*scale)/(g*scale)+2.f)<1e-6f,"log chroma direction rg");
        check(std::abs((b*scale)/(g*scale)+.5f)<1e-6f,"log chroma direction bg");
    }
    std::printf("beta0.5 extracted production boost math: %u checks passed.\n",checks);
}
'''
    with tempfile.TemporaryDirectory(prefix='nr-beta05-') as tmp:
        cpp = Path(tmp) / 'production_boost.cpp'
        exe = Path(tmp) / ('production_boost.exe' if __import__('os').name == 'nt' else 'production_boost')
        cpp.write_text(prefix + helper + tests, encoding='utf-8')
        subprocess.run([args.compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print('beta0.5 config / Mode2-only / ABI / history integration checks passed.')

if __name__ == '__main__':
    main()
