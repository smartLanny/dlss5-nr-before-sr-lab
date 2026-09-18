// Exact production normalizers; comparison includes scratch acquisition/sorting.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include "nr_descriptor_scratch.h"
namespace baseline {
using Id=std::uint64_t;struct UnknownRange{Id first=0,last=0;unsigned type=0;};
#include "old_normalize.inl"
}
namespace candidate {
using Id=std::uint64_t;using nr_descriptor::ScratchBuffer;struct UnknownRange{Id first=0,last=0;unsigned type=0;};
#include "current_normalize.inl"
}
static void check(bool v,const char* m){if(!v){std::fprintf(stderr,"FAIL %s\n",m);std::exit(1);}}
static void compare(std::vector<baseline::UnknownRange> a){
 std::vector<candidate::UnknownRange> b;for(auto r:a)b.push_back({r.first,r.last,r.type});
 unsigned x=static_cast<unsigned>(a.size()),y=x;
 bool ax=baseline::NormalizeUnknownRanges(a.data(),x),bx=candidate::NormalizeUnknownRanges(b.data(),y);
 check(ax==bx&&x==y,"normalization acceptance/count changed");
 for(unsigned i=0;i<x;++i)check(a[i].first==b[i].first&&a[i].last==b[i].last&&a[i].type==b[i].type,"exact byte union/type changed");
}
static void semantics(){
 std::mt19937_64 rng(0x8000277);
 for(unsigned trial=0;trial<300;++trial){unsigned count=trial%11==0?4096:trial%7==0?2048:trial%3==0?650:trial%259;
  std::vector<baseline::UnknownRange> a;
  for(unsigned i=0;i<count;++i){auto first=trial%4==0?rng()|1:0x100000+32*(rng()%2048);auto last=first;
   if(trial%5==0&&first<std::numeric_limits<std::uint64_t>::max()-65)last+=rng()%65;
   a.push_back({first,last,trial%3==0?static_cast<unsigned>(rng()%4):2u});}
  if(trial%19==0&&!a.empty())a.back().first=0;
  if(trial%23==0&&!a.empty()){a.back().first=5;a.back().last=4;}
  compare(a);
 }
 for(unsigned kind=0;kind<4;++kind){std::vector<baseline::UnknownRange>a;
  for(unsigned i=0;i<2048;++i){auto first=kind==0?1ull:kind==1?0xfffffffffffff000ull+i:0x100000ull+(kind==2?i:2047-i)*32;
   auto last=kind==0?2048-i:first;a.push_back({first,last,0});}
  compare(a);
 }
 // Nested native-copy scopes may occupy both cached arrays. Sorting must use
 // independent owned fallback storage and cannot mutate either live scope.
 candidate::ScratchBuffer<candidate::UnknownRange,2048> parent,child;
 check(parent.Acquire(2048)&&child.Acquire(2048),"scratch acquisition");
 for(unsigned i=0;i<2048;++i){parent[i]={100,200,7};child[i]={300,400,9};}
 std::vector<baseline::UnknownRange> a;for(unsigned i=0;i<650;++i)a.push_back({10000+(650-i)*32,10000+(650-i)*32,2});compare(a);
 for(unsigned i=0;i<2048;++i)check(parent[i].first==100&&child[i].last==400,"nested scope metadata overwritten");
 unsigned n=0;check(candidate::NormalizeUnknownRanges(nullptr,n),"empty normalization");n=1;check(!candidate::NormalizeUnknownRanges(nullptr,n),"null nonempty admitted");
 std::puts("PASS 300 randomized byte unions, full-width/high-address, same-start, stride gaps, invalid input and nested scratch ownership");
}
static void bench(){std::mt19937_64 rng(2778);
 for(unsigned count:{64u,256u,650u,2048u,4096u})for(unsigned mode=0;mode<4;++mode){
  std::vector<baseline::UnknownRange>a;std::vector<candidate::UnknownRange>b;
  for(unsigned i=0;i<count;++i){auto first=mode==0?0x100000ull+i*32:mode==1?0x100000ull+(count-i)*32:mode==2?0x100000ull+(rng()%count)*32:rng()|1;
   a.push_back({first,first,0});b.push_back({first,first,0});}
  auto run=[](auto original){auto copy=original;unsigned long long checksum=0;auto start=std::chrono::steady_clock::now();
   for(unsigned r=0;r<500;++r){copy=original;unsigned n=static_cast<unsigned>(copy.size());
    if constexpr(std::is_same_v<typename decltype(copy)::value_type,baseline::UnknownRange>)baseline::NormalizeUnknownRanges(copy.data(),n);
    else candidate::NormalizeUnknownRanges(copy.data(),n);
    checksum+=n+copy[0].first;}
   std::printf(" sum=%llu",checksum);return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
  double old_ms=run(a),new_ms=run(b);std::printf(" BENCH ranges=%u mode=%u old_ms=%.4f new_ms=%.4f\n",count,mode,old_ms,new_ms);
 }
}
int main(int argc,char**argv){if(argc>1&&std::string(argv[1])=="--bench")bench();else semantics();return 0;}
