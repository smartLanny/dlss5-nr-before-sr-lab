"""Compile actual staged Release hooks; validate RR delayed-retirement dispatch."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('post_parser',ROOT/'scripts/patch-post-compat.py')
parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',type=Path,required=True)
    ap.add_argument('--compiler',required=True)
    ap.add_argument('--negative-control',action='store_true')
    args=ap.parse_args()
    source=args.source.read_text(encoding='utf-8-sig')
    hooks=[]
    for marker in ('NgxResult HookRelease(', 'NgxResult HookCoreRelease(', 'NgxResult HookDlssdRelease('):
        a,b=parser.block(source,marker);body=source[a:b]
        assert body.count('feature.feature_id == 1 || feature.feature_id == 13')==1
        if args.negative_control:
            body=body.replace('feature.feature_id == 1 || feature.feature_id == 13','feature.feature_id == 1')
        hooks.append(body)
    prefix=r'''
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using NgxResult=unsigned; struct NgxHandle{};
constexpr unsigned kNgxSuccess=1,kNgxFeatureNotFound=2;
enum class NgxLayer {Dlss,Core,Dlssd}; enum class NgxOwner {Game,ExternalNr};
struct ScopedNgxLayer { explicit ScopedNgxLayer(NgxLayer){} };
struct FeatureRecord { NgxHandle*handle; int feature_id; NgxOwner owner=NgxOwner::Game; NgxHandle*context_handle=nullptr; unsigned long long generation=1; } record;
void ObserveNgxRelease(const NgxHandle*){}
unsigned g_release_hook_depth=0,calls=0,retired=0,forgotten=0;int g_chain_cs=0,locks=0;
NgxResult native_result=0;
std::atomic<bool> g_shortfuse_nr_enabled{false},g_feature_tracking_uncertain{false};
bool LatchGenericRenoDxConflictIfLoaded(){return false;}
FeatureRecord FindFeature(NgxHandle*){return record;}
enum class ModuleObserverFault{UnknownRelease};
void LatchUnknownFeatureHandle(const char*,const char*,ModuleObserverFault){}
void LatchNestedRrOwnershipChange(const char*){}
void LatchConcurrentRrOwnershipChange(const char*){}
void LatchRrOwnershipUncertain(const char*,const char*){}
struct ScopedRrOwnershipGate { ScopedRrOwnershipGate(bool,bool,bool){} bool is_nested(){return false;} bool acquisition_failed(){return false;} };
void EnterCriticalSection(int*){++locks;} void LeaveCriticalSection(int*){--locks;}
void QueueNgxContextRetirement(NgxHandle*,unsigned long long){if(locks)std::abort();++retired;}
void ForgetObservedFeature(NgxHandle*,unsigned long long){++forgotten;}
template<class...T>void Log(const char*,T...){}
const char*FeatureName(int){return "fixture";} const char*ResultName(unsigned){return "fixture";}
NgxResult NativeRelease(NgxHandle*){++calls;return native_result;}
auto g_original_release=NativeRelease,g_original_core_release=NativeRelease,g_original_dlssd_release=NativeRelease;
'''
    suffix=r'''
int main(){unsigned checks=0;NgxHandle h;
 for(auto hook:{HookRelease,HookCoreRelease,HookDlssdRelease})
 for(int feature:{1,13,18}) for(unsigned result:{1u,2u,3u}) for(unsigned nested:{0u,1u}){
  record={&h,feature};record.context_handle=&h;native_result=result;g_release_hook_depth=nested;
  calls=retired=forgotten=0;locks=0;
  auto returned=hook(&h);bool success=result==1||result==2;
  bool expected=success&&(feature==1||feature==13);
  if(returned!=result||calls!=1||retired!=unsigned(expected)||forgotten!=unsigned(success)||locks||g_release_hook_depth!=nested){
    std::fprintf(stderr,"FAIL RR retirement feature=%d result=%u nested=%u retired=%u expected=%u\n",feature,result,nested,retired,unsigned(expected));return 1;}
  ++checks;
 }
 std::printf("PASS actual Release hooks: %u retirement/result/nesting scenarios\n",checks);
}
'''
    with tempfile.TemporaryDirectory(prefix='nr-rr-retire-') as tmp:
        cpp=Path(tmp)/'retire.cpp';exe=Path(tmp)/'retire.exe'
        cpp.write_text(prefix+'\n'.join(hooks)+suffix,encoding='utf-8')
        subprocess.run([args.compiler,'-std=c++17','-Wall','-Wextra','-Werror','-static',str(cpp),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        print(result.stdout+result.stderr,end='')
        if args.negative_control:
            if result.returncode!=1 or 'FAIL RR retirement feature=13' not in result.stderr:raise SystemExit('Historical RR leak not detected')
            print('PASS historical missing-RR-retirement control rejected')
        elif result.returncode:raise SystemExit(result.returncode)

if __name__=='__main__':main()
