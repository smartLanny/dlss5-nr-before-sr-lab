// CPU values only. This observer never queries a device, changes settings or
// advances an activity Window while publishing or taking an F8 snapshot.
#define NR_HAS_UI_ACTIVITY_RECEIPTS 1
SRWLOCK g_nr_ui_activity_lock=SRWLOCK_INIT;
std::atomic<nr_ui_activity::Count> g_nr_ui_activity_next_thread{0};
nr_ui_activity::TraceSnapshot g_nr_ui_activity_trace;
unsigned g_nr_ui_activity_recent_next=0;
// Activity counters describe the process, so drawing-thread migration must not
// select an older private sampling window. Never hold this lock while drawing,
// publishing, collecting F8, or calling a graphics API.
SRWLOCK g_nr_ui_activity_window_lock=SRWLOCK_INIT;
nr_ui_activity::Window g_nr_ui_activity_window;
class NrUiActivityWindowGuard {
public:
    NrUiActivityWindowGuard() {AcquireSRWLockExclusive(&g_nr_ui_activity_window_lock);}
    ~NrUiActivityWindowGuard() {ReleaseSRWLockExclusive(&g_nr_ui_activity_window_lock);}
    NrUiActivityWindowGuard(const NrUiActivityWindowGuard&)=delete;
    NrUiActivityWindowGuard& operator=(const NrUiActivityWindowGuard&)=delete;
};

nr_ui_activity::Count NrUiActivityThreadCookie() {
    auto previous=g_nr_ui_activity_next_thread.load(std::memory_order_relaxed);
    while(previous!=UINT64_MAX) {
        if(g_nr_ui_activity_next_thread.compare_exchange_weak(previous,previous+1,
                std::memory_order_relaxed))return previous+1;
    }
    return 0; // Exhausted metadata identity never alters the activity decision.
}
struct NrUiActivityThreadState {
    const nr_ui_activity::Count cookie=NrUiActivityThreadCookie();
    nr_ui_activity::Count calls=0,last_read=0;
    bool read_seen=false;
    struct Displayed {
        nr_ui_activity::Display value=nr_ui_activity::Display::Unavailable;
        bool known=false,valid=false;
    } displayed[static_cast<unsigned>(nr_ui_activity::Site::Count)];
};
NrUiActivityThreadState& NrUiActivityThread() {
    static thread_local NrUiActivityThreadState value;
    return value;
}
void NrUiActivityReadIdentity(NrUiActivityThreadState& thread,nr_ui_activity::ReadReceipt& receipt,
                            nr_ui_activity::Count now) {
    if(thread.calls!=UINT64_MAX)++thread.calls;
    receipt.thread_cookie=thread.cookie;receipt.thread_call=thread.calls;
    receipt.read_gap_known=thread.read_seen && now>=thread.last_read;
    if(receipt.read_gap_known)receipt.read_gap_ms=now-thread.last_read;
    thread.last_read=now;thread.read_seen=true;
}
void NrPublishUiActivity(const nr_ui_activity::ReadReceipt& read,nr_ui_activity::Site site,
                        nr_ui_activity::Display display) {
    using namespace nr_ui_activity;
    const auto index=static_cast<unsigned>(site);
    if(index>=static_cast<unsigned>(Site::Count))return;
    auto& previous=NrUiActivityThread().displayed[index];
    Publication value{};value.available=true;value.read=read;value.site=site;value.display=display;
    value.previous=previous.value;value.previous_known=previous.known;value.previous_valid=previous.valid;
    value.transition=read.observation.reset_mask || !previous.known ||
        previous.value!=display || previous.valid!=read.observation.sample.valid;
    previous={display,true,read.observation.sample.valid};
    AcquireSRWLockExclusive(&g_nr_ui_activity_lock);
    if(g_nr_ui_activity_trace.published!=UINT64_MAX)++g_nr_ui_activity_trace.published;
    value.sequence=g_nr_ui_activity_trace.published;
    g_nr_ui_activity_trace.latest[index]=value;
    if(value.transition) {
        g_nr_ui_activity_trace.recent[g_nr_ui_activity_recent_next]=value;
        g_nr_ui_activity_recent_next=(g_nr_ui_activity_recent_next+1)%kRecentReceipts;
        if(g_nr_ui_activity_trace.recent_count<kRecentReceipts)++g_nr_ui_activity_trace.recent_count;
        else if(g_nr_ui_activity_trace.overwritten!=UINT64_MAX)++g_nr_ui_activity_trace.overwritten;
    }
    ReleaseSRWLockExclusive(&g_nr_ui_activity_lock);
}
void SnapshotNrUiActivity(nr_ui_activity::TraceSnapshot& output) {
    AcquireSRWLockShared(&g_nr_ui_activity_lock);
    output=g_nr_ui_activity_trace;
    const auto first=output.recent_count==nr_ui_activity::kRecentReceipts?g_nr_ui_activity_recent_next:0u;
    for(unsigned i=0;i<output.recent_count;++i)
        output.recent[i]=g_nr_ui_activity_trace.recent[(first+i)%nr_ui_activity::kRecentReceipts];
    ReleaseSRWLockShared(&g_nr_ui_activity_lock);
}
