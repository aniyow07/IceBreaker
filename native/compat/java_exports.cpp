// Java -> guest forwarding. The original classes.dex declares these natives; this
// library provides them and forwards each call into the original ARM32 implementation.
#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <string>

#include "cpu.h"
#include "jni_bridge.h"
#include "runtime.h"

using namespace compat;

namespace {

JavaVM* g_vm = nullptr;
std::string g_apk_path;
// The original code dereferences engine objects that only exist after the first
// nativeInit. Modern Android can pause the activity (permission prompt, old-target
// warning) before the GL surface exists, which crashed the unmodified ARM32 build on
// a Galaxy S23 (tests/device-s23/arm32). Renderer calls that arrive before the first
// nativeInit are therefore not forwarded.
bool g_engine_started = false;

constexpr const char* kOrigCocosDenshion = "assets/original-armeabi/libcocosdenshion.so";
constexpr const char* kOrigGame = "assets/original-armeabi/libgame.so";

std::string find_own_apk() {
#if defined(__linux__)
    // The runtime maps the installed APK; pick the one belonging to this package.
    FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return {};
    char line[1024];
    std::string found;
    while (std::fgets(line, sizeof line, f)) {
        const char* p = std::strchr(line, '/');
        if (!p) continue;
        std::string path(p);
        while (!path.empty() && (path.back() == '\n' || path.back() == ' ')) path.pop_back();
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".apk") == 0 &&
            path.find("com.nitrome.icebreaker") != std::string::npos) {
            found = path;
            break;
        }
    }
    std::fclose(f);
    return found;
#else
    return {};
#endif
}

bool ensure_booted(JNIEnv* env, const char* apk_hint = nullptr) {
    if (booted()) return true;
    if (apk_hint && *apk_hint) g_apk_path = apk_hint;
    if (g_apk_path.empty()) g_apk_path = find_own_apk();
    if (!g_vm) env->GetJavaVM(&g_vm);
    BootSource src;
    src.apk_path = g_apk_path;
    src.cocosdenshion_member = kOrigCocosDenshion;
    src.game_member = kOrigGame;
    NativeFrame frame(env);
    if (!boot(g_vm, std::move(src))) {
        env->ThrowNew(env->FindClass("java/lang/UnsatisfiedLinkError"),
                      "Icebreaker compatibility layer could not load the original game libraries");
        return false;
    }
    return true;
}

#define ENTER(env)                    \
    if (!ensure_booted(env)) return;  \
    NativeFrame frame(env)
#define ENTER_R(env, r)               \
    if (!ensure_booted(env)) return r; \
    NativeFrame frame(env)
#define ENTER_STARTED(env)                                \
    if (!g_engine_started || !ensure_booted(env)) return; \
    NativeFrame frame(env)

GuestArgs base(jclass cls) { return GuestArgs().w(guest_env()).w(to_guest(cls)); }

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    return JNI_VERSION_1_4;
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths(JNIEnv* env, jclass cls, jstring apk) {
    const char* p = apk ? env->GetStringUTFChars(apk, nullptr) : nullptr;
    std::string path = p ? p : "";
    if (p) env->ReleaseStringUTFChars(apk, p);
    if (!ensure_booted(env, path.c_str())) return;
    NativeFrame frame(env);
    call_guest_args(g_entry.Cocos2dxActivity_nativeSetPaths, base(cls).w(to_guest(apk)));
}

#define VOID0(cls, m)                                                                       \
    JNIEXPORT void JNICALL Java_org_cocos2dx_lib_##cls##_##m(JNIEnv* env, jclass c) {       \
        ENTER(env);                                                                         \
        call_guest_args(g_entry.cls##_##m, base(c));                                        \
    }
VOID0(Cocos2dxActivity, confirmDelete)
VOID0(Cocos2dxActivity, doTransition)
VOID0(Cocos2dxActivity, recheckAchievements)
VOID0(Cocos2dxActivity, setPressed)
#undef VOID0
#define VOID0_STARTED(cls, m)                                                                   JNIEXPORT void JNICALL Java_org_cocos2dx_lib_##cls##_##m(JNIEnv* env, jclass c) {               ENTER_STARTED(env);                                                                         call_guest_args(g_entry.cls##_##m, base(c));                                            }
VOID0_STARTED(Cocos2dxRenderer, nativeDeleteBackward)
VOID0_STARTED(Cocos2dxRenderer, nativeOnPause)
VOID0_STARTED(Cocos2dxRenderer, nativeOnResume)
#undef VOID0_STARTED

// Frame pacing. The original Cocos2dxRenderer.onDrawFrame sleeps 2 x (interval - elapsed)
// after each frame (REA/JADX evidence ev_a3c9c47b...). With the fast emulated frame and a 120 Hz
// display that alternates short and long frames (measured p50 49-55 ms frame-to-frame on the S23
// at the game's requested 1/30 s). Holding each nativeRender until the requested interval has
// passed since its own start makes Java's sleep zero, so frames arrive evenly at the
// rate the game itself asked for.
void pace_frame(std::chrono::steady_clock::time_point frame_start) {
    double interval = g_animation_interval_ms.load();
    if (interval <= 0) return;
    std::this_thread::sleep_until(frame_start + std::chrono::microseconds(static_cast<long long>(interval * 1000.0)));
}

