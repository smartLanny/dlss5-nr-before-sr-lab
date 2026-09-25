#include "nr_late_present_receipt.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace nr_present_mapping;
using namespace nr_late_present;
Candidate candidate(){return {3,2,40,40,60,60,7,9,10,5,1920,1080,true,true,true,true,false,false,false,false};}
int main(){unsigned cases=0;
 auto m=Map({1920,1080},{3840,2160});assert(m&&m.x==2&&m.y==2&&m.scaled&&!m.rounded_aspect);++cases;
 assert(!Map({1920,1080},{3840,2160},false));++cases;
 assert(Map({3840,2160},{3840,2160},false));++cases;
 assert(!Map({1920,1080},{3840,1600}));++cases;
 assert(!Map({0,1080},{3840,2160}));++cases;
 assert(!Map({1920,1080},{65535,65535}));++cases;
 auto c=candidate();Request q{7,3840,2160,5,false,false,false};
 assert(q.automatic_full_viewport_scale&&Select(q,&c,1));++cases;
 q.automatic_full_viewport_scale=false;assert(Select(q,&c,1).decision==Decision::WrongExtent);++cases;
 q.automatic_full_viewport_scale=true;
 for(int kind=0;kind<12;++kind){auto bad=c;auto req=q;
  switch(kind){case 0:bad.submitted=false;break;case 1:bad.native_succeeded=false;break;
   case 2:bad.consumed=true;break;case 3:bad.latest_sequence+=3;break;
   case 4:bad.device=8;break;case 5:bad.config_generation=6;break;
   case 6:req.known_generated=true;break;case 7:req.known_inline_processed=true;break;
   case 8:req.known_fg_consumed=true;break;case 9:bad.poisoned=true;break;
   case 10:bad.producer_fence_value=0;break;case 11:bad.native_result_known=false;break;}
  assert(!Select(req,&bad,1));++cases;
 }
 Candidate cs[]={c,c};cs[1].stream=4;assert(Select(q,cs,2).decision==Decision::Ambiguous);++cases;
 cs[1].stream=3;cs[1].sequence=39;assert(Select(q,cs,2).index==0);++cases;
 float x=1920,y=1080;assert(ScaleToModel(x,y,{1920,1080},{3840,2160}));assert(x==3840&&y==2160);++cases;
 x=-1920;y=1080;assert(ScaleToModel(x,y,{1920,1080},{3840,2160}));assert(x==-3840&&y==2160);++cases;
 const auto maximum=std::numeric_limits<float>::max();x=maximum;y=1;
 assert(!ScaleToModel(x,y,{1,1},{16384,16384})&&x==maximum&&y==1);++cases;
 x=1;y=std::numeric_limits<float>::quiet_NaN();assert(!ScaleToModel(x,y,{1920,1080},{3840,2160})&&x==1);++cases;
 x=0;y=1;assert(!ScaleToModel(x,y,{1920,1080},{3840,2160}));++cases;
 assert(Map({2228,1253},{3840,2160}).rounded_aspect);++cases;
 assert(Map({1281,720},{3840,2160}).rounded_aspect);++cases;
 assert(!Map({1280,722},{3840,2160}));++cases;
 assert(!Map({8,4},{8,5}));++cases;
 assert(Map({3840,2160},{1920,1080}).x==0.5f);++cases;
 Demands<2> d;assert(d.Needed(7,5,{1920,1080}));++cases;
 d.Publish(10,7,5,{3840,2160});assert(d.Needed(7,5,{1920,1080}));++cases;
 assert(!d.Needed(7,5,{1920,1080},false));++cases;
 assert(d.Needed(7,6,{1920,1080}));++cases;
 d.Publish(10,7,6,{1920,1080});assert(d.Needed(7,6,{1920,1080},false));++cases;
 d.Publish(11,7,6,{1920,1080});assert(d.Needed(7,6,{1920,1080}));++cases;
 d.Forget(11);assert(d.Needed(7,6,{1920,1080}));++cases;
 d.Forget(10);assert(d.Needed(7,6,{1920,1080}));++cases;
 d.Publish(10,7,6,{3840,2160});d.Publish(11,8,6,{1920,1080});d.Publish(12,9,6,{1920,1080});
 assert(d.Needed(7,6,{3840,2160}));++cases;
 x=2;y=-2;assert(CompensateLayerMotion(x,y,{640,360},{320,180},0)&&x==2&&y==-2);++cases;
 assert(CompensateLayerMotion(x,y,{640,360},{320,180},1)&&x==1&&y==-1);++cases;
 x=2;y=-2;assert(CompensateLayerMotion(x,y,{640,360},{640,360},1)&&x==2&&y==-2);++cases;
 x=maximum;y=2;assert(!CompensateLayerMotion(x,y,{1,1},{16384,16384},1)&&x==maximum&&y==2);++cases;
 std::cout<<"mapping/receipt/demand cases="<<cases<<"\n";
}
