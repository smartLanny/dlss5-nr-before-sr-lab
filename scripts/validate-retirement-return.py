"""Exercise the actual production DestroyContext body with returned-error seams.
This is a CPU ownership/ordering regression, not NGX, driver or game verification.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile


def definition(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


HARNESS = r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
using NgxResult = std::uint32_t;
constexpr NgxResult kNgxSuccess=1;
int checks=0,failures=0,depth=0,releases=0,destroys=0,slots=0,bindings=0,guide_releases=0;
NgxResult release_result=1,destroy_result=1;
std::atomic<bool> g_lifecycle_tracking_uncertain{false};
int g_nr_api_lock=0;
void check(bool ok,const char *name) {++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
struct ScopedSrwExclusive {explicit ScopedSrwExclusive(int*){++depth;}~ScopedSrwExclusive(){--depth;}};
struct NgxParams {} params;
struct NgxHandle {};
enum class PrivateNrOperation {Release};
enum class NgxLayer {PrivateBridge};
int private_calls=0,private_layers=0;
struct ScopedPrivateNrCall {
    ScopedPrivateNrCall(PrivateNrOperation,void *handle) {check(handle&&depth==1,"private Release receives live handle under provider lock");++private_calls;}
    ~ScopedPrivateNrCall(){--private_calls;}
};
struct ScopedNgxLayer {explicit ScopedNgxLayer(NgxLayer){++private_layers;}~ScopedNgxLayer(){--private_layers;}};
struct FeatureRecord {unsigned long long generation=7;};
FeatureRecord FindFeature(const NgxHandle *handle) {
    check(handle&&private_calls==1&&private_layers==1,"private Release lookup uses scoped identity");return {};
}
void ForgetObservedFeature(const NgxHandle *handle,unsigned long long generation) {
    check(handle&&generation==7&&release_result==kNgxSuccess&&private_calls==1&&private_layers==1,
          "only successful Release forgets the same observed generation");
}
struct NrSlot {int value=7;};
struct NrContext {bool create_dependencies_quarantined=false,lifecycle_dependencies_quarantined=false;void *nr_handle=nullptr;NgxParams *params=nullptr;NrSlot slots[2];void* dual_create_guides[2]{};};
NgxResult release(void *entry,void *handle) {check(depth==1&&entry&&handle,"release serialized and callable");++releases;return release_result;}
NgxResult destroy(NgxParams *p) {check(depth==1&&p,"parameter destroy serialized");++destroys;return destroy_result;}
struct NrApi {bool initialized=true;void *signed_release=&params;NgxResult(*bridge_release)(void*,void*)=release;NgxResult(*destroy)(NgxParams*)=::destroy;} g_nr_api;
void MarkLifecycleUncertain(const char*) {g_lifecycle_tracking_uncertain=true;}
template<class... T> void Log(const char*,T...) {}
template<class T> void ReleaseCom(T*& p) {if(p){++guide_releases;p=nullptr;}}
void DestroyBindings(NrContext& c) {check(depth==0&&!c.nr_handle&&!c.params&&!c.lifecycle_dependencies_quarantined,"binding tables retained until provider teardown succeeds");++bindings;}
void DestroySlot(NrSlot &s) {check(depth==0&&bindings==1,"slot teardown follows binding retirement outside provider lock");++slots;s.value=0;}
@ACTUAL@
void reset() {check(depth==0,"provider lock unwound");check(private_calls==0&&private_layers==0,"private Release scopes unwound");g_lifecycle_tracking_uncertain=false;g_nr_api={};releases=destroys=slots=bindings=guide_releases=0;release_result=destroy_result=1;}
NrContext live() {NrContext c;c.nr_handle=&params;c.params=&params;c.dual_create_guides[0]=c.dual_create_guides[1]=&params;return c;}
int main() {
 reset();{auto c=live();DestroyContext(c);check(releases==1&&destroys==1&&slots==2&&bindings==1&&guide_releases==2,"successful retirement completes once");check(!c.nr_handle&&!c.params&&!c.lifecycle_dependencies_quarantined&&!g_lifecycle_tracking_uncertain,"success clears ownership");}
 for(auto code:{0xBAD00000u,0xBAD00004u,0xBAD00007u,0xBAD0000Du}) {
   reset();release_result=code;auto c=live();DestroyContext(c);
   check(releases==1&&destroys==0&&slots==0&&bindings==0&&guide_releases==0,"failed release retains dependent resources");
   check(c.nr_handle==&params&&c.params==&params&&c.slots[0].value==7,"failed release preserves pointer closure");
   check(c.lifecycle_dependencies_quarantined&&g_lifecycle_tracking_uncertain,"failed release latches quarantine");
   DestroyContext(c);check(releases==1&&destroys==0&&slots==0&&bindings==0&&guide_releases==0,"failed context cannot reenter teardown");
   auto second=live();DestroyContext(second);check(releases==1&&destroys==0&&slots==0&&bindings==0&&guide_releases==0,"quarantine blocks subsequent provider teardown");
 }
 for(int missing=0;missing<3;++missing) {
   reset();if(missing==0)g_nr_api.initialized=false;if(missing==1)g_nr_api.bridge_release=nullptr;if(missing==2)g_nr_api.signed_release=nullptr;
   auto c=live();DestroyContext(c);check(releases==0&&destroys==0&&slots==0&&bindings==0&&guide_releases==0,"missing release entry retains dependencies");
   check(c.nr_handle&&c.params&&c.lifecycle_dependencies_quarantined&&g_lifecycle_tracking_uncertain,"missing release entry quarantines");
 }
 for(int missing=0;missing<2;++missing) {
   reset();destroy_result=0xBAD00000u;if(missing)g_nr_api.destroy=nullptr;
   auto c=live();DestroyContext(c);check(releases==1&&destroys==(missing?0:1)&&slots==0&&bindings==0&&guide_releases==0,"parameter teardown failure retains slots");
   check(!c.nr_handle&&c.params==&params&&c.lifecycle_dependencies_quarantined&&g_lifecycle_tracking_uncertain,"parameter ownership retained after handle released");
   DestroyContext(c);check(releases==1&&destroys==(missing?0:1),"no parameter failure retry");
 }
 reset();{auto c=live();c.create_dependencies_quarantined=true;DestroyContext(c);check(releases==0&&destroys==0&&slots==0&&bindings==0&&guide_releases==0,"existing Create quarantine preserved");}
 reset();{auto c=live();g_lifecycle_tracking_uncertain=true;DestroyContext(c);check(releases==0&&destroys==0&&slots==0&&bindings==0&&guide_releases==0,"existing uncertain GPU lifetime preserved");}
 reset();{auto c=live();c.nr_handle=nullptr;DestroyContext(c);check(releases==0&&destroys==1&&slots==2,"parameter-only context still retires");}
 reset();{auto c=live();c.params=nullptr;DestroyContext(c);check(releases==1&&destroys==0&&slots==2,"handle-only context still retires");}
 check(depth==0,"final lock balance");
 std::printf("production DestroyContext: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]/'src/nr_before_sr.cpp')
    p.add_argument('--compiler',default=os.environ.get('CXX','g++'))
    p.add_argument('--expect-rejection',action='store_true')
    a=p.parse_args()
    compiler=shutil.which(a.compiler)
    if not compiler: raise SystemExit('C++ compiler not found')
    actual=definition(a.source.read_text(encoding='utf-8-sig'),'void DestroyContext(NrContext &context)')
    with tempfile.TemporaryDirectory(prefix='nr-retirement-') as td:
        source=Path(td)/'test.cpp';exe=Path(td)/('test.exe' if os.name=='nt' else 'test')
        source.write_text(HARNESS.replace('@ACTUAL@',actual),encoding='utf-8')
        flags=['-std=c++20','-O0','-Wall','-Wextra','-Werror']+(['-static'] if os.name=='nt' else [])
        subprocess.run([compiler,*flags,str(source),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],text=True,capture_output=True)
        print(result.stdout,end='');print(result.stderr,end='')
        if a.expect_rejection:
            required=['FAIL failed release retains dependent resources','FAIL quarantine blocks subsequent provider teardown','FAIL parameter teardown failure retains slots']
            if result.returncode!=1 or any(s not in result.stdout for s in required):raise SystemExit('Historical control did not fail the exact ownership oracles')
            print('PASS historical negative control: exact unsafe retirement paths rejected')
        elif result.returncode:raise SystemExit(result.returncode)

if __name__=='__main__':main()
