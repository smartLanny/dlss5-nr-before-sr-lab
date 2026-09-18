// Included inside nr_source_runtime, after the Recording/Resource aliases.
// Value-only legacy-state evidence. Every method, including const Get and its
// relative MRU hint, requires the caller's existing EXCLUSIVE source_lock.
// Capacity, first-free admission order, and conservative invalidation match the
// former array. No allocation, eviction, COM reference or native GPU state here.
class SourceStateTable {
    struct Entry {Recording recording{};Resource resource{};unsigned state=0;};
    static constexpr unsigned capacity=256, buckets=512, list_buckets=256;
    std::array<Entry,capacity> entries_{};
    std::array<unsigned,buckets> index_{}; // slot + 1; zero terminates a probe
    // Exact-key home and current index position survive cluster relocation.
    // A Reset removes its members rather than rebuilding unrelated recordings.
    std::array<unsigned,capacity> home_{},position_{},list_next_{},list_previous_{};
    std::array<unsigned,list_buckets> list_heads_{};
    unsigned first_free_=0;
    mutable unsigned recent_=capacity;
    static unsigned Hash(Recording r,Resource v) noexcept {
        Id h=r.list;
        for(Id n:{r.lifetime,r.reset,r.device,v.address,v.lifetime,v.device,Id(v.subresource)})
            h=(h^(n+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2)))*0xbf58476d1ce4e5b9ULL;
        h^=h>>32;return static_cast<unsigned>(h)&(buckets-1);
    }
    static unsigned ListHash(Id list,Id lifetime) noexcept {
        Id h=(list^(lifetime+0x9e3779b97f4a7c15ULL+(list<<6)+(list>>2)))*0xbf58476d1ce4e5b9ULL;
        h^=h>>32;return static_cast<unsigned>(h)&(list_buckets-1);
    }
    unsigned Find(Recording r,Resource v,unsigned& bucket,bool locate_bucket=false) const noexcept {
        if(recent_<capacity) {
            const auto& e=entries_[recent_];
            if(e.recording==r&&e.resource==v) {
                if(locate_bucket)bucket=position_[recent_];
                return recent_;
            }
        }
        bucket=Hash(r,v);
        for(unsigned n=0;n<buckets;++n,bucket=(bucket+1)&(buckets-1)) {
            const unsigned number=index_[bucket];
            // PERF7_STATE_PROBE: temporary test instrumentation only.
            if(!number)return capacity;
            const auto& e=entries_[number-1];
            if(e.recording==r&&e.resource==v){recent_=number-1;return number-1;}
        }
        return capacity; // impossible at the fixed <= 50% occupied index
    }
    void EraseSlot(unsigned slot) noexcept {
        const auto r=entries_[slot].recording;
        const unsigned next=list_next_[slot],previous=list_previous_[slot];
        if(previous)list_next_[previous-1]=next;
        else list_heads_[ListHash(r.list,r.lifetime)]=next;
        if(next)list_previous_[next-1]=previous;
        list_next_[slot]=list_previous_[slot]=0;
        // Backward-shift only keys whose probe path crosses the hole. Moving a
        // bucket never moves its value slot or changes first-free admission.
        unsigned hole=position_[slot];
        for(unsigned probe=(hole+1)&(buckets-1);index_[probe];probe=(probe+1)&(buckets-1)) {
            const unsigned number=index_[probe],moving=number-1;
            // PERF8_STATE_SHIFT: temporary test instrumentation only.
            if(((probe-home_[moving])&(buckets-1))>=((probe-hole)&(buckets-1))) {
                index_[hole]=number;position_[moving]=hole;hole=probe;
            }
        }
        index_[hole]=0;
        entries_[slot]={};home_[slot]=position_[slot]=0;
        first_free_=std::min(first_free_,slot);
        if(recent_==slot)recent_=capacity;
    }
    void EraseListMembers(Recording r,bool exact_recording) noexcept {
        unsigned number=list_heads_[ListHash(r.list,r.lifetime)];
        while(number) {
            const unsigned slot=number-1;
            number=list_next_[slot]; // unlinking must not skip a colliding member
            const auto& entry=entries_[slot];
            // PERF8_STATE_MEMBER: temporary test instrumentation only.
            if(entry.recording.list==r.list&&entry.recording.lifetime==r.lifetime&&
               (!exact_recording||entry.recording==r))EraseSlot(slot);
        }
    }
public:
    bool Get(Recording r,Resource v,unsigned& state) const noexcept {
        // Preserve the old array's empty-key observation for diagnostic callers;
        // valid native recording paths never use this case for admission.
        if(!r||!v) {
            for(const auto& e:entries_)if(e.recording==r&&e.resource==v){state=e.state;return true;}
            return false;
        }
        unsigned bucket=0;const auto i=Find(r,v,bucket);
        if(i==capacity)return false;state=entries_[i].state;return true;
    }
    bool Put(Recording r,Resource v,unsigned state) noexcept {
        if(!r||!v)return false;
        unsigned bucket=0;auto i=Find(r,v,bucket);
        if(i==capacity) {
            if(first_free_==capacity)return false; // never recycle live evidence
            i=first_free_;index_[bucket]=i+1;position_[i]=bucket;home_[i]=Hash(r,v);
            const unsigned list_bucket=ListHash(r.list,r.lifetime);
            list_next_[i]=list_heads_[list_bucket];list_previous_[i]=0;
            if(list_next_[i])list_previous_[list_next_[i]-1]=i+1;
            list_heads_[list_bucket]=i+1;
            do{++first_free_;}while(first_free_<capacity&&entries_[first_free_].recording);
        }
        entries_[i]={r,v,state};recent_=i;return true;
    }
    void Erase(Recording r,Resource v) noexcept {
        unsigned bucket=0;const auto i=Find(r,v,bucket,true);
        if(i!=capacity)EraseSlot(i);
    }
    template<class Predicate> void EraseIf(Predicate predicate) noexcept {
        // Rare resource/list-address retirement can span several lifetimes.
        // It keeps the exact predicate without rehashing unaffected entries.
        for(unsigned i=0;i<capacity;++i)
            if(entries_[i].recording&&predicate(entries_[i]))EraseSlot(i);
    }
    void EraseRecording(Recording r) noexcept {EraseListMembers(r,true);}
    void EraseListLifetime(Recording r) noexcept {EraseListMembers(r,false);}
    void EraseResourceAddress(Id address) noexcept {
        EraseIf([&](const Entry& e){return e.resource.address==address;});
    }
    void EraseListAddress(Id address) noexcept {
        EraseIf([&](const Entry& e){return e.recording.list==address;});
    }
};
