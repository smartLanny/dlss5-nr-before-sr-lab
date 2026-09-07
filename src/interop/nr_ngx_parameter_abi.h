#pragma once
// Windows x64 NGX's parameter object is a Microsoft C++ interface. This is the
// minimal interface already declared by the MIT native carrier, not an NGX
// implementation. No allocation, deletion, RTTI or exception crosses it.
//
// MSVC groups overloaded virtual functions in reverse declaration order.
// Windows-GNU compilers use declaration order. Spell the Windows-GNU group in
// the actual native slot order, including for locally implemented proxies.
// The core's raw ParamSlot access and the carrier must agree on all 17 slots.
static_assert(sizeof(void*) == 8, "NGX consumer ABI is Windows x64 only");
struct NVSDK_NGX_Parameter {
#if defined(_MSC_VER)
    virtual void Set(const char*, unsigned long long) = 0;
    virtual void Set(const char*, float) = 0;
    virtual void Set(const char*, double) = 0;
    virtual void Set(const char*, unsigned int) = 0;
    virtual void Set(const char*, int) = 0;
    virtual void Set(const char*, ID3D11Resource*) = 0;
    virtual void Set(const char*, ID3D12Resource*) = 0;
    virtual void Set(const char*, void*) = 0;
    virtual NVSDK_NGX_Result Get(const char*, unsigned long long*) const = 0;
    virtual NVSDK_NGX_Result Get(const char*, float*) const = 0;
    virtual NVSDK_NGX_Result Get(const char*, double*) const = 0;
    virtual NVSDK_NGX_Result Get(const char*, unsigned int*) const = 0;
    virtual NVSDK_NGX_Result Get(const char*, int*) const = 0;
    virtual NVSDK_NGX_Result Get(const char*, ID3D11Resource**) const = 0;
    virtual NVSDK_NGX_Result Get(const char*, ID3D12Resource**) const = 0;
    virtual NVSDK_NGX_Result Get(const char*, void**) const = 0;
#else
    virtual void Set(const char*, void*) = 0;                    // 0
    virtual void Set(const char*, ID3D12Resource*) = 0;           // 1
    virtual void Set(const char*, ID3D11Resource*) = 0;           // 2
    virtual void Set(const char*, int) = 0;                      // 3
    virtual void Set(const char*, unsigned int) = 0;             // 4
    virtual void Set(const char*, double) = 0;                   // 5
    virtual void Set(const char*, float) = 0;                    // 6
    virtual void Set(const char*, unsigned long long) = 0;       // 7
    virtual NVSDK_NGX_Result Get(const char*, void**) const = 0;                  // 8
    virtual NVSDK_NGX_Result Get(const char*, ID3D12Resource**) const = 0;         // 9
    virtual NVSDK_NGX_Result Get(const char*, ID3D11Resource**) const = 0;         // 10
    virtual NVSDK_NGX_Result Get(const char*, int*) const = 0;                    // 11
    virtual NVSDK_NGX_Result Get(const char*, unsigned int*) const = 0;           // 12
    virtual NVSDK_NGX_Result Get(const char*, double*) const = 0;                 // 13
    virtual NVSDK_NGX_Result Get(const char*, float*) const = 0;                  // 14
    virtual NVSDK_NGX_Result Get(const char*, unsigned long long*) const = 0;     // 15
#endif
    virtual void Reset() = 0;                                    // 16
};
