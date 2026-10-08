// CPU-only source-derived controls. D3D12 types/constants only; no device API.
#include <d3d12.h>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include "<PRIVATE_PATH>"

struct FakeResource{D3D12_RESOURCE_STATES actual;bool observed=true;};
struct Event{unsigned before,after;};struct FakeList{std::vector<Event> events;unsigned invalid=0;};
namespace nr_source_runtime {
struct Lock{};FakeList* RecordingOf(FakeList* l){return l;}FakeResource* ResourceOf(FakeResource* r){return r;}
bool StateOf(FakeList*,FakeResource* r,unsigned& state){if(!r->observed)return false;state=r->actual;return true;}
}
void Barrier(FakeList* l,FakeResource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
 l->events.push_back({unsigned(before),unsigned(after)});if(before==after||before!=r->actual)++l->invalid;r->actual=after;
}
struct OldResources{
 static void Move(FakeList* list,FakeResource* resource,D3D12_RESOURCE_STATES& state,D3D12_RESOURCE_STATES wanted){
  {nr_source_runtime::Lock lock;unsigned actual=0;if(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list),nr_source_runtime::ResourceOf(resource),actual))state=static_cast<D3D12_RESOURCE_STATES>(actual);}
  Barrier(list,resource,state,wanted);state=wanted;
 }
};
struct NewResources{
 static void Move(FakeList* list,FakeResource* resource,D3D12_RESOURCE_STATES& state,D3D12_RESOURCE_STATES wanted){
  {nr_source_runtime::Lock lock;unsigned actual=0;if(nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list),nr_source_runtime::ResourceOf(resource),actual))state=static_cast<D3D12_RESOURCE_STATES>(actual);}
  if(state!=wanted)Barrier(list,resource,state,wanted);state=wanted;
 }
};
constexpr unsigned kCodecSrvCount=13,kCodecUavCount=8,kCodecDescriptorCount=21;
void InitializeNew(nr_binding::DescriptorSet& table){
  static_assert(kCodecDescriptorCount==21);
  for(unsigned i=0;i<kCodecSrvCount;++i)table.Srv(i,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
  for(unsigned i=0;i<kCodecUavCount;++i)table.Uav(kCodecSrvCount+i,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);

}
nr_binding::DescriptorSet OldLayout(){nr_binding::DescriptorSet table;
  table.Srv(0,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);table.Srv(1,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);table.Srv(2,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);table.Uav(kCodecSrvCount,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
return table;}
nr_binding::DescriptorSet NewLayout(){nr_binding::DescriptorSet table;InitializeNew(table);
  table.Srv(0,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);table.Srv(1,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);table.Srv(2,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);table.Uav(kCodecSrvCount,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
return table;}
void Check(bool v,const char* name){if(!v){std::printf("FAIL %s GPU=0 NGX=0\n",name);std::exit(1);}std::printf("PASS %s GPU=0 NGX=0\n",name);}
int main(){
 auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;auto copy=D3D12_RESOURCE_STATE_COPY_SOURCE;auto uav=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
 FakeResource a{uav},b{uav};D3D12_RESOURCE_STATES old_state=uav,new_state=uav;FakeList before,after;
 for(auto wanted:{read,copy,read,read}){OldResources::Move(&before,&a,old_state,wanted);NewResources::Move(&after,&b,new_state,wanted);}
 Check(before.events.size()==4&&before.invalid==1&&before.events.back().before==64&&before.events.back().after==64,"v1-frame0-warmup-compose-records-illegal-64-to-64");
 Check(after.events.size()==3&&after.invalid==0&&b.actual==read,"v2-same-frame-path-retains-required-transitions-without-noop");
 FakeResource stale{read};D3D12_RESOURCE_STATES cache=uav;FakeList observed;NewResources::Move(&observed,&stale,cache,read);
 Check(observed.events.empty()&&cache==read,"v2-real-observer-wins-over-stale-cache-before-comparison");
 FakeResource real{uav};cache=read;FakeList changed;NewResources::Move(&changed,&real,cache,read);
 Check(changed.events.size()==1&&changed.events[0].before==8&&changed.events[0].after==64&&changed.invalid==0,"v2-required-transition-uses-actual-before-state");
 FakeResource fallback{read,false};cache=read;FakeList completed;NewResources::Move(&completed,&fallback,cache,read);
 Check(completed.events.empty()&&cache==read,"v2-completed-observed-cache-can-already-match-read-state");
 auto old=OldLayout(),now=NewLayout();unsigned holes=0;for(unsigned i=0;i<old.count;++i)holes+=old.views[i].kind==nr_binding::ViewKind::Empty;
 Check(old.count==14&&old.count<kCodecDescriptorCount&&holes==10,"v1-actual-DescriptorSet-has-14-slots-ten-holes-for-21-slot-root");
 bool typed=now.count==21;for(unsigned i=0;i<now.count;++i)typed&=now.views[i].kind==(i<13?nr_binding::ViewKind::Srv2D:nr_binding::ViewKind::Uav2D)&&now.views[i].format==DXGI_FORMAT_R16G16B16A16_FLOAT;
 Check(typed,"v2-actual-source-initializer-provides-all-21-typed-null-slots");
 std::puts("PASS CPU_RECORDING_CONTRACT tests=7 device=0 ResourceBarrierAPI=0 NGX=0 GPU=0 oldRuntimeInvalidExpected=1 v2RuntimeUnverified=1");return 0;
}
