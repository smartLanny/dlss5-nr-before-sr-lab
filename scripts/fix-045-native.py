"""Fixed-source native boundary fix. Export blobs only after real Windows regressions."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import urllib.request

REPO='smartLanny/dlss5-nr-before-sr-lab'
BASE='12009d0448fb8bba8c699020382bf5bb6b84dec4'

def main():
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--compiler-root',required=True);a=p.parse_args();root=a.root.resolve()
 def git(*args,raw=False):return subprocess.check_output(['git',*args],cwd=root,text=not raw)
 if git('rev-parse','HEAD').strip()!=BASE:raise RuntimeError('Unexpected base')
 path=root/'src/nr_command_state.inl';s=path.read_text()
 old='''NR_STATE_HOOK(OMSetBlendFactor, (ID3D12GraphicsCommandList *command_list, const FLOAT values[4]),
    (command_list, values), if (values) memcpy(state.dynamic.blend, values, sizeof(state.dynamic.blend)); else for (float &value : state.dynamic.blend) value=1.0f; state.dynamic.recorded |= kDynamicBlend;)'''
 assert s.count(old)==1
 new='''// Microsoft documents NULL BlendFactor as {1,1,1,1}. Canonicalize before
// forwarding: the non-debug WARP implementation in our Windows regression
// dereferences a raw NULL before the old post-call observer can process it.
// This preserves documented values without catching exceptions or changing PSOs.
// https://learn.microsoft.com/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetblendfactor
CommandStateHookFamily g_state_OMSetBlendFactor;
constexpr FLOAT kDefaultBlendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
template <unsigned int I> void STDMETHODCALLTYPE StateHookOMSetBlendFactor(
        ID3D12GraphicsCommandList *command_list, const FLOAT values[4]) {
    using Fn = decltype(ID3D12GraphicsCommandListVtbl::OMSetBlendFactor);
    const FLOAT *effective = values != nullptr ? values : kDefaultBlendFactor;
    reinterpret_cast<Fn>(g_state_OMSetBlendFactor.originals[I])(command_list, effective);
    MutateNativeCommandState(command_list, [&](NativeCommandState &state) {
        memcpy(state.dynamic.blend, effective, sizeof(state.dynamic.blend));
        state.dynamic.recorded |= kDynamicBlend;
    });
}
void *const g_state_detours_OMSetBlendFactor[4] = {
    reinterpret_cast<void *>(&StateHookOMSetBlendFactor<0>), reinterpret_cast<void *>(&StateHookOMSetBlendFactor<1>),
    reinterpret_cast<void *>(&StateHookOMSetBlendFactor<2>), reinterpret_cast<void *>(&StateHookOMSetBlendFactor<3>) };'''
 path.write_text(s.replace(old,new),newline='\n')
 path=root/'tests/pristine_null_native.inl';s=path.read_text();assert '    g_allow_experimental=false;' in s
 s=s.replace('    g_allow_experimental=false;','    setvbuf(stdout,nullptr,_IONBF,0); // Preserve exact checkpoints if a future native regression terminates.\n    g_allow_experimental=false;',1);path.write_text(s,newline='\n')
 for script,extra in [('validate-command-state-native.ps1',[]),('validate-command-state-gpu.ps1',[]),('validate-command-state-gpu.ps1',['-BlendStateNegativeControl'])]:
  subprocess.run(['pwsh','-NoProfile','-File','scripts/'+script,'-ToolchainRoot',a.compiler_root,*extra],cwd=root,check=True)
 path=root/'docs/releases/beta0.4.5.md';s=path.read_text()
 s+='\n## 发布前原生边界验证\n新增非调试 WARP 原生测试发现：直接透传 OMSetBlendFactor(NULL) 会在原调用内 AV，尚未进入观察器。按微软定义的全1值在调用前规范化 NULL，保留真实非空指针和全部其它状态；无异常捕获/重试。实际原生测试继续保留 NULL 用例，完整 WARP 像素对照与错误恢复负例分别验证。此项不宣称是玩家已有崩溃的根因。\n'
 path.write_text(s,newline='\n')
 paths=['src/nr_command_state.inl','tests/pristine_null_native.inl','docs/releases/beta0.4.5.md']
 git('add','--',*paths);entries=[]
 for path in paths:
  data=git('show',':'+path,raw=True)
  req=urllib.request.Request('https://api.github.com/repos/'+REPO+'/git/blobs',data=json.dumps({'encoding':'utf-8','content':data.decode('utf-8')}).encode(),method='POST',headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Content-Type':'application/json'})
  with urllib.request.urlopen(req,timeout=60) as r:sha=json.load(r)['sha']
  assert sha==hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
  entries.append({'path':path,'mode':'100644','type':'blob','sha':sha})
 print('REVIEWED_TREE_ENTRIES '+json.dumps({'base':BASE,'entries':entries}))
if __name__=='__main__':main()
