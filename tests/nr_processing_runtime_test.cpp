// Production route adapter; command admission and GPU work are explicit seams.
#include "nr_processing_route.h"
#include "nr_fg_presentation.h"
#include <atomic>
#include <cmath>
#include <cassert>
#include <iostream>
struct Config {bool enabled=true,identity_use_original=false;float transfer_strength=1;
    nr_processing::Start processing_start=nr_processing::Start::After;
    unsigned long long processing_selection=1;}g_config;
std::atomic<unsigned> g_history_generation{1};
nr_fg_presentation::Policy g_fg_presentation_policy;
int g_chain_cs=0;
bool TryEnterCriticalSection(int*){return true;}
void LeaveCriticalSection(int*){}
void Log(const char*,...){}
namespace nr_source_runtime {unsigned long long source=1;unsigned long long CurrentRetryToken(){return source;}}
namespace nr_compat_present_runtime {
bool InlineDrainReady(){return true;}
bool NativeNrDrained(){return true;}
}
#include "../src/nr_processing_runtime.inl"
int main(){
    using namespace nr_processing_runtime;
    {CarrierScope first;assert(Effective()==Start::After);
        Reject(Start::After,"command-state-unverified");assert(PresentSelected());
        BeginSource(1);assert(PresentSelected()); // No earlier entry on this source.
    }
    nr_source_runtime::source=2;
    {CarrierScope next;assert(Effective()==Start::After); // D13/D15 regression: no manual reselection.
        MarkRecorded();Reject(Start::After,"late error");assert(Effective()==Start::After);
    }
    nr_source_runtime::source=3;
    {CarrierScope next;assert(Effective()==Start::After);
        Reject(Start::After,"unavailable");assert(PresentSelected());
        MarkRecorded(); // The actual fallback has started private work.
    }
    for(unsigned source=4;source<12;++source){nr_source_runtime::source=source;
        CarrierScope later;assert(PresentSelected());}
    g_config.processing_selection++;
    nr_source_runtime::source=12;
    {CarrierScope selected;assert(Effective()==Start::After);}
    // Source refusal after a skipped inline frame does not itself retain Present.
    RejectBeforePresentWork(12,"source-unverified");
    nr_processing::Session backoff;backoff.Select(Start::After,1);
    for(unsigned source=1;source<=3;++source){backoff.BeginSource(source);
        assert(backoff.Current()==Start::After);backoff.Reject(Start::After,source,"temporary");}
    for(unsigned source=4;source<11;++source){backoff.BeginSource(source);assert(backoff.Current()==Start::Present);}
    backoff.BeginSource(11);assert(backoff.Current()==Start::After);
    backoff.Reject(Start::After,11,"temporary");backoff.Adopted(Start::Present);
    backoff.BeginSource(1000);assert(backoff.Current()==Start::Present);
    backoff.BeginSource(10);assert(backoff.Current()==Start::Present); // Late receipt cannot promote.
    nr_processing::Session structural;structural.BeginSource(1);
    structural.Reject(Start::Before,1,"unavailable",true);
    assert(structural.Retained()==Start::Before);
    structural.BeginSource(5);assert(structural.Current()==Start::Before);
    std::cout<<"PASS production route adapter: transient recovery, same-source exclusion, work retention and bounded backoff\n";
}
