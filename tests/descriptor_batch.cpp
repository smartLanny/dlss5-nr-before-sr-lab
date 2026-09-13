#include "nr_descriptor_provenance.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
using namespace nr_descriptor;
using R=Registry<16,128,512,1024>;
View v(Resource r,unsigned fmt=10){return {r,Kind::Uav,fmt,0,0,1920,1080,true,true,true};}
void seed(R& r){r.ObserveHeap({1,1,10,10000,0,512,8,0,false});r.ObserveHeap({2,1,20,20000,50000,1024,8,0,true});r.ObserveResource({3,1,30});
 for(unsigned i=0;i<128;++i)r.Finish(r.BeginWrite(10000+8*i,0,v({3,1,30})));}
int main(){unsigned cases=0;
 // Compare the full-mirror scalar adapter with batch publication, including
 // exact receipt revisions and native-call pending masks.
 for(unsigned n: {1u,2u,16u,64u,128u}){
  R a,b;seed(a);seed(b);Id dst[128]{},src[128]{};Write wa[128]{},wb[128]{};
  for(unsigned i=0;i<n;++i){dst[i]=20000+8*i;src[i]=10000+8*i;wa[i]=a.BeginCopy(dst[i],src[i],0);}
  b.BeginCopies(n,dst,src,0,wb);
  for(unsigned i=0;i<n;++i){assert(!a.Gpu(50000+8*i,0)&&!b.Gpu(50000+8*i,0));assert(wa[i].destination.revision==wb[i].destination.revision);}
  for(unsigned i=0;i<n;++i)a.Finish(wa[i]);b.FinishCopies(n,wb);
  for(unsigned i=0;i<n;++i){auto x=a.Gpu(50000+8*i,0),y=b.Gpu(50000+8*i,0);assert(x&&y&&SameView(x.view,y.view)&&x.stamp.revision==y.stamp.revision);}
  assert(a.SnapshotCounters().writes==b.SnapshotCounters().writes);++cases;
 }
 {R r;seed(r);Id d[]={20000,20008},s[]={10000,10008};Write w[2];r.BeginCopies(2,d,s,0,w);
  r.Finish(r.BeginWrite(10000,0,v({3,1,30},11)));r.FinishCopies(2,w);
  assert(!r.Gpu(50000,0)&&r.Gpu(50008,0));++cases;
 }
 {R r;seed(r);Id d[]={20000,20008},s[]={10000,10008};Write a[2],b[2];
  r.BeginCopies(2,d,s,0,a);r.BeginCopies(2,d,s,0,b);r.FinishCopies(2,a);r.FinishCopies(2,b);
  assert(!r.Gpu(50000,0)&&!r.Gpu(50008,0));++cases;
 }
 {R r;seed(r);Id d[]={20000},s[]={10000};Write w[1];r.BeginCopies(1,d,s,0,w);r.RetireResource(3);r.FinishCopies(1,w);assert(!r.Gpu(50000,0));++cases;}
 {R r;seed(r);Id d[]={20000},s[]={10000};Write w[1];r.BeginCopies(1,d,s,0,w);r.RetireHeap(2);r.FinishCopies(1,w);assert(!r.Gpu(50000,0));++cases;}
 {R r;seed(r);Id d[]={20000},s[]={10000};Write w[1];UnknownRange range{20000,20000,0};UnknownCopy unknown;
  r.BeginCopies(1,d,s,0,w);r.BeginUnknownCopy(unknown,&range,1);r.FinishCopies(1,w);r.EndUnknownCopy(unknown);assert(!r.Gpu(50000,0));++cases;}
 {R r;seed(r);std::vector<std::thread> workers;
  for(unsigned k=0;k<4;++k)workers.emplace_back([&r,k]{Id d[16]{},s[16]{};Write w[16]{};
   for(unsigned j=0;j<16;++j){d[j]=20000+8*(k*16+j);s[j]=10000+8*j;}
   for(unsigned i=0;i<500;++i){r.BeginCopies(16,d,s,0,w);r.FinishCopies(16,w);}});
  for(auto& t:workers)t.join();for(unsigned i=0;i<64;++i)assert(r.Gpu(50000+8*i,0));++cases;}
 std::cout<<"full-mirror batch cases="<<cases<<"\n";
}
