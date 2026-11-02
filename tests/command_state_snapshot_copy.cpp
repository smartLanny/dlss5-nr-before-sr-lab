// Exact production snapshot/copy/setter code with CPU-only D3D12/COM doubles.
// No command list, driver or GPU is created by this fixture.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
using UINT = std::uint32_t;
using UINT64 = std::uint64_t;
using FLOAT = float;
using BOOL = int;
constexpr BOOL FALSE = 0;
using D3D12_PRIMITIVE_TOPOLOGY = unsigned;
using D3D12_PREDICATION_OP = unsigned;
using D3D12_SHADING_RATE = unsigned;
using D3D12_SHADING_RATE_COMBINER = unsigned;
using D3D12_INDEX_BUFFER_STRIP_CUT_VALUE = unsigned;
constexpr unsigned D3D_PRIMITIVE_TOPOLOGY_UNDEFINED = 0;
constexpr unsigned D3D12_PREDICATION_OP_EQUAL_ZERO = 0;
constexpr unsigned D3D12_SHADING_RATE_1X1 = 0;
constexpr unsigned D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED = 0;
constexpr unsigned D3D12_DESCRIPTOR_HEAP_TYPE_RTV = 2;
constexpr unsigned D3D12_DESCRIPTOR_HEAP_TYPE_DSV = 3;
constexpr unsigned D3D12_GRAPHICS_STATE_COMPUTE_ROOT_SIGNATURE = 1;
constexpr unsigned D3D12_GRAPHICS_STATE_GRAPHICS_ROOT_SIGNATURE = 2;
constexpr unsigned D3D12_GRAPHICS_STATE_DESCRIPTOR_HEAP = 4;
struct D3D12_VIEWPORT { float values[6]{}; };
struct D3D12_RECT { int values[4]{}; };
struct D3D12_INDEX_BUFFER_VIEW { UINT64 location{}; UINT size{}, format{}; };
struct D3D12_VERTEX_BUFFER_VIEW { UINT64 location{}; UINT size{}, stride{}; };
struct D3D12_STREAM_OUTPUT_BUFFER_VIEW { UINT64 location{}, size{}, filled{}; };
struct D3D12_CPU_DESCRIPTOR_HANDLE { UINT64 ptr{}; };
struct D3D12_SAMPLE_POSITION { signed char x{}, y{}; };
struct IUnknown { int references = 1; };
struct ID3D12RootSignature : IUnknown {};
struct ID3D12PipelineState : IUnknown {};
struct ID3D12StateObject : IUnknown {};
struct ID3D12Resource : IUnknown {};
struct ID3D12DescriptorHeap : IUnknown { unsigned type = 0; UINT64 cpu = 0; };
static unsigned checks = 0;
static void Check(bool condition, const char *message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
static void IUnknown_AddRef(IUnknown *object) { ++object->references; }
static void IUnknown_Release(IUnknown *object) {
    Check(object->references > 1, "balanced COM reference"); --object->references;
}
struct HeapDescription { unsigned Type; };
static HeapDescription ID3D12DescriptorHeap_GetDesc(ID3D12DescriptorHeap *heap) { return {heap->type}; }
static D3D12_CPU_DESCRIPTOR_HANDLE ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap *heap) {
    return {heap->cpu};
}
static bool descriptor_ready = true;
static unsigned descriptor_copies = 0;
static bool CopyCommandCpuHeap(ID3D12DescriptorHeap *&, ID3D12DescriptorHeap *source, unsigned, UINT count) {
    if (source && count) ++descriptor_copies;
    return descriptor_ready;
}
#include "production_snapshot.inl"

static void SameArguments(const NativeCommandState &copy, const NativeCommandState &source) {
    for (unsigned stage = 0; stage < 2; ++stage) {
        for (unsigned index = 0; index < 64; ++index) {
            const auto &a = copy.arguments[stage][index], &b = source.arguments[stage][index];
            Check(a.kind == b.kind, "binding tag");
            Check(a.value == b.value, "binding scalar");
            Check(a.constant_mask == b.constant_mask, "constant mask");
            if (b.kind == CommandRootKind::Constants)
                for (unsigned word = 0; word < 64; ++word)
                    if (b.constant_mask & (UINT64{1} << word))
                        Check(a.constants[word] == b.constants[word], "valid constant payload");
        }
    }
}

