#include <windows.h>
#include <cstdio>

using AddonInit = bool (*)(HMODULE, HMODULE);
using SetAccept = void (*)(BOOL);
using RegisteredAddon = HMODULE (*)();
using EventCount = unsigned (*)();

static int Fail(int line, const char *message) {
    std::fprintf(stderr, "line %d: %s\n", line, message);
    return line;
}
#define CHECK(x) do { if (!(x)) return Fail(__LINE__, #x); } while (false)

int wmain(int argc, wchar_t **argv) {
    CHECK(argc == 3);
    HMODULE reshade = LoadLibraryW(argv[1]);
    CHECK(reshade != nullptr);
    auto set_accept = reinterpret_cast<SetAccept>(GetProcAddress(reshade, "ReShadeTestSetAccept"));
    auto registered = reinterpret_cast<RegisteredAddon>(GetProcAddress(reshade, "ReShadeTestRegisteredAddon"));
    auto event_count = reinterpret_cast<EventCount>(GetProcAddress(reshade, "ReShadeTestEventCount"));
    CHECK(set_accept != nullptr && registered != nullptr && event_count != nullptr);

    // DllMain must not race the loader's immediate post-LoadLibrary check.
    HMODULE carrier = LoadLibraryW(argv[2]);
    CHECK(carrier != nullptr && registered() == nullptr && event_count() == 0);
    auto init = reinterpret_cast<AddonInit>(GetProcAddress(carrier, "AddonInit"));
    CHECK(init != nullptr);

    // Exercise the actual failed-registration/unload/reload path first.
    set_accept(FALSE);
    CHECK(!init(carrier, reshade));
    CHECK(registered() == nullptr && event_count() == 0);
    CHECK(FreeLibrary(carrier));
    const wchar_t *leaf = wcsrchr(argv[2], L'\\');
    leaf = leaf == nullptr ? argv[2] : leaf + 1;
    CHECK(GetModuleHandleW(leaf) == nullptr);

    carrier = LoadLibraryW(argv[2]);
    CHECK(carrier != nullptr && registered() == nullptr);
    init = reinterpret_cast<AddonInit>(GetProcAddress(carrier, "AddonInit"));
    CHECK(init != nullptr);
    set_accept(TRUE);
    CHECK(init(carrier, reshade));
    CHECK(registered() == carrier && event_count() >= 5);
    const unsigned registered_events = event_count();
    CHECK(init(carrier, reshade));
    CHECK(event_count() == registered_events);
    std::puts("carrier AddonInit loader contract: passed (CPU, no GPU/game claim)");
    return 0;
}
