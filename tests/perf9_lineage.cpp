// Complete frozen/current production Lineage, not a second pending-bit model.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include "lineage_old.hpp"
#include "lineage_current.hpp"
struct Old {using L=perf9_reference::Lineage;using R=perf9_reference::Recording;using V=perf9_reference::Resource;using Label=perf9_reference::Label;using Role=perf9_reference::Role;};
struct Now {using L=nr_source_frame::Lineage;using R=nr_source_frame::Recording;using V=nr_source_frame::Resource;using Label=nr_source_frame::Label;using Role=nr_source_frame::Role;};
using Id=std::uint64_t;
static void check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
template<class T> typename T::R rec(unsigned i,unsigned reset=1){return {0x10000+Id(i%80)*16,1+Id(i/80),reset,1+Id(i%2)};}
template<class T> typename T::V resource(unsigned i){return {0x30000+Id(i%280)*32,1+Id(i/280),1+Id(i%2),i%3};}
template<class Label> auto fields(const Label& v){return std::array<Id,13>{v.source.stream,v.source.epoch,v.source.sequence,Id(v.role),v.queue,v.submission,Id(v.submitted),v.content,Id(v.exact_write),Id(v.tainted),Id(v.processed),Id(v.fg_consumed),v.encoding};}
template<class T> bool act(typename T::L& l,unsigned op,unsigned i,unsigned j,unsigned serial){
 auto r=rec<T>(i,1+serial%3);auto dst=resource<T>(i),src=resource<T>(j);
 typename T::Label label{};label.source={1+Id(i%3),1+Id(serial%2),1+Id(serial)};label.role=static_cast<typename T::Role>(j%4);label.processed=j%5==0;label.fg_consumed=j%7==0;label.encoding=j%3;
 switch(op){
  case 0:return l.Set(r,dst,label);
  case 1:return l.Copy(r,dst,src,serial%3!=0);
  case 2:return l.Invalidate(r,dst);
  case 3:return l.InvalidateDevice(r);
  case 4:return l.Clear(r,dst);
  case 5:return l.Submit(r,serial%5?1+Id(j%2):0,serial%7!=0);
  case 6:l.Reset(r);return true;
  case 7:l.Forget(dst);return true;
  case 8:l.ForgetAddress(dst.address);return true;
  case 9:l.ForgetList(r.list);return true;
  case 10:l.RetireSource(label.source.stream,label.source.epoch);return true;
  case 11:l.Presented(dst,label,serial%3);return true;
  default:l.DescribeSurface(dst,1+j%3);return true;
 }
}
static void compare(Old::L& a,Now::L& b,unsigned seed){
 check(a.AnyPending()==b.AnyPending(),"pending summary diverged");
 for(unsigned i=0;i<160;++i){auto r=rec<Old>(i,1+seed%3);auto s=rec<Now>(i,1+seed%3);
  check(a.PendingList(r)==b.PendingList(s),"exact PendingList");
  check(a.TracksList(r.list,r.lifetime)==b.TracksList(s.list,s.lifetime),"list/lifetime membership");}
 for(unsigned n=0;n<24;++n){const unsigned i=(seed+n*13)%560;auto x=resource<Old>(i);auto y=resource<Now>(i);
  check(fields(a.Lookup(x))==fields(b.Lookup(y)),"submitted image label");
  check(fields(a.Lookup(x,rec<Old>(i,1+seed%3)))==fields(b.Lookup(y,rec<Now>(i,1+seed%3))),"pending event replay");}
}
static void semantics(){
 auto a=std::make_unique<Old::L>();auto b=std::make_unique<Now::L>();
 // Full event batches, overflow, failed submission, replay, and reuse must keep
 // exactly the same pending truth; overflow is NOT permission to stop tracking.
 for(unsigned i=0;i<65;++i){for(unsigned n=0;n<51;++n)check(act<Old>(*a,0,i,n,0)==act<Now>(*b,0,i,n,0),"capacity/overflow admission");compare(*a,*b,i);}
 for(unsigned i=0;i<65;++i){check(act<Old>(*a,5,i,0,0)==act<Now>(*b,5,i,0,0),"failed native result");compare(*a,*b,i);act<Old>(*a,6,i,0,0);act<Now>(*b,6,i,0,0);compare(*a,*b,i);}
 // Bit 63, all slots, invalid recording, repeated failed/successful native calls.
 for(unsigned i=0;i<64;++i){typename Old::Label x{};typename Now::Label y{};x.source={1,1,i+1};y.source={1,1,i+1};
  check(a->Set(rec<Old>(i),resource<Old>(i),x)==b->Set(rec<Now>(i),resource<Now>(i),y),"all-slot publication");}
 for(unsigned i=0;i<63;++i){a->Reset(rec<Old>(i));b->Reset(rec<Now>(i));}
 check(a->AnyPending()&&b->AnyPending(),"highest slot remains pending");
 a->Reset(rec<Old>(63));b->Reset(rec<Now>(63));check(!a->AnyPending()&&!b->AnyPending(),"last reset empties pending");
 check(a->Submit({},1,true)==b->Submit({},1,true),"legacy invalid Submit behavior");
 std::mt19937 rng(0x900277);
 for(unsigned n=0;n<12000;++n){unsigned op=rng()%13,i=rng()%160,j=rng()%560,serial=rng()%40;
  check(act<Old>(*a,op,i,j,serial)==act<Now>(*b,op,i,j,serial),"mixed operation return");
  check(a->AnyPending()==b->AnyPending(),"summary after each transition");
  if(n%53==0)compare(*a,*b,n);
 }
 auto ca=std::make_unique<Old::L>(*a);auto cb=std::make_unique<Now::L>(*b);compare(*ca,*cb,42);
 for(unsigned i=0;i<160;++i){ca->ForgetList(rec<Old>(i).list);cb->ForgetList(rec<Now>(i).list);}
 compare(*ca,*cb,9);compare(*a,*b,9);
 check(!ca->AnyPending()&&!cb->AnyPending(),"copy-owned metadata/payload clear");
 *ca=*a;*cb=*b;compare(*ca,*cb,17);
 std::puts("PASS full 64-slot/48-event overflow, invalid/failed/replayed submission, 12000 transitions, stale payload isolation and object copy");
}
static int budget(){
 auto b=std::make_unique<Now::L>();
 for(unsigned i=0;i<64;++i){typename Now::Label v{};b->Set(rec<Now>(i),resource<Now>(i),v);b->Submit(rec<Now>(i),1,true);}
 nr_source_frame::perf9_pending_probes=0;
 for(unsigned n=0;n<1000;++n)check(!b->AnyPending(),"empty submitted summary");
 const auto tests=nr_source_frame::perf9_pending_probes;
 const auto stride=b->Perf9BatchStrideForTest();
 std::printf("pending scan predicates=%llu/1000 queries; hot Batch bytes=%llu\n",static_cast<unsigned long long>(tests),static_cast<unsigned long long>(stride));
 return tests==0&&stride<=64?0:3;
}
template<class T> double timed(unsigned& checksum){auto l=std::make_unique<typename T::L>();
 typename T::Label value{};value.source={1,1,1};value.role=T::Role::Application;
 for(unsigned i=0;i<64;++i){l->Set(rec<T>(i),resource<T>(i),value);l->Submit(rec<T>(i),1,true);}
 auto start=std::chrono::steady_clock::now();
 for(unsigned n=0;n<15000;++n){unsigned i=n%64;l->Reset(rec<T>(i));
  checksum+=l->Set(rec<T>(i),resource<T>(i),value);checksum+=l->AnyPending();
  checksum+=l->Submit(rec<T>(i),1,true);checksum+=l->AnyPending();
  checksum+=l->TracksList(rec<T>(i).list,rec<T>(i).lifetime);}
 return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
int main(int argc,char**argv){std::string mode=argc>1?argv[1]:"";if(mode=="--budget")return budget();if(mode=="--bench"){unsigned a=0,b=0;auto x=timed<Old>(a),y=timed<Now>(b);check(a==b,"timed semantic checksum");std::printf("15000 reset/set/pending/submit/membership transactions: old_ms=%.3f new_ms=%.3f checksum=%u\n",x,y,b);return 0;}semantics();return 0;}
