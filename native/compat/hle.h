#pragma once
#include <string>

#include "cpu.h"

namespace compat {

// Registers a host implementation for an imported function or data object.
void hle_func(const char* name, HleFn fn);
void hle_data(const char* name, u32 guest_addr);
// Returns the guest address for an import, or 0 if unknown.
u32 hle_resolve(const char* name, bool is_function);

// Groups of import implementations (each registers its own names).
void hle_register_libc();
void hle_register_stdio();
void hle_register_sys();
void hle_register_zlib();
void hle_register_gl();
void hle_install_kuser_helpers();
struct Module;
void hle_install_game_hooks(const Module& game);

// Guest errno for the calling thread.
void set_guest_errno(int e);

// Optional translation of guest file paths to host paths (used by the PC harness;
// identity on Android because the guest sees the app's real paths).
void set_path_mapper(std::string (*fn)(const char*));
std::string map_path(const char* guest_path);

// Reads variadic arguments the way the guest pushed them.
class VaReader {
public:
    VaReader(Ctx& c, int first_slot) : ctx_(&c), slot_(first_slot) {}
    explicit VaReader(u32 va_list_ptr) : mem_(va_list_ptr) {}
    u32 next32();
    u64 next64();
    double nextd() { u64 b = next64(); double d; std::memcpy(&d, &b, 8); return d; }

private:
    Ctx* ctx_ = nullptr;
    int slot_ = 0;
    u32 mem_ = 0;
};

// printf-style formatting of a guest format string with guest varargs.
std::string guest_format(const char* fmt, VaReader& va);

}  // namespace compat