static void Semantics() {
    auto source = std::make_unique<NativeCommandState>();
    auto saved = std::make_unique<NativeCommandState>();
    auto restored = std::make_unique<NativeCommandState>();
    ID3D12RootSignature roots[3];
    ID3D12DescriptorHeap heaps[3]; heaps[1].type = 1;
    UINT words[64]; for (unsigned i = 0; i < 64; ++i) words[i] = 0x82000000u + i;
    std::mt19937 random(0x2772026);
    // Reuse a dirty destination across real setter transitions. The source's
    // kind/mask are the oracle; unused payload bytes are deliberately unequal.
    for (auto &stage : saved->arguments)
        for (auto &argument : stage) std::fill_n(argument.constants, 64, 0xDEADBEEFu);
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        const unsigned stage = random() % 2, index = random() % 64;
        switch (iteration % 8) {
        case 0: case 1: case 2: {
            const CommandRootKind kinds[] = {CommandRootKind::Table, CommandRootKind::Cbv,
                CommandRootKind::Srv, CommandRootKind::Uav};
            CommandStateSetValue(*source, stage, index, kinds[random() % 4], random()); break;
        }
        case 3: case 4: {
            const unsigned offset = random() % 65, count = random() % (65 - offset);
            CommandStateSetConstants(*source, stage, index, count, count ? words : nullptr, offset); break;
        }
        case 5: CommandStateSetRoot(*source, stage, &roots[random() % 3]); break;
        case 6: {
            ID3D12DescriptorHeap *bound[] = {&heaps[(random() % 2) * 2], &heaps[1]};
            const unsigned count = random() % 3;
            CommandStateSetHeaps(*source, count, count ? bound : nullptr); break;
        }
        default:
            // Signature invalidation must erase a destination's previous tag.
            source->unknown_domains |= 3;
            CommandStateSetRoot(*source, stage, source->roots[stage]); break;
        }
        Check(CopyNativeCommandState(*saved, *source), "capture returns success");
        SameArguments(*saved, *source);
        Check(CopyNativeCommandState(*restored, *saved), "restore returns success");
        SameArguments(*restored, *source);
    }
    // Both stages and high-word masks, full -> scalar -> partial and zero-count
    // constants. Reads of masked-out words must never become active on restore.
    for (unsigned stage = 0; stage < 2; ++stage) {
        CommandStateSetConstants(*source, stage, 63, 64, words, 0);
        Check(CopyNativeCommandState(*saved, *source), "full constants"); SameArguments(*saved, *source);
        CommandStateSetValue(*source, stage, 63, CommandRootKind::Uav, 1234);
        Check(CopyNativeCommandState(*saved, *source), "constants to scalar"); SameArguments(*saved, *source);
        CommandStateSetConstants(*source, stage, 63, 1, words + 7, 63);
        Check(CopyNativeCommandState(*saved, *source), "last constant bit"); SameArguments(*saved, *source);
        Check(saved->arguments[stage][63].constant_mask == (UINT64{1} << 63), "old constants stay inactive");
        CommandStateSetValue(*source, stage, 63, CommandRootKind::Cbv, 0);
        CommandStateSetConstants(*source, stage, 63, 1, words + 9, 15);
        Check(CopyNativeCommandState(*saved, *source), "short-copy boundary"); SameArguments(*saved, *source);
        CommandStateSetConstants(*source, stage, 63, 1, words + 11, 16);
        Check(CopyNativeCommandState(*saved, *source), "wide-copy boundary"); SameArguments(*saved, *source);
        CommandStateSetRoot(*source, stage, nullptr);
        CommandStateSetConstants(*source, stage, 63, 0, nullptr, 64);
        Check(CopyNativeCommandState(*saved, *source), "zero-count constants"); SameArguments(*saved, *source);
    }
    auto before = std::make_unique<NativeCommandState>();
    Check(CopyNativeCommandState(*before, *saved), "self-copy reference");
    Check(CopyNativeCommandState(*saved, *saved), "self-copy success"); SameArguments(*saved, *before);
    // Descriptor ownership and COM references remain in the actual enclosing
    // production function. A failed descriptor snapshot exits before publication.
    ID3D12DescriptorHeap source_rtv, source_dsv, saved_rtv, saved_dsv;
    source_rtv.cpu = 10; source_dsv.cpu = 20; saved_rtv.cpu = 30; saved_dsv.cpu = 40;
    CommandStateAssign(source->rtv_cache, &source_rtv); CommandStateAssign(source->dsv_cache, &source_dsv);
    CommandStateAssign(saved->rtv_cache, &saved_rtv); CommandStateAssign(saved->dsv_cache, &saved_dsv);
    source->null_pipeline_baseline = true; source->dynamic.target_count = 1; source->dynamic.has_depth_target = true;
    source->dynamic.targets[0].ptr = 10; source->dynamic.depth_target.ptr = 20;
    source->known = true; source->reset_serial = 712; source->dynamic.recorded = 0x7F;
    Check(CopyNativeCommandState(*saved, *source), "descriptor snapshot success");
    Check(saved->rtv_cache == &saved_rtv && saved->dsv_cache == &saved_dsv, "independent descriptor cache");
    Check(saved->dynamic.targets[0].ptr == 30 && saved->dynamic.depth_target.ptr == 40, "owned descriptor handles");
    Check(saved->known && saved->reset_serial == 712 && saved->dynamic.recorded == 0x7F, "nonargument state copied");
    descriptor_ready = false;
    Check(!CopyNativeCommandState(*saved, *source) && !saved->known, "descriptor failure rejects snapshot");
    Check(std::strcmp(saved->unknown_reason, "dynamic-descriptor-snapshot-allocation") == 0, "descriptor failure reason");
    Check(descriptor_copies == 3, "descriptor helper calls unchanged");
    for (auto *state : {source.get(), saved.get(), restored.get(), before.get()}) ReleaseNativeCommandState(*state);
    for (const auto &root : roots) Check(root.references == 1, "root references released");
    for (const auto &heap : heaps) Check(heap.references == 1, "heap references released");
    Check(source_rtv.references == 1 && source_dsv.references == 1 && saved_rtv.references == 1 && saved_dsv.references == 1,
        "descriptor references released");
    std::printf("PASS %u checks; 20000 production capture/restore transitions, masks, descriptors and COM balance\n", checks);
}

