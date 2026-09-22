#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <array>
#include <algorithm>
#include <limits>
#include <mutex>
#include "nr_aux_shader_runtime.hpp"
namespace nr_aux_shader {
namespace {
std::mutex mutex;
std::array<unsigned long long,kSnapshotWords> counters{};
void Add(unsigned long long& value,unsigned long long increment){
    value=increment>std::numeric_limits<unsigned long long>::max()-value?
        std::numeric_limits<unsigned long long>::max():value+increment;
}
}
extern "C" void NrAuxShaderObserve(unsigned shader,unsigned operation,unsigned long long ticks,bool measured,bool success){
    if(shader>=unsigned(Shader::Count)||operation>=unsigned(Operation::Count))return;
    std::lock_guard<std::mutex> lock(mutex);
    auto* record=counters.data()+(shader*unsigned(Operation::Count)+operation)*kMetrics;
    Add(record[0],1);if(!success)Add(record[1],1);
    if(measured){Add(record[2],1);Add(record[3],ticks);record[4]=std::max(record[4],ticks);}
}
extern "C" unsigned NrAuxShaderSnapshot(unsigned long long* words,unsigned capacity){
    if(!words||capacity<kSnapshotWords)return 0;
    std::lock_guard<std::mutex> lock(mutex);std::copy(counters.begin(),counters.end(),words);return kSnapshotWords;
}
extern "C" unsigned long long NrAuxShaderFrequency(){
    LARGE_INTEGER frequency{};return QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0?
        static_cast<unsigned long long>(frequency.QuadPart):0;
}
}
