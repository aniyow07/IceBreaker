// ARM32 build only: a thin armeabi-v7a "libgame.so" in front of the original, unmodified
// libgame.so (shipped as libicebreaker_original.so in the same directory).
//
// Why: on Android 13+ the system can pause the activity (notification-permission prompt,
// old-target warning) before the GL surface exists. The original Cocos2dxRenderer then calls
// nativeOnPause before nativeInit, and the original code dereferences engine objects that do
// not exist yet (SIGSEGV in Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause, observed on a
// Galaxy S23 / Android 16, tests/device-s23/arm32). This wrapper forwards every native method
// unchanged, except that renderer and touch calls are dropped until the first nativeInit.
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <atomic>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "IcebreakerShim", __VA_ARGS__)

namespace {

void* g_orig = nullptr;
std::atomic<bool> g_started{false};

template <typename F> F orig(const char* name) {
    void* p = g_orig ? dlsym(g_orig, name) : nullptr;
    if (!p) LOGE("original symbol %s missing", name);
    return reinterpret_cast<F>(p);
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    Dl_info info{};
    dladdr(reinterpret_cast<void*>(&JNI_OnLoad), &info);
    // Same directory as this library; the original links libcocosdenshion.so from there too.
    char path[1024] = {};
    if (info.dli_fname) {
        const char* slash = nullptr;
        for (const char* p = info.dli_fname; *p; ++p)
            if (*p == '/') slash = p;
        if (slash) {
            size_t n = static_cast<size_t>(slash - info.dli_fname) + 1;
            if (n + 32 < sizeof path) {
                for (size_t i = 0; i < n; ++i) path[i] = info.dli_fname[i];
                const char* f = "libicebreaker_original.so";
                for (size_t i = 0; f[i]; ++i) path[n + i] = f[i];
            }
        }
    }
    g_orig = dlopen(path[0] ? path : "libicebreaker_original.so", RTLD_NOW);
    if (!g_orig) {
        LOGE("cannot load original library: %s", dlerror());
        return JNI_ERR;
    }
    auto onload = orig<jint (*)(JavaVM*, void*)>("JNI_OnLoad");
    return onload ? onload(vm, reserved) : JNI_VERSION_1_4;
}

#define FWD0(cls, m, guarded)                                                                        \
    JNIEXPORT void JNICALL Java_org_cocos2dx_lib_##cls##_##m(JNIEnv* e, jclass c) {                  \
        if (guarded && !g_started.load()) return;                                                    \
        static auto f = orig<void (*)(JNIEnv*, jclass)>("Java_org_cocos2dx_lib_" #cls "_" #m);       \
        if (f) f(e, c);                                                                              \
    }
FWD0(Cocos2dxActivity, confirmDelete, false)
FWD0(Cocos2dxActivity, doTransition, false)
FWD0(Cocos2dxActivity, recheckAchievements, false)
FWD0(Cocos2dxActivity, setPressed, false)
FWD0(Cocos2dxRenderer, nativeDeleteBackward, true)
FWD0(Cocos2dxRenderer, nativeOnPause, true)
FWD0(Cocos2dxRenderer, nativeOnResume, true)
FWD0(Cocos2dxRenderer, nativeRender, true)
#undef FWD0

JNIEXPORT jboolean JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_isAmazonStore(JNIEnv* e, jclass c) {
    static auto f = orig<jboolean (*)(JNIEnv*, jclass)>("Java_org_cocos2dx_lib_Cocos2dxActivity_isAmazonStore");
    return f ? f(e, c) : JNI_FALSE;
}
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths(JNIEnv* e, jclass c, jstring s) {
    static auto f = orig<void (*)(JNIEnv*, jclass, jstring)>("Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths");
    if (f) f(e, c, s);
}
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_purchaseConfirmed(JNIEnv* e, jclass c, jint i, jstring s) {
    static auto f = orig<void (*)(JNIEnv*, jclass, jint, jstring)>("Java_org_cocos2dx_lib_Cocos2dxActivity_purchaseConfirmed");
    if (f) f(e, c, i, s);
}
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_restorePurchase(JNIEnv* e, jclass c, jstring s) {
    static auto f = orig<void (*)(JNIEnv*, jclass, jstring)>("Java_org_cocos2dx_lib_Cocos2dxActivity_restorePurchase");
    if (f) f(e, c, s);
}
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_setPrice(JNIEnv* e, jclass c, jstring a, jstring b) {
    static auto f = orig<void (*)(JNIEnv*, jclass, jstring, jstring)>("Java_org_cocos2dx_lib_Cocos2dxActivity_setPrice");
    if (f) f(e, c, a, b);
}
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxAccelerometer_onSensorChanged(JNIEnv* e, jclass c, jfloat x, jfloat y,
                                                                                   jfloat z, jlong t) {
    static auto f = orig<void (*)(JNIEnv*, jclass, jfloat, jfloat, jfloat, jlong)>(
        "Java_org_cocos2dx_lib_Cocos2dxAccelerometer_onSensorChanged");
    if (f) f(e, c, x, y, z, t);
}
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC(JNIEnv* e, jclass c, jint w, jint h,
                                                                               jbyteArray p) {
    static auto f = orig<void (*)(JNIEnv*, jclass, jint, jint, jbyteArray)>("Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC");
    if (f) f(e, c, w, h, p);
}
JNIEXPORT jstring JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeGetContentText(JNIEnv* e, jclass c) {
    static auto f = orig<jstring (*)(JNIEnv*, jclass)>("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeGetContentText");
    return f ? f(e, c) : nullptr;
}
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit(JNIEnv* e, jclass c, jint w, jint h) {
    static auto f = orig<void (*)(JNIEnv*, jclass, jint, jint)>("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");
    if (f) f(e, c, w, h);
    g_started.store(true);
}
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInsertText(JNIEnv* e, jclass c, jstring s) {
    static auto f = orig<void (*)(JNIEnv*, jclass, jstring)>("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInsertText");
    if (f) f(e, c, s);
}
JNIEXPORT jboolean JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown(JNIEnv* e, jclass c, jint k) {
    static auto f = orig<jboolean (*)(JNIEnv*, jclass, jint)>("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown");
    return (f && g_started.load()) ? f(e, c, k) : JNI_FALSE;
}
#define TOUCH1(m)                                                                                              \
    JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_##m(JNIEnv* e, jclass c, jint i, jfloat x, jfloat y) { \
        if (!g_started.load()) return;                                                                         \
        static auto f = orig<void (*)(JNIEnv*, jclass, jint, jfloat, jfloat)>("Java_org_cocos2dx_lib_Cocos2dxRenderer_" #m); \
        if (f) f(e, c, i, x, y);                                                                               \
    }
TOUCH1(nativeTouchesBegin)
TOUCH1(nativeTouchesEnd)
#undef TOUCH1
#define TOUCHN(m)                                                                                              \
    JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_##m(JNIEnv* e, jclass c, jintArray i, jfloatArray x, \
                                                                      jfloatArray y) {                         \
        if (!g_started.load()) return;                                                                         \
        static auto f = orig<void (*)(JNIEnv*, jclass, jintArray, jfloatArray, jfloatArray)>(                  \
            "Java_org_cocos2dx_lib_Cocos2dxRenderer_" #m);                                                     \
        if (f) f(e, c, i, x, y);                                                                               \
    }
TOUCHN(nativeTouchesMove)
TOUCHN(nativeTouchesCancel)
#undef TOUCHN
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_setMetrics(JNIEnv* e, jclass c, jint w, jint h) {
    static auto f = orig<void (*)(JNIEnv*, jclass, jint, jint)>("Java_org_cocos2dx_lib_Cocos2dxRenderer_setMetrics");
    if (f) f(e, c, w, h);
}

}  // extern "C"
