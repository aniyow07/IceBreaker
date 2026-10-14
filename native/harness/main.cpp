// PC proof-of-concept harness: runs the original, unmodified ARM32 libgame.so and
// libcocosdenshion.so through the compatibility layer (dynarmic x86-64 backend),
// with an embedded JVM providing the Java classes the game calls back into,
// desktop OpenGL standing in for GLES 1.1, scripted touches and screenshots.
#include <windows.h>

#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "harness.h"
#include "hle.h"
#include "runtime.h"

extern "C" {
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths(JNIEnv*, jclass, jstring);
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit(JNIEnv*, jclass, jint, jint);
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender(JNIEnv*, jclass);
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin(JNIEnv*, jclass, jint, jfloat, jfloat);
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd(JNIEnv*, jclass, jint, jfloat, jfloat);
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause(JNIEnv*, jclass);
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnResume(JNIEnv*, jclass);
}
namespace compat { void harness_set_vm(JavaVM* vm); }

using namespace compat;

namespace {

std::string g_data_dir;

std::string harness_paths(const char* p) {
    std::string s = p ? p : "";
    const std::string prefix = "/data/data/com.nitrome.icebreaker";
    if (s.compare(0, prefix.size(), prefix) == 0) return g_data_dir + s.substr(prefix.size());
    return s;
}

std::vector<u8> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<u8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Touch {
    int frame;
    float x, y;
    int hold_frames;
};

}  // namespace

int main(int argc, char** argv) {
    int w = 1280, h = 720, frames = 600;
    std::string root = "C:/REA", out = "C:/REA/tests/poc1", apk = "C:/REA/dist/Icebreaker-ARM32.apk";
    std::string jdk = "C:/Program Files/Eclipse Adoptium/jdk-25.0.4.101-hotspot";
    std::vector<int> shots;
    std::vector<Touch> touches;
    std::vector<int> pause_frames;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&] { return std::string(i + 1 < argc ? argv[++i] : ""); };
        if (a == "--size") { std::string v = next(); std::sscanf(v.c_str(), "%dx%d", &w, &h); }
        else if (a == "--frames") frames = std::stoi(next());
        else if (a == "--out") out = next();
        else if (a == "--shot") shots.push_back(std::stoi(next()));
        else if (a == "--touch") { Touch t{0, 0, 0, 3}; std::sscanf(next().c_str(), "%d:%f:%f", &t.frame, &t.x, &t.y); touches.push_back(t); }
        else if (a == "--pause") pause_frames.push_back(std::stoi(next()));
    }
    CreateDirectoryA(out.c_str(), nullptr);
    g_data_dir = out + "/data";
    CreateDirectoryA(g_data_dir.c_str(), nullptr);
    set_path_mapper(harness_paths);

    if (!harness_gl_create(w, h)) { LOGE("could not create the OpenGL context"); return 2; }

    HMODULE jvm_dll = LoadLibraryA((jdk + "/bin/server/jvm.dll").c_str());
    auto create_vm = reinterpret_cast<jint(JNICALL*)(JavaVM**, void**, void*)>(GetProcAddress(jvm_dll, "JNI_CreateJavaVM"));
    if (!create_vm) { LOGE("cannot load jvm.dll"); return 3; }
    std::string cp = "-Djava.class.path=" + root + "/build/harness-java";
    JavaVMOption opt[1];
    opt[0].optionString = const_cast<char*>(cp.c_str());
    JavaVMInitArgs vmargs{};
    vmargs.version = JNI_VERSION_1_8;
    vmargs.nOptions = 1;
    vmargs.options = opt;
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    if (create_vm(&vm, reinterpret_cast<void**>(&env), &vmargs) != JNI_OK) { LOGE("JNI_CreateJavaVM failed"); return 3; }
    harness_set_vm(vm);

    BootSource src;
    src.cocosdenshion_bytes = read_file(root + "/game_data/lib/armeabi/libcocosdenshion.so");
    src.game_bytes = read_file(root + "/game_data/lib/armeabi/libgame.so");
    auto t0 = std::chrono::steady_clock::now();
    if (!boot(vm, std::move(src))) { LOGE("boot failed"); return 4; }

    jclass activity = env->FindClass("org/cocos2dx/lib/Cocos2dxActivity");
    jclass renderer = env->FindClass("org/cocos2dx/lib/Cocos2dxRenderer");
    if (!activity || !renderer) { LOGE("harness Java classes missing"); return 5; }
    auto local = [&](jclass c) { return static_cast<jclass>(env->NewLocalRef(c)); };

    Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths(env, local(activity), env->NewStringUTF(apk.c_str()));
    Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit(env, local(renderer), w, h);
    auto t_init = std::chrono::steady_clock::now();
    LOGI("HARNESS boot+nativeInit: %lld ms",
         static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(t_init - t0).count()));

    std::vector<double> frame_ms;
    std::vector<unsigned char> px;
    auto frame_period = std::chrono::microseconds(16667);
    auto next_frame = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) {
        for (const Touch& t : touches) {
            if (f == t.frame) Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin(env, local(renderer), 0, t.x, t.y);
            if (f == t.frame + t.hold_frames) Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd(env, local(renderer), 0, t.x, t.y);
        }
        for (int p : pause_frames) {
            if (f == p) {
                Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause(env, local(renderer));
                Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnResume(env, local(renderer));
                // Android recreates the GL surface after resume; cocos2d-x 0.12 then calls nativeInit again,
                // which reloads every texture.
                Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit(env, local(renderer), w, h);
            }
        }
        auto a = std::chrono::steady_clock::now();
        Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender(env, local(renderer));
        auto b = std::chrono::steady_clock::now();
        frame_ms.push_back(std::chrono::duration<double, std::milli>(b - a).count());
        for (int s : shots) {
            if (f == s) {
                harness_gl_read(px);
                std::string path = out + "/frame_" + std::to_string(f) + ".png";
                LOGI("HARNESS screenshot %s %s", path.c_str(), png_write(path, w, h, px) ? "ok" : "FAILED");
            }
        }
        next_frame += frame_period;
        std::this_thread::sleep_until(next_frame);
    }
    std::vector<double> sorted = frame_ms;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0;
    for (double v : frame_ms) sum += v;
    LOGI("HARNESS frames=%zu avg=%.2fms p50=%.2fms p95=%.2fms max=%.2fms (first frame %.2fms)", frame_ms.size(),
         sum / frame_ms.size(), sorted[sorted.size() / 2], sorted[sorted.size() * 95 / 100], sorted.back(), frame_ms[0]);
    return 0;
}
