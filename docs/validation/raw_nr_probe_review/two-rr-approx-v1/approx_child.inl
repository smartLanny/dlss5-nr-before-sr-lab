namespace raw_nr_probe {
    void ApproxChild::Prepare(){
        create_params=Allocate("scene-create-params");eval_params=Allocate("scene-eval-params");
        ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;Guard("scene-create-config");
        for(auto kv:{std::pair{"Width",scene_width},{"Height",scene_height},{"OutWidth",scene_width},{"OutHeight",scene_height},
            {"CreationNodeMask",1u},{"VisibilityNodeMask",1u},{"DLSS.Roughness.Mode",0u},{"DLSS.Use.HW.Depth",1u}})SetUInt(create_params,kv.first,kv.second);
        for(auto kv:{std::pair{"PerfQualityValue",5},{"DLSS.Feature.Create.Flags",75},{"DLSS.Denoise.Mode",1},
            {"DLSS.Enable.Output.Subrects",0},{"RayReconstruction.Hint.Render.Preset.DLAA",0}})SetInt(create_params,kv.first,kv.second);
        for(const char* key:{"Width","Height","OutWidth","OutHeight","CreationNodeMask","VisibilityNodeMask","DLSS.Roughness.Mode","DLSS.Use.HW.Depth"}){
            unsigned value=0;if(UI(create_params,key,value)!=kNgxSuccess)Stop("create-bind",key,"getter-not-proven");
            const unsigned expected=std::strcmp(key,"DLSS.Roughness.Mode")==0?0:std::strstr(key,"Mask")||std::strcmp(key,"DLSS.Use.HW.Depth")==0?1:
                std::strstr(key,"Width")?scene_width:scene_height;
            Check(value==expected,"actual Scene geometry/roughness/depth create getters");
        }
        for(auto kv:{std::pair{"PerfQualityValue",5},{"DLSS.Feature.Create.Flags",75},{"DLSS.Denoise.Mode",1},
            {"DLSS.Enable.Output.Subrects",0},{"RayReconstruction.Hint.Render.Preset.DLAA",0}}){int value=0;
            Check(I(create_params,kv.first,value)==kNgxSuccess&&value==kv.second,"actual fixed Scene create mode getters");}
    }
    NgxResult ApproxChild::Create(ID3D12GraphicsCommandList* list){
        ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;Guard("scene-create");
        NrPostBoundaryAliasScope alias(list);auto* call_list=NrPrivateOriginalCallList(list);
        if(call_list!=list)Stop("create","original-call-list","no-current-original-alias");
        ++child_creates;++scene_create_attempts;std::printf("SCENE_CALL_BEGIN kind=Create feature=13 count=%u\n",scene_create_attempts);
        const auto r=exports.create(call_list,13,create_params,&child);
        std::printf("SCENE_CREATE index=%u feature=13 result=%08x child=%p parent=%p\n",child_creates,r,(void*)child,(void*)parent);
        return r;
    }
    NgxResult ApproxChild::Evaluate(ID3D12GraphicsCommandList* list,unsigned frame){
        ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;Guard("scene-evaluate");
        NrPostBoundaryAliasScope alias(list);auto* call_list=NrPrivateOriginalCallList(list);
        if(call_list!=list)Stop("evaluate","original-call-list","no-current-original-alias");
        Check(child&&child!=parent&&FindFeature(child).generation==0,"private Scene child is separate from native game registry");
        ++child_evaluates;++scene_eval_attempts;std::printf("SCENE_CALL_BEGIN kind=Eval feature=13 count=%u\n",scene_eval_attempts);
        const auto r=exports.evaluate(call_list,child,eval_params,nullptr);
        std::printf("SCENE_EVAL index=%u call=%u feature=13 result=%08x\n",frame,child_evaluates,r);return r;
    }
    void ApproxChild::ReleaseChild(){
        // Caller has no chain lock and already proved every lease retired.
        ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::Scope tls;Guard("scene-release");
        const auto r=exports.release(child);std::printf("SCENE_RELEASE result=%08x outsideChain=1\n",r);
        if(r!=kNgxSuccess)Stop("release","Feature13","release-error",r);child=nullptr;
        for(auto** p:{&eval_params,&create_params,&capability})if(*p){const auto d=exports.destroy(*p);
            std::printf("DESTROY_PARAMS result=%08x\n",d);if(d!=kNgxSuccess)Stop("destroy","parameters","destroy-error",d);*p=nullptr;}
        FreeLibrary(callback_owner);callback_owner=nullptr;FreeLibrary(capability_owner);capability_owner=nullptr;nr_private_sr_core::ReleaseExports(exports);
    }
}
