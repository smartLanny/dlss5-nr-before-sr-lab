"""CPU-only exact-source regression of the private descriptor-heap hook."""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

def extract(source: str) -> str:
    needle = 'template<unsigned I> void STDMETHODCALLTYPE StateHookSetDescriptorHeaps('
    start = source.index(needle)
    brace = source.index('{', start)
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == '{': depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0: return source[start:i + 1]
    raise ValueError('unclosed production hook')

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    root, out = a.root.resolve(), a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    hook_file = root / 'src/nr_command_state.inl'
    helper_file = root / 'src/nr_private_descriptor_heaps.h'
    test_file = root / 'tests/private_descriptor_heaps_test.cpp'
    hook = extract(hook_file.read_text(encoding='utf-8-sig'))
    prefix = test_file.read_text(encoding='utf-8-sig').split('int main(){', 1)[0]
    host = prefix + r'''
#include <atomic>
#define NR_HAS_PRIVATE_RESIDUAL_SR 1
#define NR_HAS_DESCRIPTOR_PROVENANCE 1
#define NR_HAS_POST_BOUNDARY_TRACE 1
namespace nr_post_product { bool private_call=false; bool PrivateCall(){return private_call;} }
struct CommandStateHookFamily { void* originals[4]{}; };
CommandStateHookFamily g_state_SetDescriptorHeaps;
struct NativeCommandState { ID3D12DescriptorHeap* heaps[2]{}; };
NativeCommandState state;
ID3D12DescriptorHeap* original_heaps[2]{};
ID3D12DescriptorHeap* observed_heaps[2]{};
UINT original_count=0, observed_count=0;
void Log(const char*,...) {}
void STDMETHODCALLTYPE Original(ID3D12GraphicsCommandList*,UINT count,ID3D12DescriptorHeap* const* heaps){
    original_count=count;
    for(UINT i=0;i<count&&i<2;++i){original_heaps[i]=heaps[i];
        if(heaps[i]) Check(Heap::Of(heaps[i]).refs>=1,"original sees live reference");}
}
void NrDescriptorObserveBoundHeaps(ID3D12GraphicsCommandList*,UINT count,ID3D12DescriptorHeap* const* heaps){
    observed_count=count;for(UINT i=0;i<count&&i<2;++i)observed_heaps[i]=heaps[i];
}
template<class F> void MutateNativeCommandState(ID3D12GraphicsCommandList*,F f){f(state);}
void CommandStateSetHeaps(NativeCommandState& s,UINT count,ID3D12DescriptorHeap* const* heaps){
    for(UINT i=0;i<2;++i)s.heaps[i]=i<count?heaps[i]:nullptr;
}
#define NR_POST_OBSERVE_STATE(list,name) ((void)0)
''' + hook + r'''
int main(){
    g_state_SetDescriptorHeaps.originals[0]=reinterpret_cast<void*>(&Original);
    Heap raw_a,raw_b,proxy_a(&raw_a),proxy_b(&raw_b);
    ID3D12DescriptorHeap* pair[]={&proxy_a.iface,&raw_b.iface};
    nr_post_product::private_call=true;
    StateHookSetDescriptorHeaps<0>(nullptr,2,pair);
    Forward(original_heaps,2,&raw_a.iface,&raw_b.iface);
    Check(original_count==2&&observed_count==2,"private forwards and observes two heaps");
    Check(original_heaps[0]==&raw_a.iface&&observed_heaps[0]==&raw_a.iface&&state.heaps[0]==&raw_a.iface,
        "private original, provenance and state use same raw heap");
    Check(original_heaps[1]==&raw_b.iface&&observed_heaps[1]==&raw_b.iface&&state.heaps[1]==&raw_b.iface,
        "private raw heap passed unchanged");
    Check(raw_a.refs==1&&raw_a.releases==1,"private temporary reference released after hook");
    const auto proxy_queries=proxy_a.queries;
    nr_post_product::private_call=false;
    StateHookSetDescriptorHeaps<0>(nullptr,2,pair);
    Check(proxy_a.queries==proxy_queries,"nonprivate bypasses normalization");
    Check(original_heaps[0]==&proxy_a.iface&&observed_heaps[0]==&proxy_a.iface&&state.heaps[0]==&proxy_a.iface,
        "nonprivate original, provenance and state preserve proxy");
    nr_post_product::private_call=true;
    ID3D12DescriptorHeap* both[]={&proxy_a.iface,&proxy_b.iface};
    StateHookSetDescriptorHeaps<0>(nullptr,2,both);
    Check(original_heaps[0]==&raw_a.iface&&original_heaps[1]==&raw_b.iface&&raw_a.refs==1&&raw_b.refs==1,
        "two proxy heaps normalized and released");
    StateHookSetDescriptorHeaps<0>(nullptr,0,nullptr);
    Check(original_count==0&&observed_count==0,"zero count forwarded");
    ID3D12DescriptorHeap* empty[]={nullptr};
    StateHookSetDescriptorHeaps<0>(nullptr,1,empty);
    Check(original_count==1&&original_heaps[0]==nullptr&&state.heaps[0]==nullptr,"null heap forwarded");
    std::printf("exact hook checks=%u\n",checks);
}
'''
    generated = out / 'private_descriptor_heaps_hook_test.cpp'
    generated.write_text(host, encoding='utf-8')
    binary = out / 'private_descriptor_heaps_hook_test.exe'
    compile_cmd = [str(a.compiler), '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', '-static',
                   '-I' + str(root / 'src'), str(generated), '-o', str(binary)]
    runs = []
    for name, cmd in [('compile', compile_cmd), ('run', [str(binary)])]:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        (out / (name + '.log')).write_text(r.stdout + r.stderr, encoding='utf-8')
        runs.append({'name': name, 'exit': r.returncode, 'stdout': r.stdout, 'stderr': r.stderr})
        if r.returncode: break
    report = {'passed': len(runs)==2 and all(x['exit']==0 for x in runs), 'kind':'CPU COM doubles; exact production hook body',
              'game':False,'gpu':False,
              'source_sha256': {str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in (hook_file, helper_file, test_file)},
              'hook_sha256':hashlib.sha256(hook.encode()).hexdigest(), 'runs':runs}
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(runs[-1]['stdout'] or runs[-1]['stderr'])
    if not report['passed']: raise SystemExit(1)

if __name__ == '__main__': main()
