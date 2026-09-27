// The NGX call uses the live original interface; all Core recording identities,
// resource ownership, Execute/Signal and Reset/retirement remain on the raw list.
// ReShade owns both its heap objects and virtual CPU descriptor handles. Do not
// partially unwrap this protocol or cache an interface past the native call.
ID3D12GraphicsCommandList* NrPrivateOriginalCallList(ID3D12GraphicsCommandList* native) {
    if(!native)return nullptr;
    static constexpr GUID unwrap={0x7f2c9a11,0x3b4e,0x4d6a,
        {0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    const auto identity=SnapshotPostBoundaryList(native);
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    const auto epoch=g_command_state_epoch.load(std::memory_order_acquire);
    unsigned depth=0;
    for(auto* alias=g_nr_post_boundary_alias_scope;alias&&depth++<16;alias=alias->parent) {
        const auto& proof=alias->identity;
        if(!identity.known||!proof.known||
           alias->epoch!=epoch||identity.address!=reinterpret_cast<std::uint64_t>(native)||
           proof.address!=identity.address||proof.token!=identity.token||
           proof.reset!=identity.reset||proof.device!=identity.device)continue;
        auto* original=reinterpret_cast<ID3D12GraphicsCommandList*>(alias->original);
        if(!original)continue;
        if(original==native&&g_command_state_epoch.load(std::memory_order_acquire)==epoch)return native;
        ID3D12GraphicsCommandList* raw=nullptr;
        const auto queried=ID3D12GraphicsCommandList_QueryInterface(original,unwrap,reinterpret_cast<void**>(&raw));
        const bool same=SUCCEEDED(queried)&&raw==native&&
            g_command_state_epoch.load(std::memory_order_acquire)==epoch;
        if(raw)ID3D12GraphicsCommandList_Release(raw);
        if(same)return original;
    }
#endif
    // Even GetDevice returning a raw interface cannot prove the NGX Init domain.
    // Without this current original-call receipt, preserve Z and refuse the call.
    return nullptr;
}
