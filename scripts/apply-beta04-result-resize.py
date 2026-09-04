#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "nr_before_sr.cpp"

OLD = """    if (slot.owns_source || !slot.owns_result || !pre_source_supported)
        return false;

    ID3D12Resource_AddRef(external);
    ReleaseCom(slot.source);
    slot.key = external;
    slot.source = external;
    slot.source_format = external_desc.Format;
    slot.converts_source_format =
        slot.source_format != DXGI_FORMAT_R16G16B16A16_FLOAT;
    slot.codec_color_mode = pre_sr_color_mode;
    slot.source_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = slot.source_format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, slot.source, &srv, cpu);
    RebindExposureSource(device, exposure, slot);
"""

NEW = """    if (slot.owns_source || !slot.owns_result || !pre_source_supported ||
        slot.result == nullptr)
        return false;

    // A stable Feature 18 working bucket can outlive many physically different
    // SceneColor allocations. When all rotating slots are occupied and one is
    // recycled, its full-render Result surface must follow the new allocation;
    // retaining the previous dimensions can truncate a larger active region or
    // hand native SR a replacement Color texture with the wrong physical extent.
    // CanMutateSlotLocked above proves the old Result is no longer referenced.
    const D3D12_RESOURCE_DESC previous_result_desc =
        ID3D12Resource_GetDesc(slot.result);
    const bool result_extent_changed =
        previous_result_desc.Width != external_desc.Width ||
        previous_result_desc.Height != external_desc.Height;
    ID3D12Resource *replacement_result = nullptr;
    if (result_extent_changed) {
        if (external_desc.Width > UINT_MAX ||
            !CreateTexture(device,
                           static_cast<unsigned int>(external_desc.Width),
                           external_desc.Height, L"NRChain.Result",
                           &replacement_result)) {
            Log("Rotating pre-SR slot could not rebuild Result for a physical "
                "extent change %llux%u -> %llux%u; native SR retained.",
                static_cast<unsigned long long>(previous_result_desc.Width),
                previous_result_desc.Height,
                static_cast<unsigned long long>(external_desc.Width),
                external_desc.Height);
            return false;
        }
    }

    ID3D12Resource_AddRef(external);
    ReleaseCom(slot.source);
    slot.key = external;
    slot.source = external;
    slot.source_format = external_desc.Format;
    slot.converts_source_format =
        slot.source_format != DXGI_FORMAT_R16G16B16A16_FLOAT;
    slot.codec_color_mode = pre_sr_color_mode;
    slot.source_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = slot.source_format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, slot.source, &srv, cpu);

    if (replacement_result != nullptr) {
        ReleaseCom(slot.result);
        slot.result = replacement_result;
        slot.result_state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_CPU_DESCRIPTOR_HANDLE result_cpu =
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
        result_cpu.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) *
                          kCodecResultUavIndex;
        D3D12_UNORDERED_ACCESS_VIEW_DESC result_uav = {};
        result_uav.Format = slot.result_uav_format;
        result_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(
            device, slot.result, nullptr, &result_uav, result_cpu);
        Log("Rebuilt rotating pre-SR Result surface: %llux%u -> %llux%u.",
            static_cast<unsigned long long>(previous_result_desc.Width),
            previous_result_desc.Height,
            static_cast<unsigned long long>(external_desc.Width),
            external_desc.Height);
    }
    RebindExposureSource(device, exposure, slot);
"""


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    marker = "Rebuilt rotating pre-SR Result surface"
    if marker in text:
        print("DRS Result-surface rebuild is already integrated")
        return
    count = text.count(OLD)
    if count != 1:
        raise SystemExit(f"expected one pre-SR rebind anchor, found {count}")
    text = text.replace(OLD, NEW, 1)
    SOURCE.write_text(text, encoding="utf-8", newline="\n")
    print("integrated fence-safe rotating Result-surface rebuild")


if __name__ == "__main__":
    main()