static int Budget() {
    auto source = std::make_unique<NativeCommandState>(), saved = std::make_unique<NativeCommandState>();
    for (auto &stage : saved->arguments)
        for (auto &argument : stage) std::fill_n(argument.constants, 64, 0xDEADBEEFu);
    for (unsigned stage = 0; stage < 2; ++stage)
        for (unsigned index = 0; index < 64; ++index)
            CommandStateSetValue(*source, stage, index, CommandRootKind::Cbv, 1234 + index);
    Check(CopyNativeCommandState(*saved, *source), "scalar snapshot succeeds"); SameArguments(*saved, *source);
    unsigned untouched = 0;
    for (const auto &stage : saved->arguments)
        for (const auto &argument : stage)
            for (auto word : argument.constants) untouched += word == 0xDEADBEEFu;
    std::printf("inactive constant words untouched=%u/8192; inactive payload bytes avoided=%u\n", untouched, untouched * 4);
    if (untouched != 8192) return 3;
    const UINT value = 77;
    for (unsigned stage = 0; stage < 2; ++stage)
        for (unsigned index = 0; index < 64; ++index)
            CommandStateSetConstants(*source, stage, index, 1, &value, 0);
    Check(CopyNativeCommandState(*saved, *source), "dense short-constant snapshot succeeds"); SameArguments(*saved, *source);
    untouched = 0;
    for (const auto &stage : saved->arguments)
        for (const auto &argument : stage)
            for (unsigned word = 16; word < 64; ++word) untouched += argument.constants[word] == 0xDEADBEEFu;
    std::printf("short-constant unused tail words untouched=%u/6144; tail bytes avoided=%u\n", untouched, untouched * 4);
    return untouched == 6144 ? 0 : 3;
}

static void Benchmark() {
    auto source = std::make_unique<NativeCommandState>(), saved = std::make_unique<NativeCommandState>();
    constexpr unsigned iterations = 100000;
    struct Scenario { const char *name; unsigned count, words, scalars; };
    const Scenario scenarios[] = {
        {"scalar", 0, 0, 32}, {"short-2", 1, 4, 30}, {"short-8", 4, 8, 16},
        {"short-32", 16, 2, 16}, {"short-128", 64, 1, 0}, {"wide-2", 1, 64, 0},
        {"synthetic-wide-128", 64, 64, 0},
    };
    for (const auto &scenario : scenarios) {
        for (unsigned stage = 0; stage < 2; ++stage)
            for (unsigned index = 0; index < 64; ++index) {
                auto &a = source->arguments[stage][index];
                a.kind = index < scenario.count ? CommandRootKind::Constants :
                    index < scenario.count + scenario.scalars ? CommandRootKind::Cbv : CommandRootKind::Empty;
                a.constant_mask = index < scenario.count ?
                    (scenario.words == 64 ? ~UINT64{0} : (UINT64{1} << scenario.words) - 1) : 0;
                std::fill_n(a.constants, 64, index + 7);
            }
        for (unsigned repeat = 0; repeat < 5; ++repeat) {
            UINT64 sum = 0;
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                source->arguments[0][0].value = i;
                Check(CopyNativeCommandState(*saved, *source), "benchmark snapshot succeeds");
                sum += saved->arguments[0][0].value + saved->arguments[1][63].constant_mask;
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::printf("scenario=%s constant_roots=%u words=%u repeat=%u iterations=%u ms=%.3f checksum=%llu\n",
                scenario.name, scenario.count * 2, scenario.words, repeat, iterations, ms, static_cast<unsigned long long>(sum));
        }
    }
}
int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--budget") return Budget();
    if (mode == "--bench") { Benchmark(); return 0; }
    Semantics(); return 0;
}
