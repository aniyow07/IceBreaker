// stdio, printf/scanf formatting, file descriptors and stat for the guest.
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <sys/mman.h>
#endif
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "guest_mem.h"
#include "hle.h"

namespace compat {

static std::string (*g_path_mapper)(const char*) = nullptr;
void set_path_mapper(std::string (*fn)(const char*)) { g_path_mapper = fn; }
std::string map_path(const char* p) { return g_path_mapper ? g_path_mapper(p) : std::string(p ? p : ""); }

// ---- printf --------------------------------------------------------------------------
std::string guest_format(const char* fmt, VaReader& va) {
    std::string out;
    char spec[64];
    char buf[512];
    for (const char* p = fmt; *p;) {
        if (*p != '%') { out += *p++; continue; }
        const char* start = p++;
        if (*p == '%') { out += '%'; ++p; continue; }
        std::string flags;
        while (*p && std::strchr("-+ #0'", *p)) flags += *p++;
        std::string width, prec;
        if (*p == '*') { width = std::to_string(static_cast<s32>(va.next32())); ++p; }
        else while (*p >= '0' && *p <= '9') width += *p++;
        bool has_prec = false;
        if (*p == '.') {
            has_prec = true;
            ++p;
            if (*p == '*') { prec = std::to_string(static_cast<s32>(va.next32())); ++p; }
            else while (*p >= '0' && *p <= '9') prec += *p++;
        }
        int lng = 0;  // 0 int, -1 h, -2 hh, 1 l, 2 ll, 3 L
        for (;;) {
            if (*p == 'h') { lng = lng == -1 ? -2 : -1; ++p; }
            else if (*p == 'l') { lng = lng == 1 ? 2 : 1; ++p; }
            else if (*p == 'q' || *p == 'j') { lng = 2; ++p; }
            else if (*p == 'L') { lng = 3; ++p; }
            else if (*p == 'z' || *p == 't') { lng = 0; ++p; }
            else break;
        }
        char conv = *p;
        if (!conv) { out.append(start); break; }
        ++p;
        std::string base = "%" + flags + width + (has_prec ? "." + prec : "");
        switch (conv) {
        case 'd': case 'i': {
            if (lng == 2) {
                std::snprintf(spec, sizeof spec, "%slld", base.c_str());
                std::snprintf(buf, sizeof buf, spec, static_cast<long long>(va.next64()));
            } else {
                s32 v = static_cast<s32>(va.next32());
                if (lng == -1) v = static_cast<short>(v);
                if (lng == -2) v = static_cast<signed char>(v);
                std::snprintf(spec, sizeof spec, "%sd", base.c_str());
                std::snprintf(buf, sizeof buf, spec, v);
            }
            out += buf;
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            if (lng == 2) {
                std::snprintf(spec, sizeof spec, "%sll%c", base.c_str(), conv);
                std::snprintf(buf, sizeof buf, spec, static_cast<unsigned long long>(va.next64()));
            } else {
                u32 v = va.next32();
                if (lng == -1) v = static_cast<u16>(v);
                if (lng == -2) v = static_cast<u8>(v);
                std::snprintf(spec, sizeof spec, "%s%c", base.c_str(), conv);
                std::snprintf(buf, sizeof buf, spec, v);
            }
            out += buf;
            break;
        }
        case 'c': {
            std::snprintf(spec, sizeof spec, "%sc", base.c_str());
            std::snprintf(buf, sizeof buf, spec, static_cast<int>(va.next32() & 0xFF));
            out += buf;
            break;
        }
        case 's': {
            u32 a = va.next32();
            const char* s = a ? HS(a) : "(null)";
            if (lng == 1) s = "";  // %ls is not used by the game
            std::snprintf(spec, sizeof spec, "%ss", base.c_str());
            int n = std::snprintf(nullptr, 0, spec, s);
            std::vector<char> big(static_cast<size_t>(n) + 1);
            std::snprintf(big.data(), big.size(), spec, s);
            out += big.data();
            break;
        }
        case 'p': {
            std::snprintf(buf, sizeof buf, "0x%x", va.next32());
            out += buf;
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
            double d = va.nextd();
            std::snprintf(spec, sizeof spec, "%s%c", base.c_str(), conv);
            int n = std::snprintf(nullptr, 0, spec, d);
            std::vector<char> big(static_cast<size_t>(n) + 1);
            std::snprintf(big.data(), big.size(), spec, d);
            out += big.data();
            break;
        }
        case 'n': {
            u32 a = va.next32();
            if (a) wr32(a, static_cast<u32>(out.size()));
            break;
        }
        default:
            out.append(start, p);
            break;
        }
    }
    return out;
}

namespace {

// ---- FILE mapping ------------------------------------------------------------------------
// Each guest FILE (84 bytes, the 32-bit bionic size) carries a slot index at offset 28
// (_cookie) and a marker at offset 32, so lookups need no lock. Files opened read-only
// are additionally memory-mapped: the original code (minizip inside libgame.so) reads
// the APK's central directory one byte at a time, millions of fread calls per level
// load, and serving those from memory removes most of the per-call cost.
constexpr u32 kFileSize = 84;
constexpr u32 kSlotOff = 28, kMarkOff = 32, kMarker = 0x1CEF11E5;
constexpr u32 kMaxFiles = 4096;
u32 g_sF = 0;  // __sF[3]: stdin, stdout, stderr

struct GFile {
    FILE* h = nullptr;
    const u8* map = nullptr;  // read-only mapping, or null
    size_t size = 0, pos = 0;
    int ungot = -1;
    bool eof = false;
};
std::mutex g_file_mu;
GFile* g_slots[kMaxFiles];

bool is_std(u32 f, int which) { return f == g_sF + which * kFileSize; }

GFile* gfile(u32 f) {
    if (!f || rd32(f + kMarkOff) != kMarker) return nullptr;
    u32 i = rd32(f + kSlotOff);
    return i < kMaxFiles ? g_slots[i] : nullptr;
}
FILE* host_file(u32 f) {
    if (is_std(f, 0)) return stdin;
    GFile* g = gfile(f);
    return g ? g->h : nullptr;
}

// Keeps the guest FILE's _flags (offset 12) EOF/ERR bits in sync for any inlined feof/ferror.
void set_flags(u32 f, bool eof, bool err) {
    u16 fl = rd16(f + 12) & ~u16(0x0020 | 0x0040);
    if (eof) fl |= 0x0020;
    if (err) fl |= 0x0040;
    wr16(f + 12, fl);
}
void sync_flags(u32 f, FILE* h) { set_flags(f, std::feof(h) != 0, std::ferror(h) != 0); }

void try_map(GFile* g, bool read_only) {
#if !defined(_WIN32)
    if (!read_only) return;
    struct stat st;
    if (fstat(fileno(g->h), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) return;
    void* m = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fileno(g->h), 0);
    if (m == MAP_FAILED) return;
    g->map = static_cast<const u8*>(m);
    g->size = static_cast<size_t>(st.st_size);
#else
    (void)g;
    (void)read_only;
#endif
}

u32 wrap_file(FILE* h, bool read_only) {
    if (!h) return 0;
    auto* g = new GFile();
    g->h = h;
    try_map(g, read_only);
    u32 idx = kMaxFiles;
    {
        std::lock_guard<std::mutex> lk(g_file_mu);
        for (u32 i = 0; i < kMaxFiles; ++i)
            if (!g_slots[i]) { idx = i; g_slots[i] = g; break; }
    }
    if (idx == kMaxFiles) { LOGE("guest has too many open files"); std::fclose(h); delete g; return 0; }
    u32 f = gcalloc(1, kFileSize);
    wr16(f + 14, static_cast<u16>(fileno(h)));
    wr32(f + kSlotOff, idx);
    wr32(f + kMarkOff, kMarker);
    return f;
}

void log_text(const std::string& s) {
    if (!s.empty()) LOGI("guest: %s", s.c_str());
}

void write_out(u32 f, const std::string& s, Ctx& c) {
    if (is_std(f, 1) || is_std(f, 2)) { log_text(s); c.ret(static_cast<u32>(s.size())); return; }
    FILE* h = host_file(f);
    if (!h) { c.ret(0xFFFFFFFFu); return; }
    size_t n = std::fwrite(s.data(), 1, s.size(), h);
    sync_flags(f, h);
    c.ret(static_cast<u32>(n));
}

void h_fopen(Ctx& c) {
    std::string path = map_path(HS(c.arg(0)));
    std::string mode = HS(c.arg(1));
    bool read_only = mode.find_first_of("wa+") == std::string::npos;
    if (mode.find('b') == std::string::npos) mode += 'b';  // guest never relies on text translation
    FILE* h = std::fopen(path.c_str(), mode.c_str());
    if (!h) set_guest_errno(errno);
    c.ret(wrap_file(h, read_only));
}
void h_fdopen(Ctx& c) {
    std::string mode = HS(c.arg(1));
    FILE* h = fdopen(static_cast<int>(c.arg(0)), mode.c_str());
    c.ret(wrap_file(h, false));
}
void h_tmpfile(Ctx& c) { c.ret(wrap_file(std::tmpfile(), false)); }
void h_fclose(Ctx& c) {
    u32 f = c.arg(0);
    GFile* g = gfile(f);
    if (!g) { c.ret(0xFFFFFFFFu); return; }
    {
        std::lock_guard<std::mutex> lk(g_file_mu);
        g_slots[rd32(f + kSlotOff)] = nullptr;
    }
    wr32(f + kMarkOff, 0);
#if !defined(_WIN32)
    if (g->map) munmap(const_cast<u8*>(g->map), g->size);
#endif
    int r = std::fclose(g->h);
    delete g;
    gfree(f);
    c.ret(static_cast<u32>(r));
}
void h_fread(Ctx& c) {
    u32 f = c.arg(3);
    GFile* g = gfile(f);
    if (!g) { c.ret(0); return; }
    u32 size = c.arg(1), count = c.arg(2);
    if (g->map) {
        if (!size || !count) { c.ret(0); return; }
        u8* dst = HP<u8>(c.arg(0));
        u64 want = u64(size) * count, got = 0;
        if (g->ungot >= 0) { dst[got++] = static_cast<u8>(g->ungot); g->ungot = -1; }
        size_t avail = g->pos < g->size ? g->size - g->pos : 0;
        u64 take = want - got < avail ? want - got : avail;
        std::memcpy(dst + got, g->map + g->pos, static_cast<size_t>(take));
        g->pos += static_cast<size_t>(take);
        got += take;
        if (got < want) g->eof = true;
        set_flags(f, g->eof, false);
        c.ret(static_cast<u32>(got / size));
        return;
    }
    size_t n = std::fread(H(c.arg(0)), size, count, g->h);
    sync_flags(f, g->h);
    c.ret(static_cast<u32>(n));
}
void h_fwrite(Ctx& c) {
    u32 f = c.arg(3);
    if (is_std(f, 1) || is_std(f, 2)) {
        log_text(std::string(HS(c.arg(0)), static_cast<size_t>(c.arg(1)) * c.arg(2)));
        c.ret(c.arg(2));
        return;
    }
    FILE* h = host_file(f);
    if (!h) { c.ret(0); return; }
    size_t n = std::fwrite(H(c.arg(0)), c.arg(1), c.arg(2), h);
    sync_flags(f, h);
    c.ret(static_cast<u32>(n));
}
void h_fseek(Ctx& c) {
    u32 f = c.arg(0);
    GFile* g = gfile(f);
    if (!g) { c.ret(0xFFFFFFFFu); return; }
    s32 off = static_cast<s32>(c.arg(1));
    int whence = static_cast<int>(c.arg(2));
    if (g->map) {
        long long cur = static_cast<long long>(g->pos) - (g->ungot >= 0 ? 1 : 0);
        long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? cur : static_cast<long long>(g->size);
        long long np = base + off;
        if (np < 0) { set_guest_errno(EINVAL); c.ret(0xFFFFFFFFu); return; }
        g->pos = static_cast<size_t>(np);
        g->ungot = -1;
        g->eof = false;
        set_flags(f, false, false);
        c.ret(0);
        return;
    }
    int r = std::fseek(g->h, off, whence);
    sync_flags(f, g->h);
    c.ret(static_cast<u32>(r));
}
void h_ftell(Ctx& c) {
    GFile* g = gfile(c.arg(0));
    if (!g) { c.ret(0xFFFFFFFFu); return; }
    if (g->map) { c.ret(static_cast<u32>(g->pos - (g->ungot >= 0 ? 1 : 0))); return; }
    c.ret(static_cast<u32>(std::ftell(g->h)));
}
void h_rewind(Ctx& c) {
    GFile* g = gfile(c.arg(0));
    if (!g) return;
    if (g->map) { g->pos = 0; g->ungot = -1; g->eof = false; set_flags(c.arg(0), false, false); return; }
    std::rewind(g->h);
    sync_flags(c.arg(0), g->h);
}
int map_getc(GFile* g) {
    if (g->ungot >= 0) { int v = g->ungot; g->ungot = -1; return v; }
    if (g->pos >= g->size) { g->eof = true; return EOF; }
    return g->map[g->pos++];
}
void h_fgets(Ctx& c) {
    u32 f = c.arg(2);
    GFile* g = gfile(f);
    if (!g) { c.ret(0); return; }
    if (g->map) {
        s32 n = static_cast<s32>(c.arg(1));
        char* dst = HP<char>(c.arg(0));
        s32 i = 0;
        while (i < n - 1) {
            int ch = map_getc(g);
            if (ch == EOF) break;
            dst[i++] = static_cast<char>(ch);
            if (ch == '\n') break;
        }
        if (n > 0) dst[i] = 0;
        set_flags(f, g->eof, false);
        c.ret(i ? c.arg(0) : 0);
        return;
    }
    char* r = std::fgets(HP<char>(c.arg(0)), static_cast<int>(c.arg(1)), g->h);
    sync_flags(f, g->h);
    c.ret(r ? c.arg(0) : 0);
}
void h_getc(Ctx& c) {
    u32 f = c.arg(0);
    GFile* g = gfile(f);
    if (!g) { c.ret(0xFFFFFFFFu); return; }
    if (g->map) { int r = map_getc(g); set_flags(f, g->eof, false); c.ret(static_cast<u32>(r)); return; }
    int r = std::fgetc(g->h);
    sync_flags(f, g->h);
    c.ret(static_cast<u32>(r));
}
void h_ungetc(Ctx& c) {
    GFile* g = gfile(c.arg(1));
    if (!g || static_cast<s32>(c.arg(0)) == EOF) { c.ret(0xFFFFFFFFu); return; }
    if (g->map) { g->ungot = static_cast<u8>(c.arg(0)); g->eof = false; c.ret(static_cast<u8>(c.arg(0))); return; }
    c.ret(static_cast<u32>(std::ungetc(static_cast<int>(c.arg(0)), g->h)));
}
void h_fputc(Ctx& c) {
    char ch = static_cast<char>(c.arg(0));
    write_out(c.arg(1), std::string(1, ch), c);
    if (c.reg(0) == 1) c.ret(static_cast<u8>(ch));
}
void h_fputs(Ctx& c) { write_out(c.arg(1), HS(c.arg(0)), c); }
void h_puts(Ctx& c) { log_text(HS(c.arg(0))); c.ret(1); }
void h_fflush(Ctx& c) {
    if (FILE* h = c.arg(0) ? host_file(c.arg(0)) : nullptr) std::fflush(h);
    c.ret(0);
}
void h_ferror(Ctx& c) {
    GFile* g = gfile(c.arg(0));
    if (!g) { c.ret(1); return; }
    c.ret(g->map ? 0 : static_cast<u32>(std::ferror(g->h)));
}
void h_fileno(Ctx& c) {
    if (is_std(c.arg(0), 1)) { c.ret(1); return; }
    if (is_std(c.arg(0), 2)) { c.ret(2); return; }
    FILE* h = host_file(c.arg(0));
    c.ret(h ? static_cast<u32>(fileno(h)) : 0xFFFFFFFFu);
}
void h_setvbuf(Ctx& c) { c.ret(0); }

void h_printf(Ctx& c) { VaReader va(c, 1); log_text(guest_format(HS(c.arg(0)), va)); c.ret(1); }
void h_vprintf(Ctx& c) { VaReader va(c.arg(1)); log_text(guest_format(HS(c.arg(0)), va)); c.ret(1); }
void h_fprintf(Ctx& c) { VaReader va(c, 2); write_out(c.arg(0), guest_format(HS(c.arg(1)), va), c); }
void h_vfprintf(Ctx& c) { VaReader va(c.arg(2)); write_out(c.arg(0), guest_format(HS(c.arg(1)), va), c); }
void store_formatted(u32 dst, u32 cap, bool bounded, const std::string& s, Ctx& c) {
    if (!bounded) {
        std::memcpy(H(dst), s.c_str(), s.size() + 1);
    } else if (cap) {
        size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
        std::memcpy(H(dst), s.data(), n);
        HP<char>(dst)[n] = 0;
    }
    c.ret(static_cast<u32>(s.size()));
}
void h_sprintf(Ctx& c) { VaReader va(c, 2); store_formatted(c.arg(0), 0, false, guest_format(HS(c.arg(1)), va), c); }
void h_vsprintf(Ctx& c) { VaReader va(c.arg(2)); store_formatted(c.arg(0), 0, false, guest_format(HS(c.arg(1)), va), c); }
void h_snprintf(Ctx& c) {
    VaReader va(c, 3);
    store_formatted(c.arg(0), c.arg(1), true, guest_format(HS(c.arg(2)), va), c);
}
void h_vsnprintf(Ctx& c) {
    VaReader va(c.arg(3));
    store_formatted(c.arg(0), c.arg(1), true, guest_format(HS(c.arg(2)), va), c);
}

// ---- sscanf: one conversion at a time through the host scanner -------------------------
void h_sscanf(Ctx& c) {
    const char* in = HS(c.arg(0));
    const char* fmt = HS(c.arg(1));
    VaReader va(c, 2);
    const char* s = in;
    int assigned = 0;
    bool any_input = false;
    for (const char* p = fmt; *p;) {
        if (std::isspace(static_cast<u8>(*p))) {
            while (std::isspace(static_cast<u8>(*s))) ++s;
            ++p;
            continue;
        }
        if (*p != '%' || p[1] == '%') {
            char want = *p == '%' ? '%' : *p;
            p += *p == '%' ? 2 : 1;
            if (*s != want) break;
            ++s;
            continue;
        }
        ++p;
        bool suppress = false;
        if (*p == '*') { suppress = true; ++p; }
        std::string width;
        while (*p >= '0' && *p <= '9') width += *p++;
        int lng = 0;
        for (;;) {
            if (*p == 'h') { lng = lng == -1 ? -2 : -1; ++p; }
            else if (*p == 'l') { lng = lng == 1 ? 2 : 1; ++p; }
            else if (*p == 'q' || *p == 'j' || *p == 'L') { lng = 2; ++p; }
            else if (*p == 'z' || *p == 't') ++p;
            else break;
        }
        std::string conv(1, *p);
        if (*p == '[') {
            const char* q = p + 1;
            if (*q == '^') ++q;
            if (*q == ']') ++q;
            while (*q && *q != ']') ++q;
            conv.assign(p, q + (*q ? 1 : 0));
            p = q;
        }
        if (!*p) break;
        ++p;
        char ct = conv[0];
        if (ct == 'n') {
            if (!suppress) wr32(va.next32(), static_cast<u32>(s - in));
            continue;
        }
        if (!*s) break;
        any_input = true;
        int used = 0;
        int got = 0;
        std::string hostfmt = "%" + std::string(suppress ? "*" : "") + width;
        if (ct == 'd' || ct == 'i' || ct == 'u' || ct == 'x' || ct == 'X' || ct == 'o') {
            long long v = 0;
            std::string f = hostfmt + "ll" + ct + "%n";
            got = suppress ? std::sscanf(s, f.c_str(), &used) : std::sscanf(s, f.c_str(), &v, &used);
            if (got == 1 && !suppress) {
                u32 dst = va.next32();
                if (lng == 2) wr64(dst, static_cast<u64>(v));
                else if (lng == -1) wr16(dst, static_cast<u16>(v));
                else if (lng == -2) *HP<u8>(dst) = static_cast<u8>(v);
                else wr32(dst, static_cast<u32>(v));
            }
        } else if (ct == 'f' || ct == 'e' || ct == 'g' || ct == 'E' || ct == 'G' || ct == 'a') {
            double v = 0;
            std::string f = hostfmt + "l" + ct + "%n";
            got = suppress ? std::sscanf(s, f.c_str(), &used) : std::sscanf(s, f.c_str(), &v, &used);
            if (got == 1 && !suppress) {
                u32 dst = va.next32();
                if (lng >= 1) wr64(dst, bits_from(v));
                else wr32(dst, bits_from(static_cast<float>(v)));
            }
        } else if (ct == 's' || ct == 'c' || ct == '[') {
            std::vector<char> tmp(std::strlen(s) + 2);
            std::string f = hostfmt + conv + "%n";
            got = suppress ? std::sscanf(s, f.c_str(), &used) : std::sscanf(s, f.c_str(), tmp.data(), &used);
            if (got == 1 && !suppress) {
                u32 dst = va.next32();
                size_t n = ct == 'c' ? (width.empty() ? 1 : std::stoul(width)) : std::strlen(tmp.data()) + 1;
                std::memcpy(H(dst), tmp.data(), n);
            }
        } else {
            break;
        }
        if (suppress) {
            // %*x conversions report 0 assignments; detect progress via %n alone.
            if (used == 0) break;
        } else {
            if (got != 1) break;
            ++assigned;
        }
        s += used;
    }
    c.ret(!any_input && assigned == 0 && !*s ? 0xFFFFFFFFu : static_cast<u32>(assigned));
}

// ---- wide stdio (narrow C locale) --------------------------------------------------------
void h_getwc(Ctx& c) {
    FILE* h = host_file(c.arg(0));
    int r = h ? std::fgetc(h) : EOF;
    c.ret(r == EOF ? 0xFFFFFFFFu : static_cast<u32>(r));
}
void h_putwc(Ctx& c) {
    u32 w = c.arg(0);
    write_out(c.arg(1), std::string(1, static_cast<char>(w)), c);
    c.ret(w);
}
void h_ungetwc(Ctx& c) {
    FILE* h = host_file(c.arg(1));
    c.ret(h ? static_cast<u32>(std::ungetc(static_cast<int>(c.arg(0)), h)) : 0xFFFFFFFFu);
}

// ---- file descriptors ----------------------------------------------------------------------
int host_open_flags(u32 g) {
    // Guest (Linux ARM) O_ flags -> host.
    int f = 0;
    switch (g & 3) {
    case 0: f = O_RDONLY; break;
    case 1: f = O_WRONLY; break;
    default: f = O_RDWR; break;
    }
    if (g & 0100) f |= O_CREAT;
    if (g & 01000) f |= O_TRUNC;
    if (g & 02000) f |= O_APPEND;
    if (g & 0200) f |= O_EXCL;
#if defined(_WIN32)
    f |= O_BINARY;
#endif
    return f;
}
void h_open(Ctx& c) {
    std::string path = map_path(HS(c.arg(0)));
    int fd = ::open(path.c_str(), host_open_flags(c.arg(1)), static_cast<int>(c.arg(2) ? c.arg(2) : 0644));
    if (fd < 0) set_guest_errno(errno);
    c.ret(static_cast<u32>(fd));
}
void h_close(Ctx& c) { c.ret(static_cast<u32>(::close(static_cast<int>(c.arg(0))))); }
void h_read(Ctx& c) {
    auto r = ::read(static_cast<int>(c.arg(0)), H(c.arg(1)), c.arg(2));
    if (r < 0) set_guest_errno(errno);
    c.ret(static_cast<u32>(r));
}
void h_write(Ctx& c) {
    int fd = static_cast<int>(c.arg(0));
    if (fd == 1 || fd == 2) { log_text(std::string(HS(c.arg(1)), c.arg(2))); c.ret(c.arg(2)); return; }
    auto r = ::write(fd, H(c.arg(1)), c.arg(2));
    if (r < 0) set_guest_errno(errno);
    c.ret(static_cast<u32>(r));
}
void h_writev(Ctx& c) {
    int fd = static_cast<int>(c.arg(0));
    u32 iov = c.arg(1), cnt = c.arg(2), total = 0;
    for (u32 i = 0; i < cnt; ++i) {
        u32 base = rd32(iov + 8 * i), len = rd32(iov + 8 * i + 4);
        if (fd == 1 || fd == 2) log_text(std::string(HS(base), len));
        else if (::write(fd, H(base), len) < 0) { set_guest_errno(errno); c.ret(0xFFFFFFFFu); return; }
        total += len;
    }
    c.ret(total);
}
void h_lseek(Ctx& c) {
    auto r = ::lseek(static_cast<int>(c.arg(0)), static_cast<s32>(c.arg(1)), static_cast<int>(c.arg(2)));
    c.ret(static_cast<u32>(r));
}

// 32-bit ARM bionic struct stat (104 bytes).
void fill_stat(u32 dst, const struct stat& st) {
    std::memset(H(dst), 0, 104);
    wr64(dst + 0, static_cast<u64>(st.st_dev));
    wr32(dst + 12, static_cast<u32>(st.st_ino));
    wr32(dst + 16, static_cast<u32>(st.st_mode));
    wr32(dst + 20, static_cast<u32>(st.st_nlink));
    wr32(dst + 24, static_cast<u32>(st.st_uid));
    wr32(dst + 28, static_cast<u32>(st.st_gid));
    wr64(dst + 48, static_cast<u64>(st.st_size));
    wr32(dst + 56, 4096);
    wr64(dst + 64, (static_cast<u64>(st.st_size) + 511) / 512);
    wr32(dst + 72, static_cast<u32>(st.st_atime));
    wr32(dst + 80, static_cast<u32>(st.st_mtime));
    wr32(dst + 88, static_cast<u32>(st.st_ctime));
    wr64(dst + 96, static_cast<u64>(st.st_ino));
}
void h_stat(Ctx& c) {
    struct stat st;
    std::string path = map_path(HS(c.arg(0)));
    if (::stat(path.c_str(), &st) != 0) { set_guest_errno(errno); c.ret(0xFFFFFFFFu); return; }
    fill_stat(c.arg(1), st);
    c.ret(0);
}
void h_fstat(Ctx& c) {
    struct stat st;
    if (::fstat(static_cast<int>(c.arg(0)), &st) != 0) { set_guest_errno(errno); c.ret(0xFFFFFFFFu); return; }
    fill_stat(c.arg(1), st);
    c.ret(0);
}

}  // namespace

void hle_register_stdio() {
    g_sF = data_alloc(3 * kFileSize);
    for (int i = 0; i < 3; ++i) wr16(g_sF + i * kFileSize + 14, static_cast<u16>(i));
    hle_data("__sF", g_sF);
#define R(n) hle_func(#n, h_##n)
    R(fopen); R(fdopen); R(tmpfile); R(fclose); R(fread); R(fwrite); R(fseek); R(ftell); R(rewind); R(fgets);
    R(getc); R(ungetc); R(fputc); R(fputs); R(puts); R(fflush); R(ferror); R(fileno); R(setvbuf);
    R(printf); R(vprintf); R(fprintf); R(vfprintf); R(sprintf); R(vsprintf); R(snprintf); R(vsnprintf); R(sscanf);
    R(getwc); R(putwc); R(ungetwc); R(open); R(close); R(read); R(write); R(writev); R(lseek); R(stat); R(fstat);
#undef R
    hle_func("putc", h_fputc);
}

}  // namespace compat
