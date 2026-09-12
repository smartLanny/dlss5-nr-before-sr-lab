// Included after ChainConfig, atomics and chain lock; no GPU work here.
namespace nr_processing_runtime {
using Start=nr_processing::Start;
nr_processing::Session session;
std::atomic<unsigned> effective{0};
std::atomic<bool> work_enabled{false};
std::atomic<unsigned long long> next_selection{1};
thread_local bool private_recorded=false,source_blocked=false;
thread_local unsigned long long carrier_serial=0;
void Reconcile() {
    work_enabled.store(g_config.enabled && !g_config.identity_use_original && std::isfinite(g_config.transfer_strength) && g_config.transfer_strength>0.000001f,std::memory_order_release);
    if(session.Select(g_config.processing_start,g_config.processing_selection)) {
        effective.store(static_cast<unsigned>(session.Current()),std::memory_order_release);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
    }
}
Start Effective(){return static_cast<Start>(effective.load(std::memory_order_acquire));}
bool PresentSelected(){return Effective()==Start::Present;}
void MarkRecorded(){private_recorded=true;}
void BlockSource(){source_blocked=true;}
bool Recorded(){return private_recorded||source_blocked;}
void Reject(Start route,const char* reason,bool structural=false) {
    if(Recorded())return;
    char prior[128]{};std::strncpy(prior,session.Reason(),sizeof(prior)-1);
    const bool moved=session.Reject(route,carrier_serial,reason,structural);
    if(moved) {
        effective.store(static_cast<unsigned>(session.Current()),std::memory_order_release);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
    }
    if(moved||std::strcmp(prior,session.Reason())!=0) {
        Log("[processing-route] requested=%s actual=%s reason=%s; possible compatibility bug: press F8 and submit logs. Saved preference retained.",
            nr_processing::Name(session.Requested()),nr_processing::Name(session.Current()),session.Reason());
    }
}
void RejectBeforePresentWork(unsigned long long source,const char* reason){
    const auto old_serial=carrier_serial;const bool old_recorded=private_recorded,old_blocked=source_blocked;
    carrier_serial=source;private_recorded=source_blocked=false;
    Reject(Start::Present,reason,true);
    carrier_serial=old_serial;private_recorded=old_recorded;source_blocked=old_blocked;
}
void Adopted(Start route){session.Adopted(route);}
void MissingInlineAtPresent(unsigned long long source) {
    const auto old_serial=carrier_serial;const bool old_recorded=private_recorded,old_blocked=source_blocked;
    carrier_serial=source;private_recorded=source_blocked=false;
    if(Effective()==Start::Before)Reject(Start::Before,"native-reconstruction-entry-unavailable");
    if(Effective()==Start::After)Reject(Start::After,"native-reconstruction-entry-unavailable");
    carrier_serial=old_serial;private_recorded=old_recorded;source_blocked=old_blocked;
}
struct CarrierScope {
    bool previous_recorded,previous_blocked;
    unsigned long long previous_serial;
    CarrierScope():previous_recorded(private_recorded),previous_blocked(source_blocked),previous_serial(carrier_serial) {
        Reconcile();private_recorded=false;source_blocked=false;
        carrier_serial=nr_source_runtime::CurrentRetryToken();
    }
    ~CarrierScope(){private_recorded=previous_recorded;source_blocked=previous_blocked;carrier_serial=previous_serial;}
};
}
