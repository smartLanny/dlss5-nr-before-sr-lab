// Kept behind the existing Prepare/Seal/Run surface. The consumer still owns
// exactly one final composition and the game still owns its original SR call.
namespace nr_dual_runtime {
std::atomic<unsigned> full_requested{0},full_prepared{0},full_adopted{0};
std::atomic<unsigned long long> full_instance[4]{},full_evaluations[4]{};
unsigned full_generation=0,full_mode=0;
unsigned long long full_root=0,full_recorded_adoptions=0;

Prepared Prepare(ID3D12Device* device,ID3D12GraphicsCommandList* list,
    NrContext& primary,NrSlot& main,const NgxParams* params,const FeatureRecord& feature,
    unsigned render_width,unsigned render_height,ID3D12Resource* original,
    unsigned x,unsigned y,CodecColorMode color_mode) {
    Prepared result{};
    const nr_full::Settings setting{g_config.nr_full_reference,g_config.nr_full_passes};
    const auto generation=g_config_generation.load(std::memory_order_relaxed);
    if(full_generation!=generation||full_root!=primary.instance_id) {
        full_generation=generation;full_root=primary.instance_id;full_recorded_adoptions=0;
        for(unsigned i=0;i<4;++i){full_instance[i].store(0);full_evaluations[i].store(0);}
    }
    full_mode=primary.mode;
    full_requested.store(setting.enabled?setting.passes:0);full_prepared.store(0);full_adopted.store(0);
    const unsigned keep=setting.enabled&&nr_full::Valid(setting)?setting.passes-1:
        (!setting.enabled&&g_config.nr_passes==2?1u:0u);
    for(auto& child:g_contexts)if(child.parent_instance_id==primary.instance_id&&child.layer_index>keep)
        child.retired=true;
    if(!setting.enabled) {
        static_cast<LayerPrepared&>(result)=PrepareOne(device,list,primary,main,params,feature,
            render_width,render_height,original,x,y,color_mode);
        return result;
    }
    ClearSelection(main);
    if(!nr_full::MakePlan(setting,{primary.width,primary.height})) {
        SetStatus(Status::ResourceContract);return result;
    }
    result.full=true;result.requested=setting.passes;
    NrSlot* source=&main;
    for(unsigned i=0;i<setting.passes-1;++i) {
        auto stage=PrepareOne(device,list,primary,main,params,feature,render_width,render_height,
            original,x,y,color_mode,i+1,source,true);
        if(!stage)break;
        // No two full-reference layers may share an NGX parameter block or
        // live feature, including siblings which have the same dimensions.
        bool alias=false;
        for(unsigned j=0;j<i;++j) {
            const auto* other=result.layers[j].context;
            alias|=stage.context==other||stage.context->params==other->params||
                (stage.context->nr_handle&&stage.context->nr_handle==other->nr_handle);
        }
        if(alias) {
            stage.context->lifecycle_dependencies_quarantined=true;
            primary.lifecycle_dependencies_quarantined=true;
            MarkLifecycleUncertain("Full-reference NR layers alias independent identity");
            SetStatus(Status::Failed);return {};
        }
        result.layers[i]=stage;++result.prepared;source=stage.slot;
    }
    full_prepared.store(result.prepared);
    if(result.prepared) {
        static_cast<LayerPrepared&>(result)=result.layers[0];
        result.full=true;
    }
    return result;
}

void Seal(ID3D12GraphicsCommandList* list,NrSlot& main,Prepared& prepared) {
    if(!prepared.full){SealOne(list,main,prepared);return;}
    for(unsigned i=0;i<prepared.prepared;++i) {
        SealOne(list,main,prepared.layers[i]);
        if(!prepared.layers[i]){prepared.prepared=i;break;}
    }
    full_prepared.store(prepared.prepared);
}

NgxResult Run(ID3D12GraphicsCommandList* list,NrContext& primary,NrSlot& main,
    Prepared& prepared,const FeatureRecord& feature,const ColorContract& color) {
    if(!prepared.full)return RunOne(list,primary,main,prepared,feature,color);
    full_instance[0].store(primary.instance_id);full_evaluations[0].store(primary.evaluations);
    struct Backend {
        ID3D12GraphicsCommandList* list;NrContext& primary;NrSlot& main;
        Prepared& prepared;const FeatureRecord& feature;const ColorContract& color;
        nr_full::Step Layer(unsigned i) {
            auto& stage=prepared.layers[i];
            const auto fatal=RunOne(list,primary,main,stage,feature,color);
            if(stage.context) {
                full_instance[i+1].store(stage.context->instance_id);
                full_evaluations[i+1].store(stage.context->evaluations);
            }
            return {fatal,stage.evaluated,stage.adoptable};
        }
        void Adopt(unsigned last) {
            auto& selected=prepared.layers[last];auto& aux=*selected.context;
            const auto surfaces=nr_dual::dx12::GetSurfaces(selected.slot->dual_spatial);
            main.dual_selected=selected.slot;main.dual_binding=selected.binding;
            main.dual_reduced=false;main.dual_low_width=aux.width;main.dual_low_height=aux.height;
            main.dual_model_domain=static_cast<unsigned>(surfaces.domain);
            if(main.region_ready) {
                main.region_constants[16]=aux.width;main.region_constants[17]=aux.height;
                main.region_constants[18]=0;main.region_constants[19]=main.dual_model_domain;
            }
            // Capture the already evaluated planes; never rerun a layer for F7.
            for(unsigned i=0;i<prepared.prepared;++i) {
                auto& stage=prepared.layers[i];
                stage.slot->dual_time_stable=stage.slot->dual_time_pending&&
                    StageCaptureStatus()==0&&!nr_f8::IsActive()&&!nr_replay::Active();
                nr_full_capture::Begin(list,primary,main,*stage.context,*stage.slot,
                    *stage.source,stage.guides,i,prepared.requested);
            }
            full_adopted.store(prepared.requested);++full_recorded_adoptions;SetStatus(Status::Active);
        }
    } backend{list,primary,main,prepared,feature,color};
    const auto result=nr_full::Run(prepared.requested,prepared.prepared,backend);
    if(!result.fatal)nr_dual_status_runtime::RecordObservation(primary,
        prepared.prepared?prepared.layers[0].context:nullptr,
        primary.successful_evaluations_since_reset>g_config.warmup_after_create,
        result.evaluated!=0,result.adopted,result.evaluated!=0&&!result.adopted);
    return result.fatal;
}

struct FullSnapshot {
    bool scope_matches=false,enabled=false;
    unsigned generation=0,mode=0,requested=0,prepared=0,adopted=0;
    unsigned long long root=0,recorded_adoptions=0;
    struct Layer {unsigned long long instance=0,evaluations=0;unsigned samples=0;
        bool timing_current=false;double last=0,median=0,p95=0;} layers[4]{};
};
bool TrySnapshotFull(FullSnapshot& out) {
    if(!TryEnterCriticalSection(&g_chain_cs))return false;
    out={};out.generation=g_config_generation.load(std::memory_order_relaxed);
    out.enabled=g_config.nr_full_reference;out.scope_matches=out.enabled&&full_generation==out.generation;
    out.mode=full_mode;out.requested=g_config.nr_full_passes;
    if(out.scope_matches) {
        out.root=full_root;out.prepared=full_prepared.load();out.adopted=full_adopted.load();
        out.recorded_adoptions=full_recorded_adoptions;
        for(unsigned i=0;i<4;++i) {
            auto& layer=out.layers[i];layer.instance=full_instance[i].load();layer.evaluations=full_evaluations[i].load();
            const auto& timing=full_time[i];layer.timing_current=timing.config.load()==out.generation;
            if(layer.timing_current){layer.samples=timing.samples.load();layer.last=timing.last.load();
                layer.median=timing.median.load();layer.p95=timing.p95.load();}
        }
    }
    LeaveCriticalSection(&g_chain_cs);return true;
}
}
