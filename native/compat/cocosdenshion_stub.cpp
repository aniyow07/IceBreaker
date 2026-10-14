// arm64 stand-in for libcocosdenshion.so. The original Java code calls
// System.loadLibrary("cocosdenshion") before loading "game"; the real ARM32
// libcocosdenshion.so runs inside the compatibility layer (libgame.so).
#include <jni.h>

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_4; }
