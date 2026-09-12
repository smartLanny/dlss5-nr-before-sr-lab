// Read-only CPU evidence for the missing linear-depth adapter. A finite matrix
// is NOT an accepted projection, depth conversion, or permission to run NR.
void ObserveRrDepthProjection(const NgxParams *params, const FeatureRecord &feature) {
    if (feature.rr_depth_type != 0) return;
    auto &receipt = nr_post_settings::projection;
    const auto now = GetTickCount64();
    const auto last = receipt.tick.load(std::memory_order_relaxed);
    if (receipt.generation.load(std::memory_order_relaxed) == feature.generation &&
        now >= last && now - last < 1000) return;
    float matrix[16]{};
    void *pointer = nullptr;
    int state = 1;
    auto getter = ParamSlot<GetVoidFn>(params, 8);
    if (getter && getter(params, "ViewToClipMatrix", &pointer) == kNgxSuccess && pointer) {
        SIZE_T copied = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), pointer, matrix, sizeof(matrix), &copied) ||
            copied != sizeof(matrix)) state = 2;
        else {
            state = 4;
            for (const float value : matrix) if (!std::isfinite(value)) state = 3;
        }
    }
    // Writer is serialized by the existing chain lock; atomic fields permit a
    // bounded reader to discard a concurrent update without accessing a pointer.
    receipt.sequence.fetch_add(1, std::memory_order_seq_cst);
    receipt.state.store(state, std::memory_order_seq_cst);
    receipt.generation.store(feature.generation, std::memory_order_seq_cst);
    receipt.tick.store(now, std::memory_order_seq_cst);
    for (unsigned i = 0; i != 16; ++i)
        receipt.values[i].store(state == 4 ? matrix[i] : 0.0f, std::memory_order_seq_cst);
    receipt.sequence.fetch_add(1, std::memory_order_seq_cst);
}
