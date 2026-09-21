// Included within the existing Core namespace. No second graphics observer.
struct LifecycleQueue;
namespace nr_edit_core {
inline thread_local unsigned private_image_depth=0;
inline bool PrivateImageActive() noexcept{return private_image_depth!=0;}
struct ImageScope {ImageScope(){++private_image_depth;}~ImageScope(){--private_image_depth;}};
bool PrivateSrCall() noexcept;
void CollectLocked();
void DrainRetirements();
void SubmitLocked(unsigned long long,LifecycleQueue*,unsigned long long);
void InvalidateLocked(unsigned long long,bool);
}
