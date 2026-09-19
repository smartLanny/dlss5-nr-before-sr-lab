#include "nr_edit_chain_history.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <iostream>
namespace nr_full{constexpr unsigned kMaxPasses=5;}
struct NrContext;
struct NrSlot{NrSlot* dual_selected=nullptr;NrContext* dual_owner=nullptr;};
struct NrContext{
 bool retired=false,failed=false;
 std::uint64_t instance_id=0,history_generation=0,parent_instance_id=0;
 unsigned width=0,height=0,layer_index=0;
 void* nr_handle=nullptr;bool diagnostic_reset_requested=false;
};
struct NrPrivateImage{NrContext* context;NrSlot* slot;bool ready;};
struct Config{unsigned nr_passes=2,nr_full_passes=5;bool nr_full_reference=true;}g_config;
std::atomic<unsigned>g_config_generation{7};std::array<NrContext,8>g_contexts;
namespace nr_dual_runtime{
 unsigned full_generation=7;std::uint64_t full_root=100;
 std::atomic<unsigned>full_adopted{5};std::atomic<std::uint64_t>full_instance[5];
}
namespace nr_edit_core{
 using Id=std::uint64_t;
 #include "nr_edit_d21_chain.inl"
}
int main(){using namespace nr_edit_chain;using namespace nr_edit_core;
 NrSlot main,child;
 for(unsigned i=0;i<5;++i){g_contexts[i]={false,false,100+i,10+i,i?100u:0u,1920,1080,i,reinterpret_cast<void*>(1000+i),false};
  nr_dual_runtime::full_instance[i].store(100+i);}
 main.dual_selected=&child;child.dual_owner=&g_contexts[4];NrPrivateImage image{&g_contexts[0],&main,true};AppliedChain chain;
 assert(ReadAppliedChain(image,chain)&&chain.used==5&&chain.requested==5);
 EditHistory history;assert(history.Check(chain,{1,1,1},{},false).reason==Reason::Cold);
 assert(history.Commit(chain,{1,1,1}));assert(!history.Check(chain,{1,1,2},{1,1,1},false).reset);
 assert(!history.Check(chain,{1,1,1},{},true).admitted);
 // Middle layer changes with unchanged primary AND final layer.
 g_contexts[2].instance_id=900;nr_dual_runtime::full_instance[2].store(900);
 AppliedChain changed;assert(ReadAppliedChain(image,changed));
 assert(history.Check(changed,{1,1,2},{1,1,1},false).reason==Reason::AppliedChainChanged);
 assert(history.Commit(changed,{1,1,2}));g_contexts[2].history_generation++;
 assert(ReadAppliedChain(image,changed));assert(history.Check(changed,{1,1,3},{1,1,2},false).reset);
 assert(history.Commit(changed,{1,1,3}));g_contexts[3].diagnostic_reset_requested=true;
 assert(ReadAppliedChain(image,changed));assert(history.Check(changed,{1,1,4},{1,1,3},false).reason==Reason::NativeReset);
 assert(history.Commit(changed,{1,1,4}));g_contexts[3].diagnostic_reset_requested=false;
 assert(ReadAppliedChain(image,changed));assert(!history.Check(changed,{1,1,5},{1,1,4},false).reset);
 assert(history.Check(changed,{1,1,8},{1,1,7},false).reason==Reason::SourceReference);
 assert(history.Check(changed,{1,2,8},{1,1,4},false).reason==Reason::SourceReference);
 main.dual_selected=nullptr;AppliedChain one;assert(ReadAppliedChain(image,one)&&one.used==1&&one.requested==5);
 assert(history.Check(one,{1,1,5},{1,1,4},false).reason==Reason::AppliedChainChanged);
 assert(history.Commit(one,{1,1,5}));main.dual_selected=&child;
 assert(ReadAppliedChain(image,changed));assert(history.Check(changed,{1,1,6},{1,1,5},false).reset);
 nr_dual_runtime::full_adopted.store(4);assert(!ReadAppliedChain(image,changed));nr_dual_runtime::full_adopted.store(5);
 nr_dual_runtime::full_root=99;assert(!ReadAppliedChain(image,changed));nr_dual_runtime::full_root=100;
 g_contexts[3].retired=true;assert(!ReadAppliedChain(image,changed));g_contexts[3].retired=false;
 g_contexts[3].failed=true;assert(!ReadAppliedChain(image,changed));g_contexts[3].failed=false;
 g_contexts[3].parent_instance_id=99;assert(!ReadAppliedChain(image,changed));g_contexts[3].parent_instance_id=100;
 assert(ReadAppliedChain(image,changed));history.Invalidate();assert(history.Check(changed,{1,1,6},{1,1,5},false).reason==Reason::Cold);
 assert(history.Commit(changed,{1,1,6}));NrSlot other;other.dual_owner=&g_contexts[4];main.dual_selected=&other;
 assert(ReadAppliedChain(image,chain));assert(!history.Check(chain,{1,1,7},{1,1,6},false).reset);
 g_config.nr_full_reference=false;child.dual_owner=&g_contexts[1];main.dual_selected=&child;
 assert(ReadAppliedChain(image,changed)&&changed.used==2);auto same=changed;same.layers[4]={999,9,9,9,true};assert(same==changed);
 same.layers[1].instance=same.layers[0].instance;assert(!same.Valid());same=changed;same.used=99;assert(!(same==same));
 g_config.nr_passes=1;main.dual_selected=nullptr;assert(ReadAppliedChain(image,changed)&&changed.used==1&&changed.requested==1);
 assert(!history.Check(changed,{},{},false).admitted);
 std::cout<<"D21 chain identity/reset tests passed; Core interface fixtures, not a GPU/model test.\n";
}
