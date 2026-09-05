// Included in the runtime namespace after Transition. The caller owns the
// same-device workset and has already recorded its exact command-list/fence use.
// This is full-size post-SR, not a low-resolution or synthetic-guide NR route.
constexpr bool KeepNativePostSrImage(bool identity, bool suppress_reset_frame,
                                     unsigned successful_frames,
                                     unsigned warmup_frames) noexcept {
    return identity || suppress_reset_frame || successful_frames <= warmup_frames;
}

bool CopyPostSrFp16Snapshot(ID3D12GraphicsCommandList *commands,
                            ID3D12Resource *native_output,
                            D3D12_RESOURCE_STATES &native_state,
                            ID3D12Resource *snapshot,
                            D3D12_RESOURCE_STATES &snapshot_state,
                            unsigned x, unsigned y,
                            unsigned width, unsigned height) {
    if (!commands || !native_output || !snapshot || native_output == snapshot ||
        !width || !height || native_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        return false;
    const auto source = ID3D12Resource_GetDesc(native_output);
    const auto destination = ID3D12Resource_GetDesc(snapshot);
    const auto plain_fp16 = [](const D3D12_RESOURCE_DESC &d) {
        // DENY_SHADER_RESOURCE requires a depth-stencil resource. It is not
        // a supported flag combination for an FP16 colour UAV. Do not treat
        // the lack of an SRV operation as permission to admit invalid flags.
        return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
            d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
            d.DepthOrArraySize == 1 && d.MipLevels == 1 &&
            d.SampleDesc.Count == 1 && d.SampleDesc.Quality == 0 &&
            (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 &&
            (d.Flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                        D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) == 0;
    };
    // Subtract before comparing: malformed UINT inputs cannot wrap the box.
    if (!plain_fp16(source) || !plain_fp16(destination) ||
        x > source.Width || width > source.Width - x ||
        y > source.Height || height > source.Height - y ||
        width > destination.Width || height > destination.Height ||
        x > UINT_MAX - width || y > UINT_MAX - height)
        return false;

    Transition(commands, native_output, native_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commands, snapshot, snapshot_state, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from = {}, to = {};
    from.pResource = native_output;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = snapshot;
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    const D3D12_BOX region = {x, y, 0, x + width, y + height, 1};
    ID3D12GraphicsCommandList_CopyTextureRegion(commands, &to, 0, 0, 0, &from, &region);
    Transition(commands, snapshot, snapshot_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commands, native_output, native_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return true;
}
