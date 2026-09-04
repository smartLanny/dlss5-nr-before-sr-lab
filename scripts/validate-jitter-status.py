"""Exercise actual BindNrParameters transition prefix, then snapshot currentness."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);p.add_argument('--source',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
s=(a.source or root/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
start=s.index('    NgxParams *params = context.params;',s.index('bool BindNrParameters('))
end=s.index('    if (!context.guide_contract_logged',start)
prefix=s[start:end]
h=r'''
#include <atomic>
#include <cstdio>
enum class NrJitterMode:int {Legacy,GameInputPixel};
struct NgxParams{};
struct NrJitterBinding {bool known=true,valid_x=true,valid_y=true;float x=.25f,y=-.25f,scale_x=1,scale_y=1;unsigned source_width=10,source_height=10;};
struct Guides {NrJitterBinding jitter;};
struct Ctx {NgxParams* params=nullptr;bool jitter_status_initialized=true,jitter_last_known=false,jitter_binding_written=false;void*sr_handle=nullptr;};
struct {NrJitterMode jitter_mode=NrJitterMode::GameInputPixel;} g_config;
std::atomic<unsigned> g_nr_jitter_status{0};
int writes=0,failures=0,checks=0;bool retired=false;
void RetireContextForHandle(void*) {retired=true;}
template<class...A>void Log(const char*,A...){}
template<class...A>void SetFloat(A...){++writes;}
template<class...A>void SetUInt(A...){++writes;}
template<class...A>void SetInt(A...){++writes;}
bool Bind(Ctx &context,const Guides &guides) {unsigned width=10,height=10;PREFIX return true;}
void check(bool ok,const char* what) {++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);}}
HEADER
int main(){
 NgxParams params;Ctx context;context.params=&params;Guides guides;
 bool bound=Bind(context,guides);
 check(!bound&&retired&&writes==0,"validity transition retires before writing");
 check(g_nr_jitter_status.load()!=2,"transition never publishes forwarded without a write");
 context.jitter_status_initialized=false;retired=false;
 check(Bind(context,guides)&&writes==11&&!retired,"fresh valid block receives actual setters");
 EXTRA
 std::printf("production jitter binding/status: %d checks / %d failures\n",checks,failures);return failures?1:0;
}
'''.replace('PREFIX',prefix)
header=root/'src/nr_jitter_status.h'
extra=''
if header.exists():
    h=h.replace('HEADER',header.read_text())
    extra=r'''
 using namespace nr_jitter_status;
 Snapshot shot{};shot.config=4;shot.feature=5;shot.instance=6;shot.frame=7;shot.tick=100;shot.mode=2;shot.known=true;
 auto state=[&](unsigned cfg=4,unsigned long long feat=5,bool live=true,bool enabled=true,bool bypass=false,unsigned long long now=200,int mode=2){return StatusFor(shot,cfg,feat,mode,live,enabled,bypass,now);};
 check(state()==Status::Bound,"current evaluation can report bound parameters");
 check(state(9)==Status::Pending,"new config invalidates old status");
 check(state(4,9)==Status::Changed,"new carrier generation invalidates old status");
 check(state(4,5,false)==Status::Changed,"retired or new instance invalidates old status");
 check(state(4,5,true,false)==Status::Inactive,"disabled invalidates bound status");
 check(state(4,5,true,true,true)==Status::Bypassed,"current bypass invalidates bound status");
 check(state(4,5,true,true,false,2101)==Status::Stale,"no recent evaluation expires status");
 check(state(4,5,true,true,false,99)==Status::Stale,"clock reversal expires status");
 check(state(4,5,true,true,false,200,1)==Status::Pending,"mode change invalidates status");
 shot.known=false;check(state()==Status::Unknown,"missing input never claims forwarded");
 shot.frame=0;check(state()==Status::NoFrame,"no successful evaluation remains unknown");
'''
else:h=h.replace('HEADER','')
h=h.replace('EXTRA',extra)
with tempfile.TemporaryDirectory(prefix='nr-jitter-status-') as td:
 cpp=Path(td)/'jitter.cpp';exe=Path(td)/'jitter.exe';cpp.write_text(h,encoding='utf-8')
 subprocess.run([a.compiler,'-std=c++20','-O0','-static',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
