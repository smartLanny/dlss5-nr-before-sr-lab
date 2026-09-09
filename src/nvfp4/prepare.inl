// Included in backend.cpp's private namespace. All allocation, asset parsing,
// driver discovery and descriptor building runs on the preparation worker.
uint64_t Number(const std::string& value) {
    size_t end = 0; auto n = std::stoull(value,&end,0);
    if (end != value.size() || value.empty() || value[0]=='-') throw std::runtime_error("Invalid locked manifest integer");
    return n;
}
void VerifyAssets(const fs::path& root) {
    for (const auto& entry : locked_assets::files) {
        auto b = Read(root / entry.path); uint8_t hash[32]{};
        if (b.size()!=entry.bytes || !Hash(b.data(),uint32_t(b.size()),hash) || std::memcmp(hash,entry.sha,32))
            throw std::runtime_error(std::string("Locked NVFP4 asset SHA-256 mismatch: ")+entry.path);
    }
}
bool ExactHexHash(const fs::path& path,const std::string& expected) {
    if(expected.size()!=64) return false;
    // Runtime DLLs can be substantially larger than individual CUDA assets.
    // Stream their identity check with bounded CPU storage.
    std::ifstream input(path,std::ios::binary); if(!input) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE state=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    auto status=BCryptCreateHash(algorithm,&state,nullptr,0,nullptr,0,0);
    std::array<uint8_t,65536> chunk{}; uint8_t hash[32]{};
    while(status>=0 && input) {
        input.read(reinterpret_cast<char*>(chunk.data()),chunk.size()); auto size=input.gcount();
        if(size) status=BCryptHashData(state,chunk.data(),ULONG(size),0);
    }
    if(!input.eof()) status=-1;
    if(status>=0) status=BCryptFinishHash(state,hash,32,0);
    if(state) BCryptDestroyHash(state); BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0) return false;
    for(size_t i=0;i<32;++i) {
        auto nibble=[](char c)->int { if(c>='0' && c<='9') return c-'0'; if(c>='a' && c<='f') return c-'a'+10; if(c>='A' && c<='F') return c-'A'+10; return -1; };
        int a=nibble(expected[2*i]),b=nibble(expected[2*i+1]);
        if(a<0 || b<0 || hash[i]!=(a*16+b)) return false;
    }
    return true;
}
Contract LoadContract(const fs::path& directory) {
    Contract t; std::ifstream f(directory / "manifest.txt");
    if (!f) throw std::runtime_error("Missing contraction manifest");
    fs::path cubin, params; std::string line;
    while(std::getline(f,line)) {
        std::istringstream in(line); std::string key; in>>key; if(key.empty() || key[0]=='#') continue;
        if(key=="cubin" || key=="params" || key=="kernel") { std::string value; in>>std::quoted(value);
            if(key=="cubin") cubin=directory/value; else if(key=="params") params=directory/value; else t.name=value;
        } else if(key=="grid" || key=="block") { api::Dim3 dim{}; in>>dim.x>>dim.y>>dim.z; if(key=="grid") t.grid=dim; else t.block=dim;
        } else if(key=="shared") in>>t.shared;
        else if(key=="workspace_bytes") in>>t.workspace_bytes;
        else if(key=="workspace_reset_bytes") in>>t.reset_bytes;
        else if(key=="patch") {
            std::string offset,name,add; in>>offset>>name; uint32_t binding=6;
            constexpr const char* names[]={"A_packed","SF_A","B_packed","SF_B","D","workspace"};
            for(uint32_t i=0;i<6;++i) if(name==names[i]) binding=i;
            if(binding==6) throw std::runtime_error("Unknown contraction parameter binding");
            t.patches.push_back({size_t(Number(offset)),binding,(in>>add)?Number(add):0});
        } else throw std::runtime_error("Unknown contraction manifest field");
        if(in.fail() && !in.eof()) throw std::runtime_error("Malformed contraction manifest");
    }
    if(cubin.empty() || params.empty() || t.name.empty() || !t.grid.x || !t.grid.y || !t.grid.z ||
       !t.block.x || !t.block.y || !t.block.z || t.workspace_bytes != 256 || t.reset_bytes != 0)
        throw std::runtime_error("Contraction manifest differs from zero-reset reference ABI");
    t.cubin=Read(cubin); t.params=Read(params);
    if(t.params.size()!=3456 || t.patches.empty()) throw std::runtime_error("Contraction Params ABI mismatch");
    std::vector<bool> used(t.params.size());
    for(auto& p:t.patches) {
        if(p.offset>t.params.size() || t.params.size()-p.offset<8) throw std::runtime_error("Contraction patch outside Params");
        for(size_t i=0;i<8;++i) { if(used[p.offset+i]) throw std::runtime_error("Overlapping contraction patches"); used[p.offset+i]=true; }
    }
    return t;
}
api::Handle Module(Device& d, const Blob& bytes) {
    api::Handle result=nullptr;
    auto original=G().create_module.load(); if(!original) throw std::runtime_error("NVAPI module entry not resolved");
    NvCheck(original(d.owner.Get(),bytes.data(),uint32_t(bytes.size()),&result));
    if(!result) throw std::runtime_error("NVAPI returned null module"); d.modules.push_back(result); return result;
}
api::Handle Function(Device& d, api::Handle module, const char* name) {
    api::Handle result=nullptr;
    auto original=G().create_function.load(); if(!original) throw std::runtime_error("NVAPI function entry not resolved");
    NvCheck(original(d.owner.Get(),module,name,&result));
    if(!result) throw std::runtime_error("NVAPI returned null function"); d.functions.push_back(result); return result;
}
Buffer UploadBytes(Device& d, const void* data, uint64_t bytes) {
    auto b=Allocate(d.owner.Get(),d.budget,bytes,D3D12_HEAP_TYPE_UPLOAD);
    void* mapped=nullptr; D3D12_RANGE no_read{0,0}; Check(b.gpu->Map(0,&no_read,&mapped));
    if(data) std::memcpy(mapped,data,bytes); else std::memset(mapped,0,bytes);
    D3D12_RANGE written{0,SIZE_T(bytes)}; b.gpu->Unmap(0,&written); return b;
}
void PrepareDevice(const std::shared_ptr<Device>& d) {
    fs::path assets; { std::lock_guard lock(G().mutex); assets=G().assets; }
    if(d->runtime_path.empty() || !d->runtime_path.is_absolute()) throw std::runtime_error("Replacement requires the actual signed NR runtime path");
    d->asset_locks.push_back(std::make_unique<LockedFile>(d->runtime_path));
    if(!ExactHexHash(d->runtime_path,"e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e"))
        throw std::runtime_error("NR runtime/model SHA-256 is not approved for the fixed NVFP4 weights");
    d->runtime_approved=true;
    for(const auto& entry:locked_assets::files) d->asset_locks.push_back(std::make_unique<LockedFile>(assets/entry.path));
    VerifyAssets(assets);
    // Keep both the loaded file bytes and the exact builder/module hash gates
    // paired with this device preparation; no process-global verified boolean.
    d->cuda.Init(d->owner.Get(),assets);
    if(d->optimized_settings.directory.empty() && policy::HasQualifiedPolicies()) {
        policy::Profile profile; profile.compute_major=d->cuda.major; profile.compute_minor=d->cuda.minor; profile.sm_count=d->cuda.sm;
        auto version=Symbol<int (__stdcall*)(int*)>(d->cuda.driver,"cuDriverGetVersion"); int driver_version=0;
        if(version(&driver_version)) throw std::runtime_error("CUDA driver profile query failed"); profile.cuda_driver_api_version=uint32_t(driver_version);
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter1> adapter; DXGI_ADAPTER_DESC1 desc{};
        Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory))); Check(factory->EnumAdapterByLuid(d->owner->GetAdapterLuid(),IID_PPV_ARGS(&adapter)));
        Check(adapter->GetDesc1(&desc)); profile.vendor_id=desc.VendorId; profile.device_id=desc.DeviceId;
        const auto* accepted=policy::FindQualifiedPolicy("e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e",profile);
        if(!accepted) throw std::runtime_error("No compiled quality/performance policy matches this adapter and driver profile");
        auto& settings=d->optimized_settings; settings.directory=assets/L"qualified";
        settings.cubin_sha=accepted->cubin_sha256; settings.builder_sha=accepted->builder_sha256; settings.build_id=accepted->build_id;
        settings.kernel_id=accepted->kernel_id; settings.variant=accepted->variant; settings.fused_publish=accepted->fused_publish;
        settings.tested_m.assign(accepted->tested_m.begin(),accepted->tested_m.begin()+accepted->tested_m_count); settings.allow_auto=true; settings.qualified_by_registry=true;
    }
    for(auto entry:std::array<std::pair<uint64_t,const char*>,3>{{{960,"contract"},{2160,"contract_m2160"},{3840,"contract_m3840"}}})
        d->contracts.emplace(entry.first,LoadContract(assets/entry.second));
    Blob weights=Read(assets/"weights.bin"); std::ifstream map(assets/"weights.txt");
    if(!map) throw std::runtime_error("Missing weights mapping"); std::array<bool,8> seen{}; std::string line;
    while(std::getline(map,line)) {
        std::istringstream in(line); std::string key; in>>key; if(key.empty() || key[0]=='#') continue;
        if(key=="gate_inverse_tensor_scale") { float gate=0; in>>gate; if(gate!=1) throw std::runtime_error("Unknown gate inverse tensor scale"); continue; }
        uint32_t block=0; std::string expand,ep,es,contract,cp,cs,cosine,co;
        in>>block>>expand>>ep>>es>>contract>>cp>>cs>>cosine>>co;
        if(!in || key!="block" || block<31 || block>38 || seen[block-31] || expand!="expand" || contract!="contract" || cosine!="cosine")
            throw std::runtime_error("Malformed block 31-38 weight mapping");
        d->weights_offsets[block-31]={Number(ep),Number(es),Number(cp),Number(cs),Number(co)}; seen[block-31]=true;
    }
    for(uint32_t i=0;i<8;++i) {
        if(!seen[i]) throw std::runtime_error("Missing FFN block weights"); auto w=d->weights_offsets[i];
        for(auto r:std::array<std::pair<uint64_t,uint64_t>,5>{{{w.ep,2097152},{w.es,262144},{w.cp,2097152},{w.cs,262144},{w.cosine,2048}}})
            if(r.first>weights.size() || r.second>weights.size()-r.first) throw std::runtime_error("Weight range outside locked asset");
    }
    for(auto& [m,t]:d->contracts) { (void)m; t.function=Function(*d,Module(*d,t.cubin),t.name.c_str()); t.cubin.clear(); t.cubin.shrink_to_fit(); }
    auto io=Module(*d,Read(assets/"original_io.cubin"));
    d->pack=Function(*d,io,"pack_original_fp8"); d->epilogue=Function(*d,io,"contract_epilogue_unsplit_f16_pair_publish");
    d->expand=Function(*d,Module(*d,Read(assets/"expansion"/"fused_expand.cubin")),"fused_expand");
    if(!d->optimized_settings.directory.empty()) {
        const auto& settings=d->optimized_settings;
        auto cubin=settings.directory/(settings.fused_publish ? L"nr_nvfp4_contract_fused_sm120.cubin" : L"nr_nvfp4_contract_sm120.cubin");
        auto helper=settings.directory/L"nr_nvfp4_params.dll";
        d->asset_locks.push_back(std::make_unique<LockedFile>(cubin)); d->asset_locks.push_back(std::make_unique<LockedFile>(helper));
        if(!ExactHexHash(cubin,settings.cubin_sha) || !ExactHexHash(helper,settings.builder_sha))
            throw std::runtime_error("Optimized cubin/builder pair does not match accepted SHA-256 receipt");
        d->optimized_helper=LoadLibraryExW(helper.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!d->optimized_helper) throw std::runtime_error("Accepted optimized builder cannot be loaded");
        auto id=Symbol<NrNvfp4BuildIdFn>(d->optimized_helper,"nr_nvfp4_build_id_v1");
        const char* actual=id();
        if(!actual || settings.build_id!=actual) throw std::runtime_error("Optimized Params ABI build fingerprint differs from receipt");
        d->build_contract=Symbol<NrNvfp4BuildContractFn>(d->optimized_helper,"nr_nvfp4_build_contract");
        if(settings.fused_publish) d->build_fused=Symbol<NrNvfp4BuildFusedFn>(d->optimized_helper,"nr_nvfp4_build_fused_contract");
        d->optimized_contract=Function(*d,Module(*d,Read(cubin)),settings.kernel_id.c_str());
        if(!settings.expansion.directory.empty()) {
            const auto& e=settings.expansion;
            auto expansion_cubin=e.directory/L"nr_nvfp4_pack_expansion_sm120.cubin";
            auto expansion_helper=e.directory/L"nr_nvfp4_expansion_params.dll";
            d->asset_locks.push_back(std::make_unique<LockedFile>(expansion_cubin));
            d->asset_locks.push_back(std::make_unique<LockedFile>(expansion_helper));
            if(!ExactHexHash(expansion_cubin,e.cubin_sha) || !ExactHexHash(expansion_helper,e.builder_sha))
                throw std::runtime_error("Packed expansion cubin/helper do not match the explicit receipt");
            d->packed_expansion_helper=LoadLibraryExW(expansion_helper.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if(!d->packed_expansion_helper)throw std::runtime_error("Accepted packed expansion helper cannot be loaded");
            auto expansion_id=Symbol<const char* (__cdecl*)()>(d->packed_expansion_helper,"nr_nvfp4_expansion_build_id_v1");
            const char* actual_expansion_id=expansion_id();
            if(!actual_expansion_id || e.build_id!=actual_expansion_id)
                throw std::runtime_error("Packed expansion ABI fingerprint differs from the receipt");
            d->build_packed_expansion=Symbol<NrBuildPackExpansionFn>(d->packed_expansion_helper,"nr_nvfp4_build_pack_expansion");
            d->packed_expansion_function=Function(*d,Module(*d,Read(expansion_cubin)),e.kernel_id.c_str());
        }
        d->optimized_ready=true;
    }
    auto upload=std::make_shared<Upload>(d->owner.Get());
    d->weights=Allocate(d->owner.Get(),d->budget,weights.size()); d->norm=Allocate(d->owner.Get(),d->budget,256);
    upload->sources.push_back(UploadBytes(*d,weights.data(),weights.size()));
    std::array<uint8_t,256> norm{}; float one=1; std::memcpy(norm.data(),&one,sizeof(one));
    upload->sources.push_back(UploadBytes(*d,norm.data(),norm.size()));
    upload->Copy(d->weights,upload->sources[0],weights.size()); upload->Copy(d->norm,upload->sources[1],norm.size());
    // Publish ownership before ExecuteCommandLists; even Signal failure cannot
    // destroy upload dependencies under recorded work.
    { std::lock_guard lock(G().mutex); d->upload=upload; }
    upload->Submit();
}
void PrepareArena(const std::shared_ptr<Device>& d, uint64_t m, bool optimized) {
    if(optimized ? (!d->optimized_ready || !d->optimized_settings.Contains(m)) : !TemplateM(m))
        throw std::runtime_error("No accepted contraction implementation for observed M");
    {
        std::lock_guard lock(G().mutex);
        if(!d->ready) return;
        for(auto& a:d->arenas) if(a->m==m && a->optimized==optimized && !a->poisoned && (!a->leased || !a->ready)) return;
        // Evict only idle arenas that have no recording or upload dependency.
        d->arenas.erase(std::remove_if(d->arenas.begin(),d->arenas.end(),[&](const auto& a){return (a->m!=m || a->optimized!=optimized) && !a->leased && !a->upload;}),d->arenas.end());
    }
    auto arena=std::make_shared<Arena>(); arena->m=m; arena->optimized=optimized; arena->fused_publish=optimized && d->optimized_settings.fused_publish;
    arena->packed_expansion=arena->fused_publish&&!d->optimized_settings.expansion.directory.empty();
    const uint64_t padded=((m+127)/128)*128;
    if(!arena->packed_expansion) {
        arena->xp=Allocate(d->owner.Get(),d->budget,padded*1024/2);
        arena->xs=Allocate(d->owner.Get(),d->budget,padded*1024/16);
    }
    arena->yp=Allocate(d->owner.Get(),d->budget,padded*4096/2);
    arena->ys=Allocate(d->owner.Get(),d->budget,padded*4096/16);
    if(!arena->fused_publish) arena->c=Allocate(d->owner.Get(),d->budget,padded*1024*2);
    const Contract* contract=optimized ? nullptr : &d->contracts.at(m);
    NrNvfp4ContractInput input_contract{};
    if(optimized) {
        input_contract.struct_bytes=sizeof(input_contract); input_contract.abi_version=NR_NVFP4_ABI_VERSION;
        LUID luid=d->owner->GetAdapterLuid(); std::memcpy(&input_contract.adapter_luid,&luid,8);
        input_contract.m=uint32_t(m); input_contract.n=1024; input_contract.k=4096;
        input_contract.sm_count=d->cuda.sm; input_contract.variant=d->optimized_settings.variant;
        auto& info=arena->optimized_info; info.struct_bytes=sizeof(info); info.abi_version=NR_NVFP4_ABI_VERSION;
        char error[256]{};
        int32_t query_status;
        if(arena->fused_publish) {
            NrNvfp4FusedInput fused{}; fused.contract=input_contract; arena->fused_info.launch=info;
            query_status=d->build_fused(&fused,nullptr,0,&arena->fused_info,error,sizeof(error));
            info=arena->fused_info.launch;
            const auto offset=arena->fused_info.runtime_bindings_offset;
            if(arena->fused_info.runtime_bindings_bytes!=sizeof(NrNvfp4RuntimeBindings) || offset%8 || offset>info.params_bytes ||
               info.params_bytes-offset<sizeof(NrNvfp4RuntimeBindings)) throw std::runtime_error("Fused runtime binding region is outside accepted Params ABI");
        } else query_status=d->build_contract(&input_contract,nullptr,0,&info,error,sizeof(error));
        if(query_status!=NR_NVFP4_OK)
            throw std::runtime_error(std::string("Optimized contraction size query failed: ")+error);
        if(!info.params_bytes || info.params_bytes>65536 || info.params_alignment!=128 || info.workspace_zero_bytes ||
           info.workspace_bytes>MaximumBudget || info.sm_count!=uint32_t(d->cuda.sm) ||
           info.cuda_device_ordinal!=uint32_t(d->cuda.ordinal) || std::string(info.kernel_id)!=d->optimized_settings.kernel_id)
            throw std::runtime_error("Optimized contraction Params/device/workspace contract mismatch");
    }
    const uint64_t workspace_bytes=optimized ? arena->optimized_info.workspace_bytes : contract->workspace_bytes;
    if(workspace_bytes) arena->workspace=Allocate(d->owner.Get(),d->budget,workspace_bytes);
    for(uint32_t i=0;i<8;++i) {
        const auto w=d->weights_offsets[i];
        if(arena->packed_expansion) {
            NrExpansionInput input{};input.struct_bytes=sizeof(input);input.abi_version=NR_EXPANSION_ABI_VERSION;
            input.original_fp8=d->weights.Address();input.original_capacity=d->weights.bytes;
            input.weights=d->weights.Address()+w.ep;input.weights_capacity=2097152;
            input.weight_scales=d->weights.Address()+w.es;input.weight_scales_capacity=262144;
            input.packed_output=arena->yp.Address();input.packed_output_capacity=arena->yp.bytes;
            input.output_scales=arena->ys.Address();input.output_scales_capacity=arena->ys.bytes;
            input.rows=uint32_t(m);input.columns=4096;input.reduction=1024;input.inverse_tensor_scale=1;
            input.sm_count=d->cuda.sm;input.compute_major=d->cuda.major;input.compute_minor=d->cuda.minor;
            input.max_shared_bytes=d->cuda.max_shared;input.variant=d->optimized_settings.expansion.variant;
            NrExpansionInfo info{};info.struct_bytes=sizeof(info);info.abi_version=NR_EXPANSION_ABI_VERSION;char error[256]{};
            if(d->build_packed_expansion(&input,nullptr,0,&info,error,sizeof(error))!=NR_EXPANSION_OK)
                throw std::runtime_error(std::string("Packed expansion query failed: ")+error);
            if(info.params_bytes!=sizeof(NrExpansionParams) || info.params_alignment!=alignof(NrExpansionParams) ||
               info.linear_input_bytes!=8 || info.linear_input_offset%8 || info.linear_input_offset>info.params_bytes ||
               info.params_bytes-info.linear_input_offset<8 || info.original_bytes!=((m+15)/16)*16*1024 ||
               info.weights_bytes!=2097152 || info.weight_scales_bytes!=262144 ||
               info.packed_output_bytes>arena->yp.bytes || info.output_scales_bytes>arena->ys.bytes ||
               info.sm_count!=uint32_t(d->cuda.sm) || info.shared_bytes>uint32_t(d->cuda.max_shared) ||
               std::string(info.kernel_id)!=d->optimized_settings.expansion.kernel_id ||
               !info.grid[0] || !info.grid[1] || !info.grid[2] || !info.block[0] || !info.block[1] || !info.block[2])
                throw std::runtime_error("Packed expansion launch/footprint does not match its accepted ABI");
            auto& params=arena->packed_expansion_params[i];params.Resize(size_t(info.params_bytes),size_t(info.params_alignment));
            auto built=info;
            if(d->build_packed_expansion(&input,params.Data(),info.params_bytes,&built,error,sizeof(error))!=NR_EXPANSION_OK ||
               std::memcmp(&info,&built,sizeof(info)) || (i&&std::memcmp(&info,&arena->packed_expansion_info,sizeof(info))))
                throw std::runtime_error(std::string("Packed expansion descriptor encode failed: ")+error);
            arena->packed_expansion_info=info;
        } else {
            FusedInput input{int(m),4096,1024,d->cuda.sm,arena->xp.Address(),d->weights.Address()+w.ep,
                arena->xs.Address(),d->weights.Address()+w.es,0,arena->yp.Address(),arena->ys.Address(),d->norm.Address(),0};
            auto& info=arena->expand_info[i]; d->cuda.Encode(input,arena->expand_params[i],info);
            if(info.sfa_bytes>arena->xs.bytes || info.sfb_bytes>262144 || info.sfd_bytes>arena->ys.bytes || info.shared_bytes>UINT32_MAX)
                throw std::runtime_error("Expansion descriptor exceeds scale/shared capacity");
        }
        if(optimized) {
            auto& cp=arena->optimized_params[i]; auto& info=arena->optimized_info;
            if((!arena->fused_publish && info.output_bytes>arena->c.bytes) || info.weight_scale_bytes>262144 || info.activation_scale_bytes>arena->ys.bytes)
                throw std::runtime_error("Optimized contraction output/scale capacity exceeds arena");
            input_contract.weights=d->weights.Address()+w.cp; input_contract.weight_scales=d->weights.Address()+w.cs;
            input_contract.activations=arena->yp.Address(); input_contract.activation_scales=arena->ys.Address(); input_contract.output=arena->fused_publish ? 0 : arena->c.Address();
            input_contract.workspace=info.workspace_bytes ? arena->workspace.Address() : 0;
            input_contract.workspace_capacity=info.workspace_bytes ? arena->workspace.bytes : 0;
            cp.Resize(size_t(info.params_bytes),size_t(info.params_alignment));
            NrNvfp4ContractInfo built{}; built.struct_bytes=sizeof(built); built.abi_version=NR_NVFP4_ABI_VERSION; char error[256]{};
            int32_t build_status;
            if(arena->fused_publish) {
                // These private, aligned placeholders are never launched. The
                // named linear region is overwritten from the matching owned
                // original contraction before the first record call.
                NrNvfp4FusedInput fused{input_contract,arena->yp.Address(),d->weights.Address()+w.cosine,arena->yp.Address(),arena->ys.Address(),arena->ys.Address(),arena->ys.Address()};
                NrNvfp4FusedInfo output{}; output.launch=built;
                build_status=d->build_fused(&fused,cp.Data(),info.params_bytes,&output,error,sizeof(error)); built=output.launch;
                if(output.runtime_bindings_offset!=arena->fused_info.runtime_bindings_offset || output.runtime_bindings_bytes!=arena->fused_info.runtime_bindings_bytes)
                    throw std::runtime_error("Fused runtime binding region changed during encoding");
            } else build_status=d->build_contract(&input_contract,cp.Data(),info.params_bytes,&built,error,sizeof(error));
            if(build_status!=NR_NVFP4_OK)
                throw std::runtime_error(std::string("Optimized contraction descriptor encode failed: ")+error);
            auto query_properties=built;
            std::memcpy(query_properties.grid,info.grid,sizeof(info.grid)); std::memcpy(query_properties.block,info.block,sizeof(info.block));
            if(std::memcmp(&query_properties,&info,sizeof(info))) throw std::runtime_error("Optimized descriptor properties changed after size query");
            if(!built.grid[0] || !built.grid[1] || !built.grid[2] || !built.block[0] || !built.block[1] || !built.block[2])
                throw std::runtime_error("Optimized descriptor has invalid launch geometry");
            if(i && (std::memcmp(built.grid,info.grid,sizeof(info.grid)) || std::memcmp(built.block,info.block,sizeof(info.block))))
                throw std::runtime_error("Optimized per-block launch geometry changed");
            info=built;
            continue;
        }
        auto& params=arena->contract_params[i]; params=contract->params;
        const std::array<std::pair<uint64_t,uint64_t>,6> bindings{{
            {d->weights.Address()+w.cp,2097152},{d->weights.Address()+w.cs,262144},
            {arena->yp.Address(),arena->yp.bytes},{arena->ys.Address(),arena->ys.bytes},
            {arena->c.Address(),arena->c.bytes},{arena->workspace.Address(),arena->workspace.bytes}}};
        for(const auto& patch:contract->patches) {
            const auto binding=bindings[patch.binding]; if(patch.add>=binding.second) throw std::runtime_error("Contraction patch exceeds resource capacity");
            uint64_t address=binding.first+patch.add; std::memcpy(params.data()+patch.offset,&address,8);
        }
    }
    auto upload=std::make_shared<Upload>(d->owner.Get());
    // A one MiB upload tile bounds transient memory; repeated CopyBufferRegion
    // initializes a newly allocated arena once. Every-frame reset is zero.
    upload->sources.push_back(UploadBytes(*d,nullptr,1ull<<20));
    for(auto* b:{&arena->xp,&arena->xs,&arena->yp,&arena->ys,&arena->c,&arena->workspace}) if(b->gpu) upload->Copy(*b,upload->sources[0],b->bytes);
    arena->upload=upload;
    { std::lock_guard lock(G().mutex); d->arenas.push_back(arena); }
    try { upload->Submit(); } catch(...) { arena->poisoned=true; throw; }
}
