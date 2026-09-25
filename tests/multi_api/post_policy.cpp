#include <cstdlib>
#include <cstdio>
#include <limits>
#include "src/interop/nr_post_compat.h"
#include "src/nr_compat_present_policy.hpp"
using namespace nr_post_compat;
static unsigned n=0;
#define CHECK(x) do{++n;if(!(x)){std::fprintf(stderr,"FAIL %u %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 for(int id: {1,11,13,18,-1}) for(int mode:{1,2}) for(bool rr:{false,true}) {
  auto route=Select(id,mode,rr);
  CHECK((route==Route::AfterRr)==(id==13&&rr));
  CHECK((route==Route::BeforeSr)==(id==1&&mode==2));
  CHECK((route==Route::AfterSr)==(id==1&&mode==1));
 }
 for(unsigned w:{1u,63u,1280u,1919u,2560u,3839u,7680u})
 for(unsigned h:{1u,719u,1080u,2159u})for(unsigned pc=25;pc<=100;++pc){
  auto p=Work({w,h},pc); CHECK(p.work.w>0&&p.work.h>0);
  CHECK(p.work.w<=w&&p.work.h<=h);
  const auto present=nr_compat_present::WorkExtent({w,h},pc);
  CHECK(present.width==p.work.w&&present.height==p.work.h);
  CHECK(nr_compat_present::Valid(nr_compat_present::Options{false,false,false,pc}));
  if(pc==100) CHECK(p.work.w==w&&p.work.h==h&&!p.scaled);
 }
 for(unsigned invalid:{0u,1u,24u,101u,1000u,0xffffffffu}) {
  CHECK(!SupportedPercent(invalid));CHECK(Percent(invalid)==0);
  const auto p=Work({1920,1080},invalid);CHECK(p.work.w==0&&p.work.h==0);
  CHECK(!nr_compat_present::Valid(nr_compat_present::Options{false,false,false,invalid}));
  CHECK(!nr_compat_present::Valid(nr_compat_present::WorkExtent({1920,1080},invalid)));
 }
 const auto quarter=Work({3840,2160},25);
 CHECK(quarter.work.w==960&&quarter.work.h==540&&quarter.scale==.25f);
 const auto odd=Work({3839,2159},25);
 CHECK(odd.work.w==960&&odd.work.h==540);
 for(bool low_resolution:{false,true}) {
  const auto q=Motion({1280,720},{3840,2160},quarter.work,low_resolution,-1280,-720);
  CHECK(q.valid&&q.bound.w==960&&q.bound.h==540&&q.resample);
  CHECK(q.scale_x==-960&&q.scale_y==-540);
  CHECK(q.source.w==(low_resolution?1280u:3840u));
 }
 auto m=Motion({1280,720},{3840,2160},{1920,1080},false,-1280,-720);
 CHECK(m.valid&&m.source.w==3840&&m.bound.w==1920&&m.resample);
 CHECK(m.scale_x==-1920&&m.scale_y==-1080);
 auto low=Motion({1280,720},{3840,2160},{1920,1080},true,1,1);
 CHECK(low.source.w==1280&&low.scale_x==1.5f&&low.resample);
 CHECK(!Motion({0,720},{3840,2160},{1920,1080},true,1,1).valid);
 CHECK(!Motion({1280,720},{3840,2160},{1920,1080},true,0,1).valid);
 CHECK(!Motion({1280,720},{3840,2160},{1920,1080},true,std::numeric_limits<float>::quiet_NaN(),1).valid);
 CHECK(CanFinish(1,9,9,true,false,true));
 for(auto result:{0,2,-1})CHECK(!CanFinish(result,9,9,true,false,true));
 CHECK(!CanFinish(1,0,0,true,false,true)); CHECK(!CanFinish(1,9,10,true,false,true));
 CHECK(!CanFinish(1,9,9,false,false,true)); CHECK(!CanFinish(1,9,9,true,true,true));
 CHECK(!CanFinish(1,9,9,true,false,false));
 std::printf("%u post-route/extent/motion/result checks passed\n",n);
}
