#include <cassert>
#include <limits>
#include "../src/nr_rr_projection_source.h"
int main(){
 using namespace nr_rr_projection;
 using nr_rr_depth::InputState;
 Input p;p.state=InputState::Supported;p.projection={0,.1f,true};
 Cache cache;Source source{1,2,3};cache.Put(source,p,100);
 assert(cache.Get(source,100).state==InputState::Supported);
 assert(cache.Get({1,2,4},100).state==InputState::Absent);
 assert(cache.Get({2,2,3},100).state==InputState::Absent);
 assert(cache.Get({1,3,3},100).state==InputState::Absent);
 assert(cache.Get(source,100,2).state==InputState::Absent);
 assert(cache.Get(source,99).state==InputState::Absent);
 assert(cache.Get(source,1101).state==InputState::Absent);
 int commands=0,other=0,params=0;
 {
   Evaluation e;e.Set(source,p,&commands);
   NativeScope n(&commands,&params,7);
   assert(Choose({},&params,7).state==InputState::Supported);
   assert(Choose({},&params,8).state==InputState::Absent);
   assert(Choose({},&other,7).state==InputState::Absent);
   Input invalid;invalid.state=InputState::Nonfinite;
   assert(Choose(invalid,&params,7).state==InputState::Nonfinite);
   Input original=p;original.projection.b=9;
   assert(Choose(original,&params,7).projection.b==9);
   {NativeScope wrong(&other,&params,7);assert(Choose({},&params,7).state==InputState::Absent);}
   assert(Choose({},&params,7).state==InputState::Supported);
   {Evaluation nested;NativeScope inside(&commands,&params,7);assert(Choose({},&params,7).state==InputState::Absent);}
 }
 assert(Choose({},&params,7).state==InputState::Absent);
 cache.Put(source,{},101);assert(cache.Get(source,101).state==InputState::Absent);
 for(unsigned i=0;i<96;++i)cache.Put({1,2,10+i},p,100);
 assert(cache.Get(source,101).state==InputState::Absent);
 for(unsigned i=0;i<96;++i)assert(cache.Get({1,2,10+i},101).state==InputState::Supported);
}