// nativeRender also records how long each emulated frame takes (logged every 120 frames).
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender(JNIEnv* env, jclass c) {
    ENTER_STARTED(env);
    static std::vector<float> samples, gaps;
    static auto window_start = std::chrono::steady_clock::now();
    static auto prev = std::chrono::steady_clock::now();
    auto t0 = std::chrono::steady_clock::now();
    if (samples.empty()) window_start = t0;
    else gaps.push_back(std::chrono::duration<float, std::milli>(t0 - prev).count());
    prev = t0;
    call_guest_args(g_entry.Cocos2dxRenderer_nativeRender, base(c));
    pace_frame(t0);
    samples.push_back(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count());
    if (samples.size() == 120) {
        float wall = std::chrono::duration<float>(std::chrono::steady_clock::now() - window_start).count();
        std::vector<float> s = samples;
        std::sort(s.begin(), s.end());
        float sum = 0;
        for (float v : s) sum += v;
        LOGI("PERF %.1f fps; nativeRender over %zu frames: avg %.2f ms, p50 %.2f, p95 %.2f, max %.2f", s.size() / wall,
             s.size(), sum / s.size(), s[s.size() / 2], s[s.size() * 95 / 100], s.back());
        std::sort(gaps.begin(), gaps.end());
        if (!gaps.empty())
            LOGI("PERF frame-to-frame: p50 %.1f ms, p95 %.1f ms, max %.1f ms", gaps[gaps.size() / 2],
                 gaps[gaps.size() * 95 / 100], gaps.back());
        samples.clear();
        gaps.clear();
        cpu_stats_dump("PERF 120 frames");
    }
}

JNIEXPORT jboolean JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_isAmazonStore(JNIEnv* env, jclass c) {
    ENTER_R(env, JNI_FALSE);
    return static_cast<jboolean>(call_guest_args(g_entry.Cocos2dxActivity_isAmazonStore, base(c)) & 0xFF);
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_purchaseConfirmed(JNIEnv* env, jclass c, jint i, jstring s) {
    ENTER(env);
    call_guest_args(g_entry.Cocos2dxActivity_purchaseConfirmed, base(c).w(static_cast<u32>(i)).w(to_guest(s)));
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_restorePurchase(JNIEnv* env, jclass c, jstring s) {
    ENTER(env);
    call_guest_args(g_entry.Cocos2dxActivity_restorePurchase, base(c).w(to_guest(s)));
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_setPrice(JNIEnv* env, jclass c, jstring a, jstring b) {
    ENTER(env);
    call_guest_args(g_entry.Cocos2dxActivity_setPrice, base(c).w(to_guest(a)).w(to_guest(b)));
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxAccelerometer_onSensorChanged(JNIEnv* env, jclass c, jfloat x,
                                                                                   jfloat y, jfloat z, jlong ts) {
    ENTER(env);
    call_guest_args(g_entry.Cocos2dxAccelerometer_onSensorChanged, base(c).f(x).f(y).f(z).l(static_cast<u64>(ts)));
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC(JNIEnv* env, jclass c, jint w, jint h,
                                                                               jbyteArray px) {
    ENTER(env);
    call_guest_args(g_entry.Cocos2dxBitmap_nativeInitBitmapDC,
                    base(c).w(static_cast<u32>(w)).w(static_cast<u32>(h)).w(to_guest(px)));
}

JNIEXPORT jstring JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeGetContentText(JNIEnv* env, jclass c) {
    ENTER_R(env, nullptr);
    u32 h = static_cast<u32>(call_guest_args(g_entry.Cocos2dxRenderer_nativeGetContentText, base(c)));
    jobject o = to_host(h);
    return o ? static_cast<jstring>(env->NewLocalRef(o)) : nullptr;
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit(JNIEnv* env, jclass c, jint w, jint h) {
    ENTER(env);
    call_guest_args(g_entry.Cocos2dxRenderer_nativeInit, base(c).w(static_cast<u32>(w)).w(static_cast<u32>(h)));
    g_engine_started = true;
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInsertText(JNIEnv* env, jclass c, jstring s) {
    ENTER(env);
    call_guest_args(g_entry.Cocos2dxRenderer_nativeInsertText, base(c).w(to_guest(s)));
}

JNIEXPORT jboolean JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown(JNIEnv* env, jclass c, jint k) {
    ENTER_R(env, JNI_FALSE);
    return static_cast<jboolean>(call_guest_args(g_entry.Cocos2dxRenderer_nativeKeyDown, base(c).w(static_cast<u32>(k))) & 0xFF);
}

#define TOUCH1(m)                                                                                           \
    JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_##m(JNIEnv* env, jclass c, jint id, jfloat x, \
                                                                      jfloat y) {                          \
        ENTER_STARTED(env);                                                                                       \
        call_guest_args(g_entry.Cocos2dxRenderer_##m, base(c).w(static_cast<u32>(id)).f(x).f(y));          \
    }
TOUCH1(nativeTouchesBegin)
TOUCH1(nativeTouchesEnd)
#undef TOUCH1

#define TOUCHN(m)                                                                                           \
    JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_##m(JNIEnv* env, jclass c, jintArray ids,   \
                                                                      jfloatArray xs, jfloatArray ys) {    \
        ENTER_STARTED(env);                                                                                       \
        call_guest_args(g_entry.Cocos2dxRenderer_##m, base(c).w(to_guest(ids)).w(to_guest(xs)).w(to_guest(ys))); \
    }
TOUCHN(nativeTouchesMove)
TOUCHN(nativeTouchesCancel)
#undef TOUCHN

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_setMetrics(JNIEnv* env, jclass c, jint w, jint h) {
    ENTER(env);
    call_guest_args(g_entry.Cocos2dxRenderer_setMetrics, base(c).w(static_cast<u32>(w)).w(static_cast<u32>(h)));
}

}  // extern "C"

namespace compat {
// Lets a test harness boot from local files before any Java call arrives.
void harness_set_vm(JavaVM* vm) { g_vm = vm; }
}  // namespace compat
