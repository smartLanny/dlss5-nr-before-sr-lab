#pragma once
// Included after Windows/D3D declarations; safe inside the Core private namespace.
// C-linkage counters are shared with the separately compiled spatial module.
namespace nr_aux_shader {
enum class Shader : unsigned { PresentConvert, PresentInitColor, PresentInitGuides,
    PresentFullscreen, PresentBlit, DualDownsample, DualResidual, NativeGuides, RrDepth, Count };
enum class Operation : unsigned { Compile, Pipeline, Count };
// Flat snapshot: [shader][operation][calls, failures, measured, total QPC ticks, max QPC ticks].
inline constexpr unsigned kMetrics=5;
inline constexpr unsigned kSnapshotWords=unsigned(Shader::Count)*unsigned(Operation::Count)*kMetrics;
inline HRESULT Load(const unsigned char* data,SIZE_T size,ID3DBlob** output){
    if(!output)return E_INVALIDARG;*output=nullptr;if(!data||!size)return E_FAIL;
    const HRESULT result=D3DCreateBlob(size,output);
    if(SUCCEEDED(result)){
#if defined(CINTERFACE)
        CopyMemory((*output)->lpVtbl->GetBufferPointer(*output),data,size);
#else
        CopyMemory((*output)->GetBufferPointer(),data,size);
#endif
    }
    return result;
}
#if NR_AUX_TIMING
extern "C" void NrAuxShaderObserve(unsigned,unsigned,unsigned long long,bool,bool);
extern "C" unsigned NrAuxShaderSnapshot(unsigned long long*,unsigned);
extern "C" unsigned long long NrAuxShaderFrequency();
#endif
template<class Fn> HRESULT Measure(Shader shader,Operation operation,Fn function){
#if NR_AUX_TIMING
    LARGE_INTEGER begin{},end{};const bool started=QueryPerformanceCounter(&begin)!=FALSE;
    const HRESULT result=function();
    const bool measured=QueryPerformanceCounter(&end)!=FALSE&&started&&end.QuadPart>=begin.QuadPart;
    NrAuxShaderObserve(unsigned(shader),unsigned(operation),measured?static_cast<unsigned long long>(end.QuadPart-begin.QuadPart):0,
        measured,SUCCEEDED(result));
    return result;
#else
    (void)shader;(void)operation;return function();
#endif
}
inline unsigned Snapshot(unsigned long long* words,unsigned capacity){
#if NR_AUX_TIMING
    return NrAuxShaderSnapshot(words,capacity);
#else
    (void)words;(void)capacity;return 0;
#endif
}
inline unsigned long long Frequency(){
#if NR_AUX_TIMING
    return NrAuxShaderFrequency();
#else
    return 0;
#endif
}
}
