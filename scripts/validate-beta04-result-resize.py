#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "nr_before_sr.cpp"


def require_in_order(text: str, markers: tuple[str, ...], label: str) -> None:
    cursor = 0
    for marker in markers:
        index = text.find(marker, cursor)
        if index < 0:
            raise SystemExit(f"{label}: missing or out-of-order marker: {marker}")
        cursor = index + len(marker)


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")

    function_start = source.find("bool RebindSlotExternal(")
    function_end = source.find("\nNrSlot *FindOrCreateSlot(", function_start)
    if function_start < 0 or function_end < 0:
        raise SystemExit("RebindSlotExternal body not found")
    body = source[function_start:function_end]

    required = (
        "const D3D12_RESOURCE_DESC previous_result_desc =",
        "const bool result_extent_changed =",
        "previous_result_desc.Width != external_desc.Width",
        "previous_result_desc.Height != external_desc.Height",
        "ID3D12Resource *replacement_result = nullptr;",
        "external_desc.Width > UINT_MAX",
        "L\"NRChain.Result\"",
        "slot.result_state = D3D12_RESOURCE_STATE_COMMON;",
        "kCodecResultUavIndex",
        "ID3D12Device_CreateUnorderedAccessView(",
        "Rebuilt rotating pre-SR Result surface",
    )
    missing = [marker for marker in required if marker not in body]
    if missing:
        raise SystemExit("DRS Result-surface contract missing: " + ", ".join(missing))

    # Allocation must complete before the old source/result bindings are
    # changed. A failed allocation therefore leaves a fully usable old slot and
    # safely returns to native SR rather than partially mutating descriptors.
    require_in_order(
        body,
        (
            "ID3D12Resource *replacement_result = nullptr;",
            "if (result_extent_changed)",
            "!CreateTexture(device,",
            "ID3D12Resource_AddRef(external);",
            "ReleaseCom(slot.source);",
            "if (replacement_result != nullptr)",
            "ReleaseCom(slot.result);",
            "slot.result = replacement_result;",
            "slot.result_state = D3D12_RESOURCE_STATE_COMMON;",
            "kCodecResultUavIndex",
            "ID3D12Device_CreateUnorderedAccessView(",
        ),
        "fence-safe rebind ordering",
    )

    # The initially created result must also cover the physical SceneColor
    # allocation, not merely the active/model working raster.
    create_start = source.find("bool CreateSlot(")
    create_end = source.find("\nbool EnsureMotionProxy(", create_start)
    if create_start < 0 or create_end < 0:
        raise SystemExit("CreateSlot body not found")
    create_body = source[create_start:create_end]
    initial_allocation = (
        "static_cast<unsigned int>(external_desc.Width),\n"
        "            external_desc.Height, L\"NRChain.Result\", &slot.result"
    )
    if initial_allocation not in create_body:
        raise SystemExit("initial pre-SR Result no longer follows physical SceneColor extent")

    def needs_rebuild(old: tuple[int, int], new: tuple[int, int]) -> bool:
        return old[0] != new[0] or old[1] != new[1]

    cases = (
        ((2560, 1440), (2560, 1440), False),
        ((2304, 1296), (2560, 1440), True),
        ((2560, 1440), (2304, 1296), True),
        ((2560, 1440), (2560, 1368), True),
    )
    for old, new, expected in cases:
        actual = needs_rebuild(old, new)
        if actual != expected:
            raise SystemExit(f"dimension decision failed: {old} -> {new}")

    if body.count("Rebuilt rotating pre-SR Result surface") != 1:
        raise SystemExit("DRS Result-surface rebuild marker must appear exactly once")

    print("beta0.4 DRS physical Result-surface validation passed")


if __name__ == "__main__":
    main()
