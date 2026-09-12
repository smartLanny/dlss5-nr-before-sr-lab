// Executable ABI oracle: independent Microsoft slot table, plus reverse calls
// into a local proxy. Neither side learns slot numbers from the declaration.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
struct ID3D11Resource;
struct ID3D12Resource;
using NVSDK_NGX_Result = int;
#if defined(NR_TEST_OLD_VTABLE) && !defined(_MSC_VER)
#define _MSC_VER 1930
#define NR_TEST_UNDEF_MSC
#endif
#include "src/interop/nr_ngx_parameter_abi.h"
#ifdef NR_TEST_UNDEF_MSC
#undef _MSC_VER
#endif
static int expected = -1;
static unsigned calls = 0;
static void Mark(int slot, const char* key = "key") {
    if (slot != expected || std::strcmp(key, "key") != 0) {
        std::fprintf(stderr, "NGX ABI mismatch: expected native slot %d, dispatched %d\n", expected, slot);
        std::exit(1); // before interpreting any wrongly typed argument
    }
    ++calls;
}
template<class T, int Slot> T Value() {
    if constexpr (std::is_pointer_v<T>)
        return reinterpret_cast<T>(static_cast<std::uintptr_t>(0x10000 + Slot * 0x100));
    else return static_cast<T>(Slot + 0.25);
}
struct Remote { void** vtable; };
template<class T, int Slot> void RemoteSet(Remote*, const char* key, T value) {
    Mark(Slot, key);
    if (value != Value<T, Slot>()) std::exit(2);
}
template<class T, int Slot> int RemoteGet(const Remote*, const char* key, T* out) {
    Mark(Slot, key); *out = Value<T, Slot>(); return 1;
}
static void RemoteReset(Remote*) { Mark(16); }
#define ADDRESS(f) reinterpret_cast<void*>(f)
static void* microsoft_slots[] = {
    ADDRESS((RemoteSet<void*, 0>)), ADDRESS((RemoteSet<ID3D12Resource*, 1>)),
    ADDRESS((RemoteSet<ID3D11Resource*, 2>)), ADDRESS((RemoteSet<int, 3>)),
    ADDRESS((RemoteSet<unsigned int, 4>)), ADDRESS((RemoteSet<double, 5>)),
    ADDRESS((RemoteSet<float, 6>)), ADDRESS((RemoteSet<unsigned long long, 7>)),
    ADDRESS((RemoteGet<void*, 8>)), ADDRESS((RemoteGet<ID3D12Resource*, 9>)),
    ADDRESS((RemoteGet<ID3D11Resource*, 10>)), ADDRESS((RemoteGet<int, 11>)),
    ADDRESS((RemoteGet<unsigned int, 12>)), ADDRESS((RemoteGet<double, 13>)),
    ADDRESS((RemoteGet<float, 14>)), ADDRESS((RemoteGet<unsigned long long, 15>)),
    ADDRESS(RemoteReset)
};
#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static NVSDK_NGX_Parameter* Borrow(Remote* object) {
    return reinterpret_cast<NVSDK_NGX_Parameter*>(object);
}
template<class T, int Slot> void ReadRemote(const NVSDK_NGX_Parameter* p) {
    T result{}; expected = Slot;
    if (p->Get("key", &result) != 1 || result != Value<T, Slot>()) std::exit(3);
}
class LocalProxy : public NVSDK_NGX_Parameter {
public:
#define SET(T, S) void Set(const char* k, T v) override { Mark(S, k); if (v != Value<T, S>()) std::exit(4); }
    SET(unsigned long long, 7) SET(float, 6) SET(double, 5) SET(unsigned int, 4)
    SET(int, 3) SET(ID3D11Resource*, 2) SET(ID3D12Resource*, 1) SET(void*, 0)
#undef SET
#define GET(T, S) int Get(const char* k, T* out) const override { Mark(S, k); *out = Value<T, S>(); return 1; }
    GET(unsigned long long, 15) GET(float, 14) GET(double, 13) GET(unsigned int, 12)
    GET(int, 11) GET(ID3D11Resource*, 10) GET(ID3D12Resource*, 9) GET(void*, 8)
#undef GET
    void Reset() override { Mark(16); }
};
template<class T, int Slot> void WriteProxy(LocalProxy* p, void** table) {
    using Function = void (*)(LocalProxy*, const char*, T);
    expected = Slot;
    reinterpret_cast<Function>(table[Slot])(p, "key", Value<T, Slot>());
}
template<class T, int Slot> void ReadProxy(const LocalProxy* p, void** table) {
    using Function = int (*)(const LocalProxy*, const char*, T*);
    T result{}; expected = Slot;
    if (reinterpret_cast<Function>(table[Slot])(p, "key", &result) != 1 || result != Value<T, Slot>()) std::exit(5);
}
int main() {
    Remote remote{microsoft_slots};
    auto* p = Borrow(&remote);
#define WRITE(T, S) expected = S; p->Set("key", Value<T, S>())
    WRITE(unsigned long long, 7); WRITE(float, 6); WRITE(double, 5); WRITE(unsigned int, 4);
    WRITE(int, 3); WRITE(ID3D11Resource*, 2); WRITE(ID3D12Resource*, 1); WRITE(void*, 0);
#undef WRITE
    ReadRemote<unsigned long long, 15>(p); ReadRemote<float, 14>(p); ReadRemote<double, 13>(p);
    ReadRemote<unsigned int, 12>(p); ReadRemote<int, 11>(p); ReadRemote<ID3D11Resource*, 10>(p);
    ReadRemote<ID3D12Resource*, 9>(p); ReadRemote<void*, 8>(p);
    expected = 16; p->Reset();
    LocalProxy proxy;
    void** table = *reinterpret_cast<void***>(&proxy);
    WriteProxy<unsigned long long, 7>(&proxy, table); WriteProxy<float, 6>(&proxy, table);
    WriteProxy<double, 5>(&proxy, table); WriteProxy<unsigned int, 4>(&proxy, table);
    WriteProxy<int, 3>(&proxy, table); WriteProxy<ID3D11Resource*, 2>(&proxy, table);
    WriteProxy<ID3D12Resource*, 1>(&proxy, table); WriteProxy<void*, 0>(&proxy, table);
    ReadProxy<unsigned long long, 15>(&proxy, table); ReadProxy<float, 14>(&proxy, table);
    ReadProxy<double, 13>(&proxy, table); ReadProxy<unsigned int, 12>(&proxy, table);
    ReadProxy<int, 11>(&proxy, table); ReadProxy<ID3D11Resource*, 10>(&proxy, table);
    ReadProxy<ID3D12Resource*, 9>(&proxy, table); ReadProxy<void*, 8>(&proxy, table);
    expected = 16; reinterpret_cast<void (*)(LocalProxy*)>(table[16])(&proxy);
    if (calls != 34) return 6;
    std::printf("NGX Microsoft x64 ABI: %u bidirectional native-slot calls passed (no NVIDIA runtime)\n", calls);
}
