// Executes the production full-chain adapter with a synthetic Core surface.
// NGX, command lists and captures are explicit substitutes, not GPU evidence.
#include "../src/nr_full_pass.hpp"
#include <atomic>
#include <vector>
#include <iostream>
#include <stdexcept>
using NgxResult=unsigned;
struct ID3D12Device{};struct ID3D12GraphicsCommandList{};struct ID3D12Resource{};struct NgxParams{};
struct FeatureRecord{};struct ColorContract{};struct NrGuideBinding{};
enum class CodecColorMode{Automatic};
struct NrBindingPacket{};
struct NrContext {
    unsigned long long instance_id=0,parent_instance_id=0;
    unsigned layer_index=0,width=1920,height=1080,evaluations=10,successful_evaluations_since_reset=10,mode=2;
    void* params=nullptr;void* nr_handle=nullptr;bool retired=false,lifecycle_dependencies_quarantined=false;
};
struct NrSlot {
    int value=0;void* dual_spatial=nullptr;NrSlot* dual_selected=nullptr;NrBindingPacket* dual_binding=nullptr;
    bool dual_reduced=false,region_ready=false,dual_time_pending=false,dual_time_stable=false;
    unsigned dual_low_width=0,dual_low_height=0,dual_model_domain=0;
    unsigned region_constants[20]{};
};
struct Config {bool nr_full_reference=false;unsigned nr_full_passes=2,nr_passes=1,nr_second_numerator=1,nr_second_denominator=1,warmup_after_create=1;}g_config;
std::atomic<unsigned> g_config_generation{1};int g_chain_cs=0;
bool lock_available=true;
bool TryEnterCriticalSection(int*){return lock_available;}void LeaveCriticalSection(int*){}
NrContext g_contexts[5]{};NrSlot slots[4]{};NrBindingPacket packets[4]{};
unsigned checks=0,prepared_calls=0,run_calls=0,captures=0,stop_prepare=99,stop_run=99;
bool opaque=false,alias=false,poison=false,legacy=false,refused=false;
void Check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
void MarkLifecycleUncertain(const char*){poison=true;}
int StageCaptureStatus(){return 0;}
namespace nr_f8 {bool IsActive(){return false;}}
namespace nr_replay {bool Active(){return false;}}
namespace nr_dual::dx12 {struct Surfaces{nr_dual::ModelDomain domain{};void* low_delta=nullptr;};Surfaces GetSurfaces(void*){return {};}}
namespace nr_full_capture {
void Begin(ID3D12GraphicsCommandList*,NrContext&,NrSlot&,NrContext&,NrSlot&,NrSlot&,const NrGuideBinding&,unsigned,unsigned){++captures;}
}
namespace nr_dual_status_runtime {void RecordObservation(NrContext&,NrContext*,bool,bool,bool,bool){}}
namespace nr_dual_runtime {
struct FullTime {std::atomic<unsigned> samples{0},config{0};std::atomic<double> last{0},median{0},p95{0};}full_time[5]{};
enum class Status {Off,ResourceContract,Failed,Active};Status current_status{};
struct LayerPrepared {
    NrContext* context=nullptr;NrSlot* slot=nullptr;nr_dual::Plan plan{};NrGuideBinding guides{};
    bool create_completed=false;NrSlot* source=nullptr;NrBindingPacket* binding=nullptr;
    bool evaluated=false,adoptable=false,full=false;
    bool create_admission_refused=false;
    explicit operator bool()const{return context&&slot&&bool(plan);}
};
struct Prepared:LayerPrepared {std::array<LayerPrepared,4> layers{};unsigned requested=0,prepared=0;};
void SetStatus(Status s){current_status=s;}
void ClearSelection(NrSlot& s){s.dual_selected=nullptr;s.dual_binding=nullptr;}
LayerPrepared PrepareOne(ID3D12Device*,ID3D12GraphicsCommandList*,NrContext& primary,NrSlot& main,
    const NgxParams*,const FeatureRecord&,unsigned,unsigned,ID3D12Resource*,unsigned,unsigned,
    CodecColorMode,unsigned layer=1,NrSlot* input=nullptr,bool full=false,const nr_dual::dx12::Surfaces* =nullptr) {
    ++prepared_calls;legacy=!full;
    if(layer==stop_prepare)return {};
    auto& c=g_contexts[layer];c.instance_id=primary.instance_id+layer;c.parent_instance_id=primary.instance_id;
    c.layer_index=layer;c.params=alias?&g_contexts[1]:&g_contexts[layer];c.nr_handle=&slots[layer-1];
    auto p=nr_dual::MakePlan({2,{1,1}},{1920,1080},{});
    return {&c,&slots[layer-1],p,{},true,input?input:&main,nullptr,false,false,full};
}
void SealOne(ID3D12GraphicsCommandList*,NrSlot& main,LayerPrepared& p) {
    p.binding=&packets[p.context->layer_index-1];main.dual_binding=p.binding;
}
NgxResult RunOne(ID3D12GraphicsCommandList*,NrContext&,NrSlot&,LayerPrepared& p,const FeatureRecord&,const ColorContract&) {
    ++run_calls;const auto layer=p.context->layer_index;
    Check(p.binding!=nullptr,"all descriptors sealed before runtime");
    if(layer==stop_run){p.create_admission_refused=refused;return opaque?0xBAD00005u:0;}
    p.slot->value=p.source->value*2+int(layer);p.evaluated=p.adoptable=true;
    return 0;
}
}
#include "../src/nr_full_runtime.inl"
void Reset(){for(auto& c:g_contexts)c={};for(auto& s:slots)s={};prepared_calls=run_calls=captures=0;
    stop_prepare=stop_run=99;opaque=alias=poison=legacy=refused=false;g_config={};}
