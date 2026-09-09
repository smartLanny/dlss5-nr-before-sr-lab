"""Verify the real geometry admission helper; no game or GPU is constructed."""
from pathlib import Path
import argparse
import json
import hashlib
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <cstdio>
#include <cstdlib>
#include "nr_face_capture_geometry.h"
using namespace nr_face_capture_geometry;
unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok){std::fprintf(stderr,"FACE_GEOMETRY_ORACLE: %s\n",why);std::exit(23);}}
Sample good(unsigned w=2228,unsigned h=1253){return {w,h,0,0,0,0,7,11,10000,{3840,2160,true}};}
View view(const Sample &s,unsigned w=3840,unsigned h=2160){return Resolve(s,7,11,10500,w,h);}
int main(){
    auto s=good();auto v=view(s);
    check(v.valid&&v.source_width==2228&&v.source_height==1253,"rounded render input maps to its exact full 4K output");
    check(v.viewport_x==0&&v.viewport_y==0&&v.viewport_width==3840&&v.viewport_height==2160,"display viewport is explicit and complete");
    check(v.display_width==3840&&v.display_height==2160&&v.sampled_at==10000,"display and sample metadata preserved");
    for(unsigned divisor:{1u,2u,3u,4u}){
        s=good(3840/divisor,2160/divisor);check(view(s).valid,"uniform work/render scaling retains mapping");
    }
    s=good(3840,2160);check(view(s).valid,"post-SR native grid maps directly");
    s=good();s.output.full_extent=false;check(!view(s).valid,"missing full-output proof is not inferred from aspect");
    s=good();s.output.width=1920;check(!view(s).valid,"other-size output cannot masquerade as display");
    s=good();s.output.height=1440;check(!view(s).valid,"output height must match display");
    s=good();check(!view(s,1920,1080).valid,"same aspect is insufficient when display size differs");
    s=good(2228,2000);check(!view(s).valid,"stretched or sub-view source stays unmapped");
    for(unsigned which=0;which<4;++which){
        s=good();unsigned *origins[]={&s.source_base_x,&s.source_base_y,&s.destination_base_x,&s.destination_base_y};
        *origins[which]=1;check(!view(s).valid,"unverified source or destination origin is rejected");
    }
    s=good();check(!Resolve(s,8,11,10500,3840,2160).valid,"old configuration geometry rejected");
    check(!Resolve(s,7,12,10500,3840,2160).valid,"old model instance geometry rejected");
    check(!Resolve(s,7,11,12001,3840,2160).valid,"stopped rendering cannot leave an indefinitely usable box");
    check(Resolve(s,7,11,12000,3840,2160).valid,"documented two-second boundary accepted");
    check(!Resolve(s,7,11,9999,3840,2160).valid,"future timestamp rejected");
    s=good();s.instance=0;check(!view(s).valid,"no successful NR instance cannot publish usable geometry");
    s=good();s.sampled_at=0;check(!view(s).valid,"unrecorded sample rejected");
    for(unsigned bad:{0u,16385u,0xffffffffu}){
        s=good();s.source_width=bad;check(!view(s).valid,"invalid source width rejected");
        s=good();s.source_height=bad;check(!view(s).valid,"invalid source height rejected");
    }
    s=good();check(!view(s,0,2160).valid&&!view(s,3840,0).valid,"empty display rejected");
    check(!view(s,32769,2160).valid&&!view(s,3840,32769).valid,"oversized display rejected");
    std::printf("PASS face capture geometry: %u assertions; live-output identity, stale/config/instance and full-view mapping. CPU only.\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--output-directory', type=Path)
    args = parser.parse_args()
    header = (ROOT/'src/nr_face_capture_geometry.h').read_text(encoding='utf-8')
    core = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
    assert 'GetTickCount64(), slot.face_output' in core
    assert core.count('slot->face_output = ResolveFaceCaptureOutput(') == 2
    snapshot = core[core.index('nr_face_capture_geometry::View SnapshotFaceDiagnosticGeometry('):]
    assert snapshot.index('EnterCriticalSection(&g_chain_cs)') < snapshot.index('const auto sample = g_face_capture_geometry') < snapshot.index('LeaveCriticalSection(&g_chain_cs)')
    variants = [('production', header)]
    if args.negative_control:
        guard = 'if (!s.output.full_extent || s.output.width != display_width ||\n        s.output.height != display_height)'
        assert header.count(guard) == 1
        variants.append(('unverified-output-mapping', header.replace(guard, 'if (false)')))
    if args.output_directory:
        args.output_directory.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='nr-face-geometry-') as temporary:
        out = args.output_directory or Path(temporary)
        runs = []
        for name, content in variants:
            folder = out/name
            folder.mkdir()
            (folder/'nr_face_capture_geometry.h').write_text(content, encoding='utf-8')
            cpp = folder/'test.cpp'
            cpp.write_text('#include <initializer_list>\n'+HARNESS, encoding='utf-8')
            exe = folder/'test.exe'
            subprocess.run([args.compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-static', str(cpp), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            if name == 'production':
                if result.returncode: raise RuntimeError(result.stderr)
                print(result.stdout.strip())
            else:
                if result.returncode != 23 or 'FACE_GEOMETRY_ORACLE:' not in result.stderr:
                    raise RuntimeError('Mapping negative control did not fail numerically')
                print('PASS compiled mapping negative control: '+result.stderr.strip())
            runs.append({'name': name, 'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
        if args.output_directory:
            (out/'validation.json').write_text(json.dumps({'success': True, 'game_verified': False,
                'header_sha256': hashlib.sha256(header.encode()).hexdigest(), 'runs': runs}, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
