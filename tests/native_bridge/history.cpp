#include "src/interop/nr_native_bridge_history.h"
#include "src/interop/nr_native_bridge_abi.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while(false)
int main() {
    NrNativeBridgeStatusV1 s;
    CHECK(!NrNativeBridgeStatusAllows(s));
    s.reason=NrNativeBridgeReason::Ready;
    CHECK(NrNativeBridgeStatusAllows(s));
    s.observerEpoch=1; CHECK(!NrNativeBridgeStatusAllows(s)); s.observerEpoch=2;
    s.abi++; CHECK(!NrNativeBridgeStatusAllows(s)); s.abi=kNrNativeBridgeAbi;
    s.size++; CHECK(!NrNativeBridgeStatusAllows(s)); s.size=sizeof(s);
    s.reserved=1; CHECK(!NrNativeBridgeStatusAllows(s));
    NrNativeBridgeHistory h;
    CHECK(!h.Admit(0,true)); CHECK(!h.Admit(1,false));
    CHECK(h.Admit(1,true)); CHECK(!h.NeedsReset(1)); h.Mirrored(1);
    CHECK(h.NeedsReset(1));
    h.NativeCompleted(1,false,true); CHECK(h.NeedsReset(1));
    h.NativeCompleted(1,true,false); CHECK(h.NeedsReset(1));
    h.NativeCompleted(1,true,true); CHECK(!h.NeedsReset(1));
    h.Mirrored(1); h.Created(1); CHECK(!h.NeedsReset(1));
    for (std::uintptr_t n=1; n<=16; ++n) { CHECK(h.Admit(n,true)); h.Mirrored(n); }
    CHECK(!h.Admit(17,true)); CHECK(h.NeedsReset(1));
    h.Created(8); CHECK(h.Admit(17,true)); CHECK(h.NeedsReset(9));
    // Different handles retain independent gaps; failed fallbacks cannot clear.
    for (std::uintptr_t n=1; n<=16; ++n) if (n!=8) {
        CHECK(h.NeedsReset(n)); h.NativeCompleted(n,true,true); CHECK(!h.NeedsReset(n));
    }
    std::puts("native bridge ABI/history: passed");
}
