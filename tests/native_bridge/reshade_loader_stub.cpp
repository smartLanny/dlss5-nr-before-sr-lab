#include <windows.h>
#include <cstdint>

static bool g_accept = false;
static HMODULE g_registered = nullptr;
static unsigned g_events = 0;

extern "C" __declspec(dllexport) void ReShadeTestSetAccept(BOOL accept) {
    g_accept = accept != FALSE;
}
extern "C" __declspec(dllexport) HMODULE ReShadeTestRegisteredAddon() {
    return g_registered;
}
extern "C" __declspec(dllexport) unsigned ReShadeTestEventCount() {
    return g_events;
}
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(HMODULE addon, std::uint32_t) {
    if (!g_accept) return false;
    g_registered = addon;
    return true;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(HMODULE addon) {
    if (g_registered == addon) g_registered = nullptr;
}
extern "C" __declspec(dllexport) void ReShadeRegisterEvent(unsigned, void *) {
    ++g_events;
}
