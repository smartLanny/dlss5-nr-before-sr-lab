#include "../nr_rr_depth_projection.h"
// Read-only CPU evidence for the linear-depth adapter. A finite matrix
// is NOT an accepted projection, depth conversion, or permission to run NR.
void ObserveRrDepthProjection(const NgxParams *params, const FeatureRecord &feature) {
    if (feature.rr_depth_type != 0) return;
    auto &receipt = nr_post_settings::projection;
    const auto now = GetTickCount64();
    const auto last = receipt.tick.load(std::memory_order_relaxed);
    if (receipt.generation.load(std::memory_order_relaxed) == feature.generation &&
        now >= last && now - last < 1000) return;
    auto getter = ParamSlot<GetVoidFn>(params, 8);
    const auto input=nr_rr_depth::ReadCurrent(
        [&](void*& pointer){return getter&&getter(params,"ViewToClipMatrix",&pointer)==kNgxSuccess;},
        [](void* pointer,void* target,size_t size){SIZE_T copied=0;
            return ReadProcessMemory(GetCurrentProcess(),pointer,target,size,&copied)&&copied==size;});
    // Keep historical F8 states: 4 means finite COPY, not accepted projection.
    const int state=input.state==nr_rr_depth::InputState::Unsupported?4:static_cast<int>(input.state);
    // Writer is serialized by the existing chain lock; atomic fields permit a
    // bounded reader to discard a concurrent update without accessing a pointer.
    receipt.sequence.fetch_add(1, std::memory_order_seq_cst);
    receipt.state.store(state, std::memory_order_seq_cst);
    receipt.generation.store(feature.generation, std::memory_order_seq_cst);
    receipt.tick.store(now, std::memory_order_seq_cst);
    for (unsigned i = 0; i != 16; ++i)
        receipt.values[i].store(state == 4 ? input.matrix[i] : 0.0f, std::memory_order_seq_cst);
    receipt.sequence.fetch_add(1, std::memory_order_seq_cst);
}