int main(){try {
    using namespace nr_dual_runtime;ID3D12Device device;ID3D12GraphicsCommandList list;
    FeatureRecord feature;ColorContract color;NgxParams params;ID3D12Resource resource;
    for(unsigned n=2;n<=5;++n) {
        Reset();g_config.nr_passes=2;g_config.nr_full_reference=true;g_config.nr_full_passes=n;
        NrContext root;root.instance_id=100;NrSlot main;main.value=10;
        auto p=Prepare(&device,&list,root,main,&params,feature,1920,1080,&resource,0,0,CodecColorMode::Automatic);
        Check(p.prepared==n-1&&p.requested==n,"production prepares requested independent layers");
        Seal(&list,main,p);Check(Run(&list,root,main,p,feature,color)==0,"complete run succeeds");
        int expected=10;for(unsigned i=1;i<n;++i)expected=expected*2+int(i);
        Check(main.dual_selected==&slots[n-2]&&main.dual_selected->value==expected,"actual producer feeds next layer");
        Check(main.dual_binding==&packets[n-2]&&!main.dual_reduced,"final consumer selects last direct result");
        Check(captures==n-1&&run_calls==n-1,"one capture and one call per layer");
        FullSnapshot snapshot{};Check(TrySnapshotFull(snapshot)&&snapshot.scope_matches&&snapshot.adopted==n,
            "current configuration snapshot reports actual adopted pass count");
        lock_available=false;Check(!TrySnapshotFull(snapshot),"feedback never waits for render lock");lock_available=true;
        for(unsigned failure=1;failure<n;++failure) {
            Reset();g_config.nr_passes=2;g_config.nr_full_reference=true;g_config.nr_full_passes=n;stop_run=failure;opaque=true;
            auto q=Prepare(&device,&list,root,main,&params,feature,1920,1080,&resource,0,0,CodecColorMode::Automatic);
            Seal(&list,main,q);Check(Run(&list,root,main,q,feature,color)==0xBAD00005u,"opaque failure returned to original caller");
            Check(run_calls==failure&&captures==0&&main.dual_selected==nullptr,"no later work or final adoption after opaque failure");
        }
        for(unsigned failure=1;failure<n;++failure) {
            Reset();g_config.nr_passes=2;g_config.nr_full_reference=true;g_config.nr_full_passes=n;stop_run=failure;refused=true;
            auto q=Prepare(&device,&list,root,main,&params,feature,1920,1080,&resource,0,0,CodecColorMode::Automatic);
            Seal(&list,main,q);Check(Run(&list,root,main,q,feature,color)==0&&!poison,"late network admission refusal is not an opaque failure");
            Check(q.create_admission_refused&&run_calls==failure&&captures==0&&main.dual_selected==nullptr,
                "full wrapper propagates each refused layer to the native consumer without partial adoption");
        }
    }
    Reset();g_config.nr_passes=2;g_config.nr_full_reference=true;g_config.nr_full_passes=4;stop_prepare=3;
    NrContext root;root.instance_id=100;NrSlot main;main.value=10;
    auto p=Prepare(&device,&list,root,main,&params,feature,1920,1080,&resource,0,0,CodecColorMode::Automatic);
    Seal(&list,main,p);Run(&list,root,main,p,feature,color);
    Check(!main.dual_selected&&captures==0&&run_calls==2,"incomplete preparation never advertises full chain");
    Reset();g_config.nr_passes=2;g_config.nr_full_reference=true;g_config.nr_full_passes=4;alias=true;
    auto bad=Prepare(&device,&list,root,main,&params,feature,1920,1080,&resource,0,0,CodecColorMode::Automatic);
    Check(!bad&&poison&&run_calls==0,"sibling parameter alias rejected before execution");
    Reset();auto old=Prepare(&device,&list,root,main,&params,feature,1920,1080,&resource,0,0,CodecColorMode::Automatic);
    Check(!old.full&&legacy&&prepared_calls==1,"disabled full mode retains original helper");
    Reset();g_config.nr_passes=2;stop_run=1;refused=true;NrSlot legacy_main;
    auto declined=Prepare(&device,&list,root,legacy_main,&params,feature,1920,1080,&resource,0,0,CodecColorMode::Automatic);
    Seal(&list,legacy_main,declined);
    Check(Run(&list,root,legacy_main,declined,feature,color)==0&&declined.create_admission_refused&&!legacy_main.dual_selected,
        "legacy two-layer wrapper also retains explicit admission refusal for its native consumer");
    std::cout<<"PASS "<<checks<<" production full-adapter checks; synthetic Core, not GPU/NGX\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
