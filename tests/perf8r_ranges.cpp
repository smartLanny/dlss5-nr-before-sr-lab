#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include "nr_descriptor_scratch.h"
namespace reference {using Id=std::uint64_t;static std::uint64_t comparisons=0;
#include "ranges_reference.inl"
}
namespace current {using Id=std::uint64_t;using nr_descriptor::ScratchBuffer;static std::uint64_t comparisons=0;
#include "ranges_current.inl"
}
static void check(bool b,const char* m){if(!b){std::cerr<<m<<'\n';std::exit(2);}}
static void compare(const std::vector<current::UnknownRange>& data){
    auto b=data;std::vector<reference::UnknownRange> a;for(auto r:data)a.push_back({r.first,r.last,r.type});
    unsigned na=unsigned(a.size()),nb=unsigned(b.size());
    const bool va=reference::NormalizeUnknownRanges(a.data(),na),vb=current::NormalizeUnknownRanges(b.data(),nb);
    check(va==vb&&na==nb,"normalization result/count");
    for(unsigned i=0;i<na;++i)check(a[i].first==b[i].first&&a[i].last==b[i].last&&a[i].type==b[i].type,"exact sorted byte union/invalid input mutation");
}
static std::vector<current::UnknownRange> sample(unsigned mode){
    std::vector<current::UnknownRange> a;std::mt19937 rng(17);
    for(unsigned i=0;i<2048;++i){
        std::uint64_t first=1024+std::uint64_t(i)*64,last=first+15;
        if(mode==3){first=1024+(i/16)*256+(i%2)*4;last=first+31;}
        if(mode==4){first=1024+((i/8)%37)*256;last=first+1024;}
        a.push_back({first,last,0});
    }
    if(mode==1)std::reverse(a.begin(),a.end());
    if(mode==2)std::shuffle(a.begin(),a.end(),rng);
    return a;
}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="--budget"){
        auto a=sample(3);unsigned n=unsigned(a.size());current::comparisons=0;
        check(current::NormalizeUnknownRanges(a.data(),n),"overlap budget valid");
        std::cout<<"overlapping input comparator calls="<<current::comparisons<<" result="<<n<<'\n';
        return current::comparisons<5000?0:3;
    }
    if(argc>1&&std::string(argv[1])=="--bench"){
        for(unsigned mode=0;mode<5;++mode){auto input=sample(mode);compare(input);
            double times[2]{};std::uint64_t sums[2]{};
            for(unsigned pass=0;pass<2;++pass){auto start=std::chrono::steady_clock::now();
                if(pass==0){std::vector<reference::UnknownRange> original;for(auto r:input)original.push_back({r.first,r.last,r.type});auto a=original;for(unsigned k=0;k<2000;++k){a=original;unsigned n=unsigned(a.size());reference::NormalizeUnknownRanges(a.data(),n);sums[pass]+=n+a[0].last;}}
                else{auto a=input;for(unsigned k=0;k<2000;++k){a=input;unsigned n=unsigned(a.size());current::NormalizeUnknownRanges(a.data(),n);sums[pass]+=n+a[0].last;}}
                times[pass]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
            check(sums[0]==sums[1],"bench semantic checksum");std::cout<<"mode="<<mode<<" baseline_ms="<<times[0]<<" candidate_ms="<<times[1]<<'\n';}
        return 0;
    }
    std::mt19937 rng(20260919);
    for(unsigned pass=0;pass<3000;++pass){std::vector<current::UnknownRange> a;unsigned n=1+rng()%1024;
        for(unsigned i=0;i<n;++i){std::uint64_t first=1+std::uint64_t(rng()%65536),last=first+rng()%128;unsigned type=rng()%5;
            if(pass%7==0){first=UINT64_MAX-4096+(rng()%1024);last=first+rng()%1024;}
            if(pass%11==0&&i%3)last=first;
            a.push_back({first,last,type});}
        if(pass%17==0)a.back().first=0;
        if(pass%19==0)a.back()={700,600,1};
        compare(a);
    }
    for(unsigned mode=0;mode<5;++mode)compare(sample(mode));
    compare({{1,1,0},{3,3,0},{2,2,1},{UINT64_MAX,UINT64_MAX,2}});
    std::cout<<"3000 exact-union/invalid/reverse/mixed-type/high-address comparisons passed\n";
}
