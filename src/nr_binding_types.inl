// Kept outside NrContext's inline storage: the existing deferred context move
// must never move a descriptor lease which a recorded list can still address.
struct NrBindingPacket {
    NrSlot lease{};
    nr_binding::Plan plan{};
    ID3D12DescriptorHeap* heaps[nr_binding::kHeaps]{};
    bool valid=false;
    unsigned long long last_used=0;
};
struct NrBindingPool {
    NrBindingPacket packets[nr_binding::kPackets]{};
    unsigned long long serial=0;
};
