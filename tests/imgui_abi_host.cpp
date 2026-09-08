// CPU-only host compiled for the Windows MSVC ABI, without a CRT or entrypoint.
struct ImVec2 {
    float x, y;
    ImVec2() : x(0), y(0) {}
    ImVec2(float a, float b) : x(a), y(b) {}
};
static_assert(sizeof(ImVec2) == 8, "The regression is the 8-byte vector return ABI");
extern "C" __declspec(dllexport) ImVec2 HostVec2() {
    return ImVec2(13.25f, -7.5f);
}
extern "C" __declspec(dllexport) ImVec2 HostMixed(int a, double b, void* p, float c) {
    if (a != 17 || b != 2.5 || p != reinterpret_cast<void*>(0x12340) || c != 4.25f)
        return ImVec2(-99.0f, -98.0f);
    return ImVec2(static_cast<float>(a + b), c + 8.0f);
}
extern "C" { int _fltused = 0; } // Linker marker; no CRT code is required.
