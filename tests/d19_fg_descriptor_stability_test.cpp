#include "nr_fg_presentation.h"
#include "nr_descriptor_scratch.h"
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>
#include <iostream>
using nr_fg_presentation::Policy;
using nr_fg_presentation::Reason;
int main(){
    unsigned cases=0;
    {Policy p;assert(!p.Read().late_owned);++cases;
     p.Inventory(0,1);assert(!p.Read().late_owned);++cases;
     p.Inventory(1,1);assert(p.Read().reason==Reason::LiveHandleModeUnknown&&p.Read().inline_reconstruction);++cases;
     // Successful Create without Evaluate is explicitly UNKNOWN, not active.
     assert(p.Options(0,0,true));assert(!p.Read().late_owned&&p.Read().handles==1);++cases;
     assert(!p.Options(0,1,false));assert(!p.Read().late_owned);++cases;
     for(unsigned mode=1;mode<=3;++mode){assert(p.Options(0,mode,true));assert(p.Read().reason==Reason::OptionsEnabled);++cases;}
     p.Options(0,0,true);{nr_fg_presentation::Creating c(p,true);assert(p.Read().reason==Reason::Creating);++cases;}
     assert(p.Read().reason==Reason::LiveHandleModeUnknown);++cases;
     p.Options(1,1,true);p.Options(0,0,true);assert(p.Read().late_owned);++cases;
     p.Options(1,0,true);assert(!p.Read().late_owned);++cases;
     p.Options(0,1,true);p.Inventory(2,1);p.Inventory(1,1);assert(p.Read().late_owned);++cases;
     p.Inventory(0,0);assert(!p.Read().late_owned&&p.Read().options==0);++cases;
     // A successful Off from a retired lifetime cannot authorize Present for
     // a newly created, mode-unobserved owner.
     p.Inventory(1,1);p.Options(0,0,true);p.Inventory(0,0);
     const auto retired=p.Lifecycle();
     {nr_fg_presentation::Creating c(p,true);p.Inventory(1,1);assert(p.Read().reason==Reason::Creating);
      assert(!p.Options(0,0,true,retired));}
     assert(p.Read().reason==Reason::LiveHandleModeUnknown);++cases;
     p.Inventory(0,0);p.Options(0,1,true);assert(p.Read().late_owned&&!p.Read().inline_reconstruction);++cases;
    }
    {Policy p;p.Inventory(1,1);std::vector<std::thread> ts;
     for(unsigned k=0;k<4;++k)ts.emplace_back([&,k]{for(unsigned i=0;i<1000;++i){p.Options(k,i%4,true);(void)p.Read();}});
     for(auto& t:ts){t.join();}
     for(unsigned k=0;k<4;++k){p.Options(k,0,true);}assert(!p.Read().late_owned);++cases;}
    using Buffer=nr_descriptor::ScratchBuffer<std::uint64_t,256>;
    std::uint64_t* first=nullptr;
    {Buffer a;assert(a.Acquire(256));first=a.get();a[0]=123;
     {Buffer b;assert(b.Acquire(256));assert(a.get()!=b.get());b[0]=456;
      {Buffer c;assert(c.Acquire(256));assert(c.get()!=a.get()&&c.get()!=b.get());c[0]=789;
       assert(a[0]==123&&b[0]==456);++cases;}}
     assert(!a.Acquire(1));++cases;}
    {Buffer a;assert(a.Acquire(256));assert(a.get()==first);++cases;}
    {Buffer a;assert(!a.Acquire(0));assert(a.Acquire(257));a[256]=42;assert(a[256]==42);++cases;}
    std::atomic<bool> running{false};std::uint64_t* foreign=nullptr;
    {Buffer a;assert(a.Acquire(256));
     std::thread t([&]{Buffer b;assert(b.Acquire(256));foreign=b.get();running.store(true,std::memory_order_release);
        while(running.load(std::memory_order_acquire))std::this_thread::yield();});
     while(!running.load(std::memory_order_acquire))std::this_thread::yield();
     assert(foreign!=a.get());running.store(false,std::memory_order_release);t.join();++cases;}
    std::cout<<"policy/scratch scenarios="<<cases<<"\n";
}
