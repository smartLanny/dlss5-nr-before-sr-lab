// Included after D3D12 declarations, before NrStageCapture. Ownership moves to
// the export worker only after the parent slot's exact retirement predicate.
struct NrNativeOutputCapture {
    nr_native_capture::Header header{};
    ID3D12Resource* source=nullptr;
    ID3D12Resource* readback=nullptr;
    UINT64 readback_bytes=0;
    unsigned row_pitch=0,coverage=0;
};
void ReleaseNativeOutputCapture(NrNativeOutputCapture& capture) {
    ReleaseCom(capture.readback);ReleaseCom(capture.source);capture={};
}
