"""Exercise actual jitter binding, post zero preparation and snapshot currentness."""
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
post_start=s.index('    guides.jitter.known = true;')
post_end=s.index('    NrContext *context = FindOrCreateContext(',post_start)
post_zero=s[post_start:post_end]
h=r'''
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>
enum class NrJitterMode:int {Legacy,GameInputPixel};
struct NgxParams{};
struct NrJitterBinding {bool known=true,valid_x=true,valid_y=true;float x=.25f,y=-.25f,source_x=.5f,source_y=-.5f,scale_x=1,scale_y=1;unsigned source_width=10,source_height=10;};
struct Guides {NrJitterBinding jitter;float jitter_offset_x=.25f,jitter_offset_y=-.25f;};
struct Ctx {NgxParams* params=nullptr;bool jitter_status_initialized=true,jitter_last_known=false,jitter_binding_written=false;void*sr_handle=nullptr;};
struct {NrJitterMode jitter_mode=NrJitterMode::GameInputPixel;} g_config;
std::atomic<unsigned> g_nr_jitter_status{0};
int writes=0,failures=0,checks=0;bool retired=false;
struct Setter {std::string key;double value;};std::vector<Setter> setters;
void RetireContextForHandle(void*) {retired=true;}
template<class...A>void Log(const char*,A...){}
void SetFloat(NgxParams*,const char* key,float value){++writes;setters.push_back({key,value});}
void SetUInt(NgxParams*,const char* key,unsigned value){++writes;setters.push_back({key,static_cast<double>(value)});}
void SetInt(NgxParams*,const char* key,int value){++writes;setters.push_back({key,static_cast<double>(value)});}
bool Bind(Ctx &context,const Guides &guides,unsigned width=10,unsigned height=10) {PREFIX return true;}
void check(bool ok,const char* what) {++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);}}
HEADER
int main(){
 NgxParams params;Ctx context;context.params=&params;Guides guides;
 bool bound=Bind(context,guides);
 check(!bound&&retired&&writes==0,"validity transition retires before writing");
 check(g_nr_jitter_status.load()!=2,"transition never publishes forwarded without a write");
 context.jitter_status_initialized=false;retired=false;
 check(Bind(context,guides)&&writes==11&&!retired,"fresh valid block receives actual setters");
 guides.jitter.known=false;retired=false;const auto prior_writes=writes;
 check(!Bind(context,guides)&&retired&&writes==prior_writes,"valid-to-unknown retires before stale offsets can be reused");
 context.jitter_status_initialized=false;retired=false;setters.clear();
 g_config.jitter_mode=NrJitterMode::Legacy;
 check(Bind(context,guides)&&!retired&&setters.size()==1&&setters[0].key=="DLSSNR.Jitter.Valid"&&setters[0].value==0,
       "legacy omitted binding writes only validity, never synthetic zero coordinates");
 const unsigned width=100,height=50,work_width=50,work_height=25;
 POST_ZERO
 context.jitter_status_initialized=false;setters.clear();
 check(Bind(context,guides,work_width,work_height)&&setters.size()==11&&guides.jitter.known,
       "actual post preparation keeps a known binding when game jitter forwarding is off");
 for(const auto& setter:setters)if(setter.key=="Jitter.Offset.X"||setter.key=="Jitter.Offset.Y"||
     setter.key=="DLSSNR.Jitter.Offset.X"||setter.key=="DLSSNR.Jitter.Offset.Y")
   check(setter.value==0,"actual post reconstructed grid writes explicit zero coordinates");
 for(const auto& setter:setters) {
   if(setter.key=="DLSSNR.Jitter.WorkWidth")check(setter.value==work_width,"post binding uses actual work width");
   if(setter.key=="DLSSNR.Jitter.WorkHeight")check(setter.value==work_height,"post binding uses actual work height");
 }
 check(guides.jitter.scale_x==.5f&&guides.jitter.scale_y==.5f&&guides.jitter_offset_x==0&&guides.jitter_offset_y==0,
       "post zero retains the actual source-to-work geometry");
 EXTRA
 std::printf("production jitter binding/status: %d checks / %d failures\n",checks,failures);return failures?1:0;
}
'''.replace('PREFIX',prefix).replace('POST_ZERO',post_zero)
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
