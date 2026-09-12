#include "nr_processing_route.h"
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <iostream>
#include <cassert>
struct Config {bool enabled=true,identity_use_original=false;float transfer_strength=1;
    nr_processing::Start processing_start=nr_processing::Start::Before;
    unsigned long long processing_selection=1;} g_config;
int g_chain_cs=0;
bool TryEnterCriticalSection(int*) {return true;}
void LeaveCriticalSection(int*) {}
std::atomic<unsigned> g_history_generation{1};
void Log(const char*,...) {}
namespace nr_source_runtime {
unsigned long long token=0;
unsigned long long CurrentRetryToken(){return token;}
}
namespace nr_compat_present_runtime {
bool present_done=true,native_done=true;
bool InlineDrainReady(){return present_done;}
bool NativeNrDrained(){return native_done;}
}
#include "../src/nr_processing_runtime.inl"
int main() {
    namespace R=nr_processing_runtime;
    using nr_processing::Start;
    R::Reconcile();
    nr_source_runtime::token=1;
    {R::CarrierScope c;R::Reject(Start::Before,"before");R::Reject(Start::After,"after");
        R::MarkRecorded();R::Adopted(Start::Present);}
    const auto history=g_history_generation.load();
    nr_compat_present_runtime::present_done=false;
    for(unsigned i=2;i<=34;++i){nr_source_runtime::token=i;R::BeginSource(i);
        R::CarrierScope c;assert(R::Effective()==Start::Present);}
    assert(R::session.NativeProbePending());
    assert(!R::session.Ready(Start::Present,34));
    assert(g_history_generation.load()==history);
    nr_compat_present_runtime::present_done=true;
    nr_compat_present_runtime::native_done=false;
    nr_source_runtime::token=35;{R::CarrierScope c;assert(R::Effective()==Start::Present);}
    nr_compat_present_runtime::native_done=true;
    // Never promote by merely repeating a native call for an old source.
    {R::CarrierScope c;assert(R::Effective()==Start::Present);}
    nr_source_runtime::token=36;
    {R::CarrierScope c;
        assert(R::Effective()==Start::Before);
        assert(g_history_generation.load()==history+1);
        assert(!R::Recorded());
        // Native preflight still fails normally and can select After/Present.
        R::Reject(Start::Before,"still unsupported");
        R::Reject(Start::After,"still unsupported");
        R::MarkRecorded();
        R::Reject(Start::Present,"cannot reject recorded");assert(R::Recorded());}
    assert(g_config.processing_start==Start::Before&&g_config.processing_selection==1);
    assert(!R::private_recorded&&!R::source_blocked&&R::carrier_serial==0);
    std::cout<<"actual production runtime: drain/claim/selection integration passed\n";
}
