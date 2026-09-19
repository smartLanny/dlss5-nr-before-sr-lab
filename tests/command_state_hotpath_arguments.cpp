// Actual production setters with minimal CPU-only D3D12/COM test doubles.
// Differential oracle: the same setters extracted from frozen lockperf1.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
using UINT = std::uint32_t;
using UINT64 = std::uint64_t;
struct IUnknown { int references = 1; };
struct ID3D12RootSignature : IUnknown {};
struct ID3D12DescriptorHeap : IUnknown { unsigned type = 0; };
static void IUnknown_AddRef(IUnknown *p) { ++p->references; }
static void IUnknown_Release(IUnknown *p) { assert(p->references > 1); --p->references; }
struct HeapDescription { unsigned Type; };
static HeapDescription ID3D12DescriptorHeap_GetDesc(ID3D12DescriptorHeap *p) { return {p->type}; }
constexpr UINT D3D12_GRAPHICS_STATE_COMPUTE_ROOT_SIGNATURE = 1;
constexpr UINT D3D12_GRAPHICS_STATE_GRAPHICS_ROOT_SIGNATURE = 2;
constexpr UINT D3D12_GRAPHICS_STATE_DESCRIPTOR_HEAP = 4;
#include "production_argument_types.inl"
struct NativeCommandState {
    bool known = true;
    const char *unknown_reason = "none";
    UINT unknown_domains = 0;
    ID3D12RootSignature *roots[2]{};
    ID3D12DescriptorHeap *heaps[2]{};
    CommandRootArgument arguments[2][64]{};
};
#include "production_arguments.inl"
namespace reference {
// Give the reference helper a distinct name to avoid global-type ADL.
#define CommandStateAssign BaselineCommandStateAssign
#include "baseline_arguments.inl"
#undef CommandStateAssign
}
static void equal(const NativeCommandState &a, const NativeCommandState &b) {
    assert(a.known == b.known && a.unknown_domains == b.unknown_domains);
    assert(std::strcmp(a.unknown_reason, b.unknown_reason) == 0);
    for (unsigned stage = 0; stage < 2; ++stage) {
        assert(a.roots[stage] == b.roots[stage] && a.heaps[stage] == b.heaps[stage]);
        for (unsigned index = 0; index < 64; ++index) {
            const auto &x = a.arguments[stage][index], &y = b.arguments[stage][index];
            assert(x.kind == y.kind);
            if (x.kind == CommandRootKind::Empty) continue;
            if (x.kind != CommandRootKind::Constants) { assert(x.value == y.value); continue; }
            assert(x.constant_mask == y.constant_mask);
            for (unsigned i = 0; i < 64; ++i)
                if (x.constant_mask & (UINT64{1} << i)) assert(x.constants[i] == y.constants[i]);
        }
    }
}
static int store_budget() {
    auto p = std::make_unique<NativeCommandState>();
    UINT values[64]; std::fill_n(values, 64, 0xA57E1200u);
    CommandStateSetConstants(*p, 0, 63, 64, values, 0);
    CommandStateSetValue(*p, 0, 63, CommandRootKind::Cbv, 0x123456789ULL);
    const auto &a = p->arguments[0][63];
    assert(a.kind == CommandRootKind::Cbv && a.value == 0x123456789ULL && a.constant_mask == 0);
    unsigned untouched = 0;
    for (auto v : a.constants) if (v == values[0]) ++untouched;
    std::cout << "scalar setter inactive constants preserved=" << untouched << "/64\n";
    return untouched == 64 ? 0 : 3;
}
static void semantic_tests() {
    auto a = std::make_unique<NativeCommandState>(), b = std::make_unique<NativeCommandState>();
    ID3D12RootSignature roots[3];
    ID3D12DescriptorHeap heaps[3]; heaps[1].type = 1;
    UINT values[64]; for (unsigned i = 0; i < 64; ++i) values[i] = i + 1;
    // Full constants -> scalar -> partial constants must not resurrect old bits.
    for (unsigned stage = 0; stage < 2; ++stage) {
        for (auto kind : {CommandRootKind::Table, CommandRootKind::Cbv, CommandRootKind::Srv, CommandRootKind::Uav}) {
            CommandStateSetConstants(*a, stage, 63, 64, values, 0);
            reference::CommandStateSetConstants(*b, stage, 63, 64, values, 0);
            CommandStateSetValue(*a, stage, 63, kind, 999);
            reference::CommandStateSetValue(*b, stage, 63, kind, 999);
            CommandStateSetConstants(*a, stage, 63, 1, values + 3, 63);
            reference::CommandStateSetConstants(*b, stage, 63, 1, values + 3, 63);
            equal(*a, *b);
            assert(a->arguments[stage][63].constant_mask == (UINT64{1} << 63));
            CommandStateSetConstants(*a, stage, 63, 2, values + 7, 0);
            reference::CommandStateSetConstants(*b, stage, 63, 2, values + 7, 0);
            equal(*a, *b);
        }
    }
    // Seeded state-machine differential test. Old implementation clears unused
    // bytes; comparisons intentionally model the real tag/mask-gated reader.
    std::mt19937 rng(0xD2100277);
    for (unsigned round = 0; round < 20000; ++round) {
        const unsigned stage = rng() % 2, slot = rng() % 64;
        switch (rng() % 8) {
        case 0: case 1: {
            const CommandRootKind kinds[] = {CommandRootKind::Table, CommandRootKind::Cbv, CommandRootKind::Srv, CommandRootKind::Uav};
            const auto kind = kinds[rng() % 4];
            const UINT64 value = (UINT64{rng()} << 32) | rng();
            CommandStateSetValue(*a, stage, slot, kind, value);
            reference::CommandStateSetValue(*b, stage, slot, kind, value);
            break;
        }
        case 2: case 3: {
            const unsigned offset = rng() % 65, count = rng() % (65 - offset);
            CommandStateSetConstants(*a, stage, slot, count, count ? values : nullptr, offset);
            reference::CommandStateSetConstants(*b, stage, slot, count, count ? values : nullptr, offset);
            break;
        }
        case 4: {
            auto *root = &roots[rng() % 3];
            CommandStateSetRoot(*a, stage, root);
            reference::CommandStateSetRoot(*b, stage, root);
            break;
        }
        case 5: {
            ID3D12DescriptorHeap *bound[] = {&heaps[(rng() % 2) * 2], &heaps[1]};
            const unsigned count = rng() % 3;
            CommandStateSetHeaps(*a, count, count ? bound : nullptr);
            reference::CommandStateSetHeaps(*b, count, count ? bound : nullptr);
            break;
        }
        case 6:
            // A signature dirtied by an opaque command invalidates arguments
            // even when the application sets the same pointer again.
            a->unknown_domains |= 3; b->unknown_domains |= 3;
            CommandStateSetRoot(*a, stage, a->roots[stage]);
            reference::CommandStateSetRoot(*b, stage, b->roots[stage]);
            break;
        default:
            a->unknown_domains |= 4; b->unknown_domains |= 4;
            CommandStateSetHeaps(*a, 0, nullptr);
            reference::CommandStateSetHeaps(*b, 0, nullptr);
            break;
        }
        equal(*a, *b);
    }
    // Invalid range/pointer still poisons state; no out-of-bounds write.
    CommandStateSetValue(*a, 0, 64, CommandRootKind::Uav, 1);
    reference::CommandStateSetValue(*b, 0, 64, CommandRootKind::Uav, 1); equal(*a, *b);
    CommandStateSetConstants(*a, 1, 0, 2, values, 63);
    reference::CommandStateSetConstants(*b, 1, 0, 2, values, 63); equal(*a, *b);
    CommandStateSetConstants(*a, 1, 0, 1, nullptr, 0);
    reference::CommandStateSetConstants(*b, 1, 0, 1, nullptr, 0); equal(*a, *b);
    // Release fixture references. The production reset/release is unchanged.
    for (unsigned s = 0; s < 2; ++s) {
        CommandStateAssign(a->roots[s], static_cast<ID3D12RootSignature *>(nullptr));
        reference::BaselineCommandStateAssign(b->roots[s], static_cast<ID3D12RootSignature *>(nullptr));
        CommandStateAssign(a->heaps[s], static_cast<ID3D12DescriptorHeap *>(nullptr));
        reference::BaselineCommandStateAssign(b->heaps[s], static_cast<ID3D12DescriptorHeap *>(nullptr));
    }
    for (const auto &r : roots) assert(r.references == 1);
    for (const auto &h : heaps) assert(h.references == 1);
    std::cout << "actual tagged-setter differential sequences=20000; constants transitions, ranges and COM balance passed\n";
}
int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--store-budget") return store_budget();
    semantic_tests();
    return 0;
}
