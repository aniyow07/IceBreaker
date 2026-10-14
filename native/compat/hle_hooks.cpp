// Function-level substitutions inside the original libgame.so, applied in memory only
// (the file is never modified). Each one keeps the original function's contract and
// output; it only removes cost the emulator cannot avoid otherwise.
//
// cocos2d::CCFileUtils::getFileDataFromZip(const char* zip, const char* name, unsigned long* size)
//   Original (Ghidra pseudocode of libgame.so 0x20c924, analysis/decomp/libgame.so_0021c924.c):
//   unzOpen -> unzLocateFile(name, case-sensitive) -> unzGetCurrentFileInfo -> new[] size ->
//   unzReadCurrentFile -> *size = uncompressed size; returns NULL with *size = 0 on failure.
//   minizip's lookup reads the APK central directory a byte at a time, about 8 million emulated
//   fread calls per level load on the Galaxy S23 (tests/device-s23/arm64). The substitute looks the
//   entry up in an index built once and returns the same bytes in a guest-heap buffer (the game
//   frees it with delete[], which ends in free()).
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include "elf_loader.h"
#include "guest_mem.h"
#include "hle.h"
#include "zip_reader.h"

namespace compat {

namespace {

std::mutex g_zip_mu;
std::map<std::string, std::unique_ptr<ZipIndex>> g_zips;

void h_getFileDataFromZip(Ctx& c) {
    u32 zip = c.arg(0), name = c.arg(1), size_out = c.arg(2);
    wr32(size_out, 0);
    if (!zip || !name || !*HS(zip)) { c.ret(0); return; }
    std::lock_guard<std::mutex> lk(g_zip_mu);
    std::unique_ptr<ZipIndex>& idx = g_zips[HS(zip)];
    if (!idx) {
        idx = std::make_unique<ZipIndex>();
        if (!idx->open(map_path(HS(zip)))) {
            LOGE("cannot index %s", HS(zip));
            idx.reset();
            c.ret(0);
            return;
        }
    }
    const ZipIndex::Entry* e = idx->find(HS(name));
    if (!e) { c.ret(0); return; }
    u32 buf = gmalloc(e->usize ? e->usize : 1);
    if (!buf || !idx->extract(*e, HP<u8>(buf))) {
        LOGE("cannot extract %s", HS(name));
        gfree(buf);
        c.ret(0);
        return;
    }
    wr32(size_out, e->usize);
    c.ret(buf);
}

// Redirects a Thumb function entry to an ARM SVC stub:
//   [nop]  bx pc ; nop ; (ARM) ldr pc, [pc, #-4] ; .word stub
bool redirect_thumb(u32 entry_with_thumb_bit, u32 fn_size, u32 stub) {
    u32 a = entry_with_thumb_bit & ~1u;
    u32 need = ((a & 2) ? 2u : 0u) + 12u;
    if (fn_size < need) return false;
    if (a & 2) {
        wr16(a, 0x46C0);  // nop, to reach 4-byte alignment
        a += 2;
    }
    wr16(a, 0x4778);          // bx pc -> ARM state at a+4
    wr16(a + 2, 0x46C0);      // nop
    wr32(a + 4, 0xE51FF004);  // ldr pc, [pc, #-4]  (loads the word at a+8)
    wr32(a + 8, stub);
    return true;
}

bool hooks_disabled() {
#if defined(__ANDROID__)
    char v[PROP_VALUE_MAX] = {};
    return __system_property_get("debug.icebreaker.nohooks", v) > 0 && v[0] == '1';
#else
    return std::getenv("COMPAT_NO_HOOKS") != nullptr;
#endif
}

}  // namespace

void hle_install_game_hooks(const Module& game) {
    if (hooks_disabled()) {
        LOGW("function substitutions disabled by property");
        return;
    }
    u32 fn = module_symbol(game, "_ZN7cocos2d11CCFileUtils18getFileDataFromZipEPKcS2_Pm");
    if (!fn) {
        LOGW("getFileDataFromZip not found; zip substitution skipped");
        return;
    }
    u32 stub = make_stub("getFileDataFromZip", h_getFileDataFromZip);
    LOGI("zip lookup substitution %s", redirect_thumb(fn, 172, stub) ? "installed" : "NOT installed");
}

}  // namespace compat
