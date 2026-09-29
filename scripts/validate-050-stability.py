#!/usr/bin/env python3
"""Execute production picture serializers and loader error semantics.
Host tests only; Windows core tests separately execute real TXT/ZIP publication.
"""
from pathlib import Path
import argparse
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(text, marker, semicolon=False):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end + int(semicolon)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--negative-control', action='store_true')
    args = parser.parse_args()
    cpp = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    core = (ROOT/'src/nr_feedback_core.inl').read_text(encoding='utf-8-sig')
    legacy = (ROOT/'src/nr_feedback_capture.inl').read_text(encoding='utf-8-sig')
    picture = (ROOT/'src/nr_feedback_picture.inl').read_text(encoding='utf-8-sig')
    stream = (ROOT/'src/nr_f8_stream.inl').read_text(encoding='utf-8-sig')
    loader = (ROOT/'src/nr_runtime_load_evidence.inl').read_text(encoding='utf-8-sig')
    present = definition(core, 'void Present(')
    assert 'AppendPictureConfig(s, config, session->snapshot_ok);' in present
    assert present.index('config = g_config;') < present.index('LeaveCriticalSection(') < present.index('AppendPictureConfig(')
    assert '#include "nr_feedback_picture.inl"' in core
    helper = definition(picture, 'void AppendPictureConfig(')
    assert 'g_config' not in helper and 'if (!valid) return;' in helper
    frame = definition(stream, 'JsonBuffer FrameJson(')
    assert '"highStrengthProtectionFlag\\":%d",(n.codec_flags & 1024u) != 0u' in frame
    assert 'g_config' not in frame
    if args.negative_control:
        # Deliberately serialize the wrong field. Compilation/device failures cannot pass.
        old = 'config.layer_bright)'
        assert helper.count(old) == 1
        helper = helper.replace(old, 'config.layer_shadow)')
    code = r'''
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>
using ULONGLONG = unsigned long long;
using DWORD = unsigned long;
constexpr DWORD ERROR_SUCCESS=0, ERROR_GEN_FAILURE=31;
DWORD last_error=0;
DWORD GetLastError(){return last_error;}
namespace reshade::api {struct effect_runtime;}
enum class NrJitterMode {Legacy,GameInputPixel};
'''
    code += '#include "' + (ROOT/'src/nr_feedback_limits.h').as_posix() + '"\n'
    code += '#include "' + (ROOT/'src/nr_processing_route.h').as_posix() + '"\n'
    code += '#include "' + (ROOT/'src/nr_layer_config.h').as_posix() + '"\n'
    code += definition(cpp, 'struct ChainConfig {', True) + '\n'
    code += definition(legacy, 'struct FeedbackSession', True) + '\n'
    code += definition(legacy, 'void FeedbackAppend(') + '\n' + helper + '\n'
    code += definition(stream, 'struct JsonBuffer', True) + '\n'
    code += definition(stream, 'void ConfigJson(') + '\n'
    code += definition(loader, 'DWORD RuntimeLoadError(') + '\n'
    code += r'''
int main() {
    unsigned checks=0;
    for (DWORD err : {0ul,5ul,126ul,193ul,1114ul}) {
        last_error=err;
        if (RuntimeLoadError(true)!=0 || RuntimeLoadError(false)!=(err?err:31ul)) return 2;
        ++checks;
    }
    for (int guard=0;guard<2;++guard) for (int layered=0;layered<2;++layered) {
        ChainConfig c;
        c.high_strength_protection=guard!=0;c.layered_enhancement=layered!=0;
        c.layer_tone=.5f;c.layer_structure=1.5f;c.layer_bright=.75f;c.layer_shadow=1.25f;
        c.allow_unverified_hdr_color=true;
        FeedbackSession s;
        AppendPictureConfig(s,c,true);
        if (s.report_truncated || !std::strstr(s.report,"requested.layerBright=0.75\n")) {
            std::fputs("PICTURE_SNAPSHOT_ORACLE\n",stderr);return 1;
        }
        if (!std::strstr(s.report,"requested.layerTone=0.5\n") ||
            !std::strstr(s.report,"requested.layerStructure=1.5\n") ||
            !std::strstr(s.report,"requested.layerShadow=1.25\n") ||
            !std::strstr(s.report,"requested.unverifiedHdrColor=1\n")) return 3;
        std::string expected="requested.highStrengthProtection="+std::to_string(guard)+"\n";
        if (!std::strstr(s.report,expected.c_str())) return 4;
        s={};AppendPictureConfig(s,c,false);
        if (std::strcmp(s.report,"requested.pictureSnapshotValid=0\n")!=0) return 5;
        s={};s.report_used=sizeof(s.report)-256;
        AppendPictureConfig(s,c,true);
        if (!s.report_truncated || s.report_used!=sizeof(s.report)-256) return 6;
        JsonBuffer b;b.Append("{\"test\":1");ConfigJson(b,"requested",c);b.Append("}");
        if (!b.ok) return 7;
        std::puts(b.data);checks+=7;
    }
    std::fprintf(stderr,"production picture/loader: %u checks passed\n",checks);
}
'''
    with tempfile.TemporaryDirectory(prefix='nr-050-stability-') as td:
        src = Path(td)/'test.cpp'
        exe = Path(td)/('test.exe' if os.name == 'nt' else 'test')
        src.write_text(code, encoding='utf-8')
        flags = ['-std=c++20','-O0','-Wall','-Wextra','-Werror']
        if os.name == 'nt':
            flags.append('-static')
        subprocess.run([args.compiler,*flags,str(src),'-o',str(exe)],check=True)
        result = subprocess.run([str(exe)],capture_output=True,text=True)
        if args.negative_control:
            assert result.returncode == 1 and result.stderr.strip() == 'PICTURE_SNAPSHOT_ORACLE', result
            print('PASS wrong-layer negative control rejected by exact production serializer oracle')
        else:
            assert result.returncode == 0, result.stderr
            rows = [json.loads(line)['requested'] for line in result.stdout.splitlines()]
            assert len(rows)==4
            for i,c in enumerate(rows):
                assert c['highStrengthProtection']==i//2 and c['layeredEnhancement']==i%2
                assert (c['layerTone'],c['layerStructure'],c['layerBright'],c['layerShadow'])==(.5,1.5,.75,1.25)
                assert c['unverifiedHdrColourRequested']==1
            print(result.stderr.strip())
            print('PASS real JSON round-trip, bounded TXT snapshot, missing-snapshot and unknown-loader-error semantics')

if __name__ == '__main__':
    main()
