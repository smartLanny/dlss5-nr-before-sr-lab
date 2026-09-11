#pragma once

// Include after imgui.h. ReShade's function table belongs to an MSVC-ABI DLL.
// On Win64, MSVC returns ImVec2 (8 bytes, user-defined constructors) through
// a hidden first output pointer. MinGW instead expects the value in RAX.
// Keep that foreign ABI at this boundary; returning the local value to our
// own callers uses the client's normal ABI. ImVec2 inputs by reference and
// scalar/reference returns do not require this adapter.
namespace nr_imgui_abi {
template <typename... Args>
inline ImVec2 CallHostVec2(ImVec2 (*function)(Args...), Args... args) {
    static_assert(sizeof(ImVec2) == 8, "Review the host vector-return ABI if ImVec2 changes");
#if defined(__MINGW32__) && defined(_WIN64)
    ImVec2 result;
    using HostCall = void (*)(ImVec2 *, Args...);
    reinterpret_cast<HostCall>(function)(&result, args...);
    return result;
#else
    return function(args...);
#endif
}
}
