// Private renderer-only legacy-state observations. No Core source-state writes,
// admission exception, second device proxy, hook, queue or submission ledger.
namespace scene_renderer_state {
using LegacyFn=decltype(ID3D12GraphicsCommandListVtbl::ResourceBarrier);
using EnhancedFn=decltype(ID3D12GraphicsCommandList7Vtbl::Barrier);
struct Entry {ID3D12Resource* resource=nullptr;unsigned state=0;bool known=false;};
std::array<Entry,8> entries{};
std::array<void*,4> legacy{},enhanced{},legacy_targets{},enhanced_targets{};
unsigned legacy_count=0,enhanced_count=0;bool installed=false,enhanced_absent=false;
SRWLOCK lock=SRWLOCK_INIT;ID3D12GraphicsCommandList* commands=nullptr;
unsigned long long token=0,reset=0,legacy_events=0;const char* refusal=nullptr;
void Refuse(const char* reason){if(!refusal)refusal=reason;for(auto& e:entries)e.known=false;}
void Observe(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
 // Published native targets are process-wide. Unrelated NGX internal lists
 // and resources are outside this single renderer pool, not false failures.
 if(list!=commands){
  if(count>256||(count&&!barriers)){AcquireSRWLockExclusive(&lock);Refuse("renderer-other-list-unknown-legacy-batch");ReleaseSRWLockExclusive(&lock);return;}
  bool touches=false;for(unsigned i=0;i<count;++i)for(const auto& e:entries)if(e.resource){
   const auto& b=barriers[i];touches|=b.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION&&b.Transition.pResource==e.resource;
   touches|=b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING&&(b.Aliasing.pResourceBefore==e.resource||b.Aliasing.pResourceAfter==e.resource);
  }
  if(touches){AcquireSRWLockExclusive(&lock);Refuse("renderer-owned-resource-on-other-list");ReleaseSRWLockExclusive(&lock);}return;
 }
 const auto id=SnapshotPostBoundaryList(list);
 AcquireSRWLockExclusive(&lock);
 if(list!=commands||!id.known||id.token!=token||id.reset!=reset){Refuse("renderer-observation-recording-mismatch");ReleaseSRWLockExclusive(&lock);return;}
 if((count&&!barriers)||count>256){Refuse("renderer-unbounded-barrier-batch");ReleaseSRWLockExclusive(&lock);return;}
 for(unsigned i=0;i<count;++i){const auto& b=barriers[i];
  if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING){bool touches=!b.Aliasing.pResourceBefore&&!b.Aliasing.pResourceAfter;for(const auto& e:entries)touches|=e.resource&&(b.Aliasing.pResourceBefore==e.resource||b.Aliasing.pResourceAfter==e.resource);if(touches)Refuse("renderer-aliasing-unsupported");continue;}
  if(b.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)continue;
  for(auto& e:entries)if(e.resource&&e.resource==b.Transition.pResource){
   if(b.Flags!=D3D12_RESOURCE_BARRIER_FLAG_NONE||b.Transition.Subresource!=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES){Refuse("renderer-split-or-subresource-transition-unsupported");break;}
   if(!e.known||e.state!=unsigned(b.Transition.StateBefore)){Refuse("renderer-observed-StateBefore-contradiction");break;}
   e.state=unsigned(b.Transition.StateAfter);++legacy_events;break;
  }
 }
 ReleaseSRWLockExclusive(&lock);
}
template<unsigned I> void STDMETHODCALLTYPE Legacy(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
 reinterpret_cast<LegacyFn>(legacy[I])(list,count,barriers);Observe(list,count,barriers);
}
template<unsigned I> void STDMETHODCALLTYPE Enhanced(ID3D12GraphicsCommandList7* list,UINT count,const D3D12_BARRIER_GROUP* groups){
 reinterpret_cast<EnhancedFn>(enhanced[I])(list,count,groups);
 bool touches=reinterpret_cast<void*>(list)==reinterpret_cast<void*>(commands);
 bool unknown=count>64||(count&&!groups);
 if(!touches&&!unknown)for(unsigned i=0;i<count;++i){const auto& group=groups[i];
  if(group.NumBarriers>256||(group.NumBarriers&&!group.pGlobalBarriers)){unknown=true;break;}
  if(group.Type==D3D12_BARRIER_TYPE_TEXTURE)for(unsigned j=0;j<group.NumBarriers;++j)for(const auto& e:entries)touches|=e.resource&&e.resource==group.pTextureBarriers[j].pResource;
  else if(group.Type!=D3D12_BARRIER_TYPE_GLOBAL&&group.Type!=D3D12_BARRIER_TYPE_BUFFER){unknown=true;break;}
 }
 if(unknown){AcquireSRWLockExclusive(&lock);Refuse("renderer-other-list-unknown-enhanced-batch");ReleaseSRWLockExclusive(&lock);return;}
 if(touches){AcquireSRWLockExclusive(&lock);Refuse("renderer-enhanced-barrier-unsupported");ReleaseSRWLockExclusive(&lock);}
}
void* const legacy_thunks[4]={reinterpret_cast<void*>(&Legacy<0>),reinterpret_cast<void*>(&Legacy<1>),reinterpret_cast<void*>(&Legacy<2>),reinterpret_cast<void*>(&Legacy<3>)};
void* const enhanced_thunks[4]={reinterpret_cast<void*>(&Enhanced<0>),reinterpret_cast<void*>(&Enhanced<1>),reinterpret_cast<void*>(&Enhanced<2>),reinterpret_cast<void*>(&Enhanced<3>)};
void Guard(){
 using queue_test::Check;
 Check(installed&&g_post_hook_ResourceBarrier.count==legacy_count&&g_post_hook_Barrier.count==enhanced_count,"renderer observer fixed published hook-family coverage");
 for(unsigned i=0;i<legacy_count;++i)Check(g_post_hook_ResourceBarrier.targets[i]==legacy_targets[i]&&g_post_hook_ResourceBarrier.originals[i]==legacy_thunks[i]&&g_post_hook_ResourceBarrier.enabled[i],"renderer legacy observer still chained once to actual trampoline");
 for(unsigned i=0;i<enhanced_count;++i)Check(g_post_hook_Barrier.targets[i]==enhanced_targets[i]&&g_post_hook_Barrier.originals[i]==enhanced_thunks[i]&&g_post_hook_Barrier.enabled[i],"renderer enhanced invalidation observer remains chained");
 if(refusal){std::printf("UNSUPPORTED renderer_state=%s nativeToken=%llu reset=%llu\n",refusal,token,reset);probe_terminal::Exit(42,"renderer-state-unsupported");}
}
void Install(ID3D12GraphicsCommandList* list){
 using queue_test::Check;Check(!installed&&list,"one renderer observer installation before any model call");
 commands=list;const auto id=SnapshotPostBoundaryList(list);Check(id.known&&id.token,"renderer observer actual native lifetime/reset");token=id.token;reset=id.reset;
 ID3D12GraphicsCommandList7* extended=nullptr;
 const auto query=ID3D12GraphicsCommandList_QueryInterface(list,IID_ID3D12GraphicsCommandList7,reinterpret_cast<void**>(&extended));
 enhanced_absent=query==E_NOINTERFACE;Check(enhanced_absent||(SUCCEEDED(query)&&extended),"actual enhanced interface capability receipt");ReleaseCom(extended);
 legacy_count=g_post_hook_ResourceBarrier.count;enhanced_count=g_post_hook_Barrier.count;
 Check(legacy_count&&legacy_count<=4&&(enhanced_absent||enhanced_count)&&enhanced_count<=4,"published legacy plus enhanced invalidation or real E_NOINTERFACE");
 for(unsigned i=0;i<legacy_count;++i){Check(g_post_hook_ResourceBarrier.enabled[i]&&g_post_hook_ResourceBarrier.originals[i],"real published legacy original");legacy[i]=g_post_hook_ResourceBarrier.originals[i];legacy_targets[i]=g_post_hook_ResourceBarrier.targets[i];g_post_hook_ResourceBarrier.originals[i]=legacy_thunks[i];}
 for(unsigned i=0;i<enhanced_count;++i){Check(g_post_hook_Barrier.enabled[i]&&g_post_hook_Barrier.originals[i],"real published enhanced original");enhanced[i]=g_post_hook_Barrier.originals[i];enhanced_targets[i]=g_post_hook_Barrier.targets[i];g_post_hook_Barrier.originals[i]=enhanced_thunks[i];}
 installed=true;Guard();std::printf("RENDERER_OBSERVER legacySlots=%u enhancedSlots=%u enhancedAbsent=%u token=%llu reset=%llu sourceStateWrites=0 additionalHooks=0\n",legacy_count,enhanced_count,enhanced_absent,token,reset);
}
void Add(ID3D12Resource* resource){
 Guard();for(auto& e:entries)if(!e.resource){e={resource,unsigned(D3D12_RESOURCE_STATE_UNORDERED_ACCESS),true};return;}
 queue_test::Check(false,"renderer eight-plane single pool capacity");
}
unsigned State(ID3D12GraphicsCommandList* list,ID3D12Resource* resource){
 Guard();const auto id=SnapshotPostBoundaryList(list);queue_test::Check(list==commands&&id.known&&id.token==token&&id.reset==reset,"renderer State exact native recording/reset");
 AcquireSRWLockShared(&lock);bool known=false;unsigned state=0;for(const auto& e:entries)if(e.resource==resource){known=e.known;state=e.state;break;}ReleaseSRWLockShared(&lock);
 queue_test::Check(known,"renderer state from successful resource creation or actual legacy callback");return state;
}
void AfterExactReset(ID3D12GraphicsCommandList* list){
 Guard();const auto id=SnapshotPostBoundaryList(list);queue_test::Check(list==commands&&id.known&&id.token==token&&id.reset==reset+1,"renderer carries state only across caller-proven exact completion and successful Reset");reset=id.reset;
}
void Drop(){
 Guard();for(unsigned i=0;i<legacy_count;++i)g_post_hook_ResourceBarrier.originals[i]=legacy[i];for(unsigned i=0;i<enhanced_count;++i)g_post_hook_Barrier.originals[i]=enhanced[i];
 std::printf("RENDERER_OBSERVER_RELEASE legacyEvents=%llu exactFlightsRetired=1 sourceStateWrites=0\n",legacy_events);installed=false;commands=nullptr;entries={};
}
}
