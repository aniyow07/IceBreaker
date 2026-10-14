#pragma once
#include <jni.h>

#include <atomic>
#include <vector>

#include "common.h"

namespace compat {

// Builds the guest-visible JavaVM and JNIEnv (function tables of SVC stubs) and
// maps host references to 32-bit guest handles.
void jni_init(JavaVM* host_vm);

// Last frame interval the game requested via Cocos2dxRenderer.setAnimationInterval (0 = none yet).
extern std::atomic<double> g_animation_interval_ms;
u32 guest_vm();
u32 guest_env();

// Host JNIEnv for the calling thread (set on every Java->native entry, or attached).
JNIEnv* host_env();

// Handle conversion. Objects passed into the guest during a native call are
// local to that call, like real JNI local references.
u32 to_guest(jobject o);
jobject to_host(u32 handle);

// Scope for one Java->guest native call: sets the thread's JNIEnv and releases
// the local handles created during the call when it ends.
class NativeFrame {
public:
    explicit NativeFrame(JNIEnv* env);
    ~NativeFrame();
    NativeFrame(const NativeFrame&) = delete;
    NativeFrame& operator=(const NativeFrame&) = delete;

private:
    JNIEnv* prev_env_;
    size_t mark_;
};

// Argument list for calling a guest function with the AAPCS soft-float layout.
class GuestArgs {
public:
    GuestArgs& w(u32 v) { slots_.push_back(v); return *this; }
    GuestArgs& f(float v) { return w(bits_from(v)); }
    GuestArgs& l(u64 v) {
        if (slots_.size() & 1) slots_.push_back(0);
        slots_.push_back(u32(v));
        slots_.push_back(u32(v >> 32));
        return *this;
    }
    const std::vector<u32>& slots() const { return slots_; }

private:
    std::vector<u32> slots_;
};
u64 call_guest_args(u32 fn, const GuestArgs& a);

}  // namespace compat
