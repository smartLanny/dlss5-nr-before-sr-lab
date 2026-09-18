// Included inside nr_source_runtime, after the Recording/Resource aliases.
// Value-only legacy-state evidence. Every method, including const Get and its
// relative MRU hint, requires the caller's existing EXCLUSIVE source_lock.
// Capacity, first-free admission order, and conservative invalidation match the
// former array. No allocation, eviction, COM reference or native GPU state here.
class SourceStateTable {
    struct Entry {Recording recording{};Resource resource{};unsigned state=0;};
    static constexpr unsigned capacity=256, buckets=512;
    std::array<Entry,capacity> entries_{};
    std::array<unsigned,buckets> index_{}; // slot + 1; zero terminates a probe
    unsigned first_free_=0;
    mutable unsigned recent_=capacity;
    static unsigned Hash(Recording r,Resource v) noexcept {
        Id h=r.list;
        for(Id n:{r.lifetime,r.reset,r.device,v.address,v.lifetime,v.device,Id(v.subresource)})
            h=(h^(n+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2)))*0xbf58476d1ce4e5b9ULL;
        h^=h>>32;return static_cast<unsigned>(h)&(buckets-1);
    }
    unsigned Find(Recording r,Resource v,unsigned& bucket,bool locate_bucket=false) const noexcept {
        // Avoid hashing eight identity fields repeatedly for a single hot key.
        // Erase requires the real bucket; Get/Put only need the validated slot.
        if(!locate_bucket&&recent_<capacity) {
            const auto& e=entries_[recent_];
            if(e.recording==r&&e.resource==v)return recent_;
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
    void Rebuild() noexcept {
        index_.fill(0);
        for(unsigned i=0;i<capacity;++i)if(entries_[i].recording) {
            unsigned b=Hash(entries_[i].recording,entries_[i].resource);
            while(index_[b])b=(b+1)&(buckets-1);
            index_[b]=i+1;
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
            i=first_free_;index_[bucket]=i+1;
            do{++first_free_;}while(first_free_<capacity&&entries_[first_free_].recording);
        }
        entries_[i]={r,v,state};recent_=i;return true;
    }
    void Erase(Recording r,Resource v) noexcept {
        unsigned bucket=0;const auto i=Find(r,v,bucket,true);
        if(i==capacity)return;
        entries_[i]={};first_free_=std::min(first_free_,i);
        // Repair the open-addressed cluster in place; no tombstone accumulation.
        index_[bucket]=0;
        for(unsigned b=(bucket+1)&(buckets-1);index_[b];b=(b+1)&(buckets-1)) {
            const unsigned number=index_[b];index_[b]=0;
            unsigned dst=Hash(entries_[number-1].recording,entries_[number-1].resource);
            while(index_[dst])dst=(dst+1)&(buckets-1);
            index_[dst]=number;
        }
    }
    template<class Predicate> void EraseIf(Predicate predicate) noexcept {
        bool changed=false;
        for(unsigned i=0;i<capacity;++i)if(entries_[i].recording&&predicate(entries_[i])) {
            entries_[i]={};first_free_=std::min(first_free_,i);changed=true;
        }
        if(changed)Rebuild(); // lifetime/aliasing boundary, not per transition
    }
    void EraseRecording(Recording r) noexcept {
        EraseIf([&](const Entry& e){return e.recording==r;});
    }
    void EraseListLifetime(Recording r) noexcept {
        EraseIf([&](const Entry& e){return e.recording.list==r.list&&e.recording.lifetime==r.lifetime;});
    }
    void EraseResourceAddress(Id address) noexcept {
        EraseIf([&](const Entry& e){return e.resource.address==address;});
    }
    void EraseListAddress(Id address) noexcept {
        EraseIf([&](const Entry& e){return e.recording.list==address;});
    }
};
