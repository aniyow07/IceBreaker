#include "runtime.h"

#include <chrono>
#include <mutex>

#include "cpu.h"
#include "elf_loader.h"
#include "gl_api.h"
#include "guest_mem.h"
#include "hle.h"
#include "jni_bridge.h"
#include "zip_reader.h"

namespace compat {

GuestEntryPoints g_entry;

namespace {
std::mutex g_boot_mu;
bool g_booted = false;
constexpr u32 kCocosDenshionBase = 0x01000000;
constexpr u32 kGameBase = 0x02000000;
}  // namespace

bool booted() { return g_booted; }

bool boot(JavaVM* vm, BootSource src) {
    std::lock_guard<std::mutex> lk(g_boot_mu);
    if (g_booted) return true;
    auto t0 = std::chrono::steady_clock::now();

    if (src.cocosdenshion_bytes.empty() &&
        !zip_read(src.apk_path, src.cocosdenshion_member, src.cocosdenshion_bytes)) {
        LOGE("cannot read %s from %s", src.cocosdenshion_member.c_str(), src.apk_path.c_str());
        return false;
    }
    if (src.game_bytes.empty() && !zip_read(src.apk_path, src.game_member, src.game_bytes)) {
        LOGE("cannot read %s from %s", src.game_member.c_str(), src.apk_path.c_str());
        return false;
    }
    if (!mem_init()) return false;
    cpu_init();
    if (!gl_api_load(g_gl)) {
        LOGE("OpenGL ES 1.1 entry points unavailable");
        return false;
    }
    hle_register_libc();
    hle_register_stdio();
    hle_register_sys();
    hle_register_zlib();
    hle_register_gl();
    hle_install_kuser_helpers();
    jni_init(vm);

    Module* cd = load_module("libcocosdenshion.so", src.cocosdenshion_bytes, kCocosDenshionBase);
    link_module(*cd, [](const char* n, bool f) { return hle_resolve(n, f); });
    Module* game = load_module("libgame.so", src.game_bytes, kGameBase);
    link_module(*game, [cd](const char* n, bool f) {
        if (u32 a = module_symbol(*cd, n)) return a;
        return hle_resolve(n, f);
    });

    hle_install_game_hooks(*game);

    // Same order as Android: the dependency is constructed and JNI_OnLoad'ed first.
    init_module(*cd);
    if (u32 onload = module_symbol(*cd, "JNI_OnLoad")) call_guest(onload, {guest_vm(), 0});
    init_module(*game);
    u32 onload = module_symbol(*game, "JNI_OnLoad");
    if (!onload) { LOGE("libgame.so has no JNI_OnLoad"); return false; }
    u32 ver = static_cast<u32>(call_guest(onload, {guest_vm(), 0}));
    LOGI("guest JNI_OnLoad returned %08x", ver);

#define COMPAT_RESOLVE(cls, m)                                                                \
    g_entry.cls##_##m = module_symbol(*game, "Java_org_cocos2dx_lib_" #cls "_" #m);           \
    if (!g_entry.cls##_##m) LOGW("guest entry point %s.%s not found", #cls, #m);
    COMPAT_NATIVES(COMPAT_RESOLVE)
#undef COMPAT_RESOLVE

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    LOGI("original libraries booted in %lld ms", static_cast<long long>(ms));
    g_booted = true;
    return true;
}

}  // namespace compat
