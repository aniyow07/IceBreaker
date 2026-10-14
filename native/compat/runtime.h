#pragma once
#include <jni.h>

#include <string>
#include <vector>

#include "common.h"

namespace compat {

// Guest addresses of the original JNI entry points, filled by boot().
struct GuestEntryPoints {
#define COMPAT_NATIVES(X)                                                                      \
    X(Cocos2dxAccelerometer, onSensorChanged) X(Cocos2dxActivity, confirmDelete)               \
    X(Cocos2dxActivity, doTransition) X(Cocos2dxActivity, isAmazonStore)                       \
    X(Cocos2dxActivity, nativeSetPaths) X(Cocos2dxActivity, purchaseConfirmed)                 \
    X(Cocos2dxActivity, recheckAchievements) X(Cocos2dxActivity, restorePurchase)              \
    X(Cocos2dxActivity, setPressed) X(Cocos2dxActivity, setPrice)                              \
    X(Cocos2dxBitmap, nativeInitBitmapDC) X(Cocos2dxRenderer, nativeDeleteBackward)            \
    X(Cocos2dxRenderer, nativeGetContentText) X(Cocos2dxRenderer, nativeInit)                  \
    X(Cocos2dxRenderer, nativeInsertText) X(Cocos2dxRenderer, nativeKeyDown)                   \
    X(Cocos2dxRenderer, nativeOnPause) X(Cocos2dxRenderer, nativeOnResume)                     \
    X(Cocos2dxRenderer, nativeRender) X(Cocos2dxRenderer, nativeTouchesBegin)                  \
    X(Cocos2dxRenderer, nativeTouchesCancel) X(Cocos2dxRenderer, nativeTouchesEnd)             \
    X(Cocos2dxRenderer, nativeTouchesMove) X(Cocos2dxRenderer, setMetrics)
#define COMPAT_ENTRY_FIELD(cls, m) u32 cls##_##m = 0;
    COMPAT_NATIVES(COMPAT_ENTRY_FIELD)
#undef COMPAT_ENTRY_FIELD
};
extern GuestEntryPoints g_entry;

// Where the original 32-bit libraries come from.
struct BootSource {
    std::string apk_path;               // read lib bytes from this APK ...
    std::string cocosdenshion_member;   // ... at these member paths
    std::string game_member;
    std::vector<u8> cocosdenshion_bytes;  // or supply the bytes directly
    std::vector<u8> game_bytes;
};

// Loads, links and initialises the original libraries and runs their JNI_OnLoad.
// Safe to call repeatedly; only the first call does work.
bool boot(JavaVM* vm, BootSource src);
bool booted();

}  // namespace compat
