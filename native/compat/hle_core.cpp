// Import registry plus memory, string, math, stdlib and wide-char imports.
#include <cmath>
#include <cwctype>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "guest_mem.h"
#include "hle.h"

namespace compat {

namespace {
struct Entry {
    HleFn fn = nullptr;
    u32 data = 0;
    u32 stub = 0;
};
std::map<std::string, Entry>& registry() {
    static std::map<std::string, Entry> r;
    return r;
}
std::mutex g_reg_mu;

std::set<u32> g_missing_logged;
std::mutex g_missing_mu;

void unimplemented_import(Ctx& c) {
    std::lock_guard<std::mutex> lk(g_missing_mu);
    if (g_missing_logged.insert(c.svc).second)
        LOGE("guest called unimplemented import %s (from %08x); returning 0", stub_name(c.svc), c.lr());
    c.ret(0);
}
}  // namespace

void hle_func(const char* name, HleFn fn) {
    std::lock_guard<std::mutex> lk(g_reg_mu);
    registry()[name].fn = fn;
}

void hle_data(const char* name, u32 addr) {
    std::lock_guard<std::mutex> lk(g_reg_mu);
    registry()[name].data = addr;
}

u32 hle_resolve(const char* name, bool is_function) {
    std::lock_guard<std::mutex> lk(g_reg_mu);
    auto it = registry().find(name);
    if (it != registry().end()) {
        Entry& e = it->second;
        if (e.data) return e.data;
        if (e.fn) {
            if (!e.stub) e.stub = make_stub(it->first.c_str(), e.fn);
            return e.stub;
        }
    }
    if (!is_function) return 0;
    LOGW("import %s has no implementation; calls will log and return 0", name);
    Entry& e = registry()[name];
    e.stub = make_stub(registry().find(name)->first.c_str(), unimplemented_import);
    return e.stub;
}

// ---- errno ------------------------------------------------------------------------
static thread_local u32 t_errno_addr = 0;
static u32 errno_addr() {
    if (!t_errno_addr) t_errno_addr = gcalloc(1, 4);
    return t_errno_addr;
}
void set_guest_errno(int e) { wr32(errno_addr(), static_cast<u32>(e)); }

// ---- varargs -------------------------------------------------------------------------
u32 VaReader::next32() {
    if (ctx_) return ctx_->arg(slot_++);
    u32 v = rd32(mem_);
    mem_ += 4;
    return v;
}
u64 VaReader::next64() {
    if (ctx_) {
        if (slot_ & 1) ++slot_;
        u64 v = ctx_->arg64(slot_);
        slot_ += 2;
        return v;
    }
    mem_ = (mem_ + 7) & ~7u;
    u64 v = rd64(mem_);
    mem_ += 8;
    return v;
}

namespace {

// ---- memory ------------------------------------------------------------------------
void h_malloc(Ctx& c) { c.ret(gmalloc(c.arg(0))); }
void h_calloc(Ctx& c) { c.ret(gcalloc(c.arg(0), c.arg(1))); }
void h_realloc(Ctx& c) { c.ret(grealloc(c.arg(0), c.arg(1))); }
void h_free(Ctx& c) { gfree(c.arg(0)); }
void h_memcpy(Ctx& c) { std::memcpy(H(c.arg(0)), H(c.arg(1)), c.arg(2)); }
void h_memmove(Ctx& c) { std::memmove(H(c.arg(0)), H(c.arg(1)), c.arg(2)); }
void h_memset(Ctx& c) { std::memset(H(c.arg(0)), static_cast<int>(c.arg(1)), c.arg(2)); }
void h_memcmp(Ctx& c) { c.ret(static_cast<u32>(std::memcmp(H(c.arg(0)), H(c.arg(1)), c.arg(2)))); }
void h_memchr(Ctx& c) {
    const void* p = std::memchr(H(c.arg(0)), static_cast<int>(c.arg(1) & 0xFF), c.arg(2));
    c.ret(p ? G(p) : 0);
}
void h_memrchr(Ctx& c) {
    const u8* b = HP<u8>(c.arg(0));
    u8 ch = static_cast<u8>(c.arg(1));
    for (u32 n = c.arg(2); n > 0; --n)
        if (b[n - 1] == ch) { c.ret(c.arg(0) + n - 1); return; }
    c.ret(0);
}

// ---- strings -------------------------------------------------------------------------
void h_strlen(Ctx& c) { c.ret(static_cast<u32>(std::strlen(HS(c.arg(0))))); }
void h_strcmp(Ctx& c) { c.ret(static_cast<u32>(std::strcmp(HS(c.arg(0)), HS(c.arg(1))))); }
void h_strncmp(Ctx& c) { c.ret(static_cast<u32>(std::strncmp(HS(c.arg(0)), HS(c.arg(1)), c.arg(2)))); }
int lower(int ch) { return (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch; }
void h_strcasecmp(Ctx& c) {
    const u8* a = HP<u8>(c.arg(0));
    const u8* b = HP<u8>(c.arg(1));
    int d;
    while ((d = lower(*a) - lower(*b)) == 0 && *a) { ++a; ++b; }
    c.ret(static_cast<u32>(d));
}
void h_strncasecmp(Ctx& c) {
    const u8* a = HP<u8>(c.arg(0));
    const u8* b = HP<u8>(c.arg(1));
    u32 n = c.arg(2);
    int d = 0;
    for (; n; --n, ++a, ++b) {
        d = lower(*a) - lower(*b);
        if (d || !*a) break;
    }
    c.ret(static_cast<u32>(n ? d : 0));
}
void h_strcpy(Ctx& c) { std::strcpy(HP<char>(c.arg(0)), HS(c.arg(1))); }
void h_strncpy(Ctx& c) { std::strncpy(HP<char>(c.arg(0)), HS(c.arg(1)), c.arg(2)); }
void h_strcat(Ctx& c) { std::strcat(HP<char>(c.arg(0)), HS(c.arg(1))); }
void h_strlcat(Ctx& c) {
    char* d = HP<char>(c.arg(0));
    const char* s = HS(c.arg(1));
    u32 size = c.arg(2);
    u32 dl = static_cast<u32>(strnlen(d, size)), sl = static_cast<u32>(std::strlen(s));
    if (dl == size) { c.ret(size + sl); return; }
    u32 copy = sl < size - dl - 1 ? sl : size - dl - 1;
    std::memcpy(d + dl, s, copy);
    d[dl + copy] = 0;
    c.ret(dl + sl);
}
void h_strchr(Ctx& c) {
    const char* p = std::strchr(HS(c.arg(0)), static_cast<int>(c.arg(1) & 0xFF));
    c.ret(p ? G(p) : 0);
}
void h_strrchr(Ctx& c) {
    const char* p = std::strrchr(HS(c.arg(0)), static_cast<int>(c.arg(1) & 0xFF));
    c.ret(p ? G(p) : 0);
}
void h_strstr(Ctx& c) {
    const char* p = std::strstr(HS(c.arg(0)), HS(c.arg(1)));
    c.ret(p ? G(p) : 0);
}
void h_strdup(Ctx& c) { c.ret(gstrdup(HS(c.arg(0)))); }
void h_strtok_r(Ctx& c) {
    u32 s = c.arg(0);
    const char* delim = HS(c.arg(1));
    u32 save = c.arg(2);
    if (!s) s = rd32(save);
    if (!s) { c.ret(0); return; }
    char* p = HP<char>(s);
    p += std::strspn(p, delim);
    if (!*p) { wr32(save, 0); c.ret(0); return; }
    char* e = p + std::strcspn(p, delim);
    if (*e) { *e = 0; wr32(save, G(e + 1)); } else { wr32(save, 0); }
    c.ret(G(p));
}
void h_strcoll(Ctx& c) { h_strcmp(c); }
void h_strxfrm(Ctx& c) {
    const char* s = HS(c.arg(1));
    u32 n = c.arg(2), len = static_cast<u32>(std::strlen(s));
    if (n) { u32 k = len < n - 1 ? len : n - 1; std::memcpy(H(c.arg(0)), s, k); HP<char>(c.arg(0))[k] = 0; }
    c.ret(len);
}
u32 g_strerror_buf = 0;
void h_strerror(Ctx& c) {
    if (!g_strerror_buf) g_strerror_buf = data_alloc(128);
    std::snprintf(HP<char>(g_strerror_buf), 128, "%s", std::strerror(static_cast<int>(c.arg(0))));
    c.ret(g_strerror_buf);
}
void h_strerror_r(Ctx& c) {
    if (c.arg(2)) std::snprintf(HP<char>(c.arg(1)), c.arg(2), "%s", std::strerror(static_cast<int>(c.arg(0))));
    c.ret(0);
}
u32 g_basename_buf = 0;
void h_basename(Ctx& c) {
    if (!g_basename_buf) g_basename_buf = data_alloc(4096);
    std::string s = c.arg(0) ? HS(c.arg(0)) : "";
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    size_t k = s.rfind('/');
    std::string b = s.empty() ? "." : (k == std::string::npos || s.size() == 1 ? s : s.substr(k + 1));
    std::snprintf(HP<char>(g_basename_buf), 4096, "%s", b.c_str());
    c.ret(g_basename_buf);
}

// ---- stdlib -------------------------------------------------------------------------
void h_atoi(Ctx& c) { c.ret(static_cast<u32>(std::atoi(HS(c.arg(0))))); }
void h_strtol(Ctx& c) {
    char* end = nullptr;
    long long v = std::strtoll(HS(c.arg(0)), &end, static_cast<int>(c.arg(2)));
    if (v > 0x7FFFFFFFLL) { v = 0x7FFFFFFFLL; set_guest_errno(34); }
    if (v < -0x80000000LL) { v = -0x80000000LL; set_guest_errno(34); }
    if (c.arg(1)) wr32(c.arg(1), G(end));
    c.ret(static_cast<u32>(static_cast<s32>(v)));
}
void h_strtoul(Ctx& c) {
    char* end = nullptr;
    unsigned long long v = std::strtoull(HS(c.arg(0)), &end, static_cast<int>(c.arg(2)));
    if (c.arg(1)) wr32(c.arg(1), G(end));
    c.ret(v > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<u32>(v));
}
void h_strtoll(Ctx& c) {
    char* end = nullptr;
    long long v = std::strtoll(HS(c.arg(0)), &end, static_cast<int>(c.arg(2)));
    if (c.arg(1)) wr32(c.arg(1), G(end));
    c.ret64(static_cast<u64>(v));
}
void h_strtod(Ctx& c) {
    char* end = nullptr;
    double v = std::strtod(HS(c.arg(0)), &end);
    if (c.arg(1)) wr32(c.arg(1), G(end));
    c.retd(v);
}

// POSIX 48-bit LCG, same constants and seeding as bionic/BSD (keeps original randomness behaviour).
u64 g_rand48 = 0x1234ABCD330Eull;
void h_srand48(Ctx& c) { g_rand48 = (u64(c.arg(0)) << 16) | 0x330E; }
void h_lrand48(Ctx& c) {
    g_rand48 = (g_rand48 * 0x5DEECE66Dull + 0xB) & ((1ull << 48) - 1);
    c.ret(static_cast<u32>(g_rand48 >> 17));
}

void h_qsort(Ctx& c) {
    u32 base = c.arg(0), n = c.arg(1), sz = c.arg(2), cmp = c.arg(3);
    if (n < 2 || !sz) return;
    std::vector<u8> tmp(sz);
    for (u32 i = 1; i < n; ++i) {  // insertion sort (stable); only used by curl cookie handling
        u32 j = i;
        while (j > 0 && static_cast<s32>(call_guest(cmp, {base + (j - 1) * sz, base + j * sz})) > 0) {
            std::memcpy(tmp.data(), H(base + (j - 1) * sz), sz);
            std::memcpy(H(base + (j - 1) * sz), H(base + j * sz), sz);
            std::memcpy(H(base + j * sz), tmp.data(), sz);
            --j;
        }
    }
}
void h_bsearch(Ctx& c) {
    u32 key = c.arg(0), base = c.arg(1), n = c.arg(2), sz = c.arg(3), cmp = c.arg(4);
    u32 lo = 0, hi = n;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        s32 r = static_cast<s32>(call_guest(cmp, {key, base + mid * sz}));
        if (r == 0) { c.ret(base + mid * sz); return; }
        if (r < 0) hi = mid; else lo = mid + 1;
    }
    c.ret(0);
}

// ---- math (soft-float calling convention) ---------------------------------------------
#define F1(name, expr) void h_##name(Ctx& c) { float x = c.argf(0); c.retf(expr); }
#define F2(name, expr) void h_##name(Ctx& c) { float x = c.argf(0), y = c.argf(1); c.retf(expr); }
#define D1(name, expr) void h_##name(Ctx& c) { double x = c.argd(0); c.retd(expr); }
#define D2(name, expr) void h_##name(Ctx& c) { double x = c.argd(0), y = c.argd(2); c.retd(expr); }
F1(sinf, std::sin(x)) F1(cosf, std::cos(x)) F1(tanf, std::tan(x)) F1(asinf, std::asin(x)) F1(acosf, std::acos(x))
F1(sqrtf, std::sqrt(x)) F1(fabsf, std::fabs(x)) F1(ceilf, std::ceil(x)) F1(floorf, std::floor(x))
F1(roundf, std::round(x)) F1(expf, std::exp(x)) F1(logf, std::log(x))
F2(atan2f, std::atan2(x, y)) F2(powf, std::pow(x, y)) F2(fmodf, std::fmod(x, y)) F2(fmaxf, std::fmax(x, y))
F2(fminf, std::fmin(x, y))
D1(sin, std::sin(x)) D1(cos, std::cos(x)) D1(tan, std::tan(x)) D1(sqrt, std::sqrt(x)) D1(ceil, std::ceil(x))
D1(floor, std::floor(x))
D2(atan2, std::atan2(x, y)) D2(pow, std::pow(x, y)) D2(fmax, std::fmax(x, y))
void h_difftime(Ctx& c) { c.retd(static_cast<double>(static_cast<s32>(c.arg(0))) - static_cast<s32>(c.arg(1))); }

// ---- wide characters (guest wchar_t is 32-bit; C locale semantics) --------------------
constexpr u32 kWEOF = 0xFFFFFFFFu;
void h_wcslen(Ctx& c) { u32 p = c.arg(0), n = 0; while (rd32(p + 4 * n)) ++n; c.ret(n); }
void h_wmemcpy(Ctx& c) { std::memcpy(H(c.arg(0)), H(c.arg(1)), 4ull * c.arg(2)); }
void h_wmemmove(Ctx& c) { std::memmove(H(c.arg(0)), H(c.arg(1)), 4ull * c.arg(2)); }
void h_wmemset(Ctx& c) { for (u32 i = 0; i < c.arg(2); ++i) wr32(c.arg(0) + 4 * i, c.arg(1)); }
void h_wmemchr(Ctx& c) {
    for (u32 i = 0; i < c.arg(2); ++i)
        if (rd32(c.arg(0) + 4 * i) == c.arg(1)) { c.ret(c.arg(0) + 4 * i); return; }
    c.ret(0);
}
void h_wmemcmp(Ctx& c) {
    for (u32 i = 0; i < c.arg(2); ++i) {
        s32 a = static_cast<s32>(rd32(c.arg(0) + 4 * i)), b = static_cast<s32>(rd32(c.arg(1) + 4 * i));
        if (a != b) { c.ret(a < b ? 0xFFFFFFFFu : 1); return; }
    }
    c.ret(0);
}
void h_wcscoll(Ctx& c) {
    u32 a = c.arg(0), b = c.arg(1);
    for (;; a += 4, b += 4) {
        s32 x = static_cast<s32>(rd32(a)), y = static_cast<s32>(rd32(b));
        if (x != y) { c.ret(x < y ? 0xFFFFFFFFu : 1); return; }
        if (!x) { c.ret(0); return; }
    }
}
void h_wcsxfrm(Ctx& c) {
    u32 src = c.arg(1), n = c.arg(2), len = 0;
    while (rd32(src + 4 * len)) ++len;
    if (n) { u32 k = len < n - 1 ? len : n - 1; std::memcpy(H(c.arg(0)), H(src), 4ull * k); wr32(c.arg(0) + 4 * k, 0); }
    c.ret(len);
}
void h_towlower(Ctx& c) { u32 w = c.arg(0); c.ret(w < 0x80 ? static_cast<u32>(lower(static_cast<int>(w))) : w); }
void h_towupper(Ctx& c) { u32 w = c.arg(0); c.ret((w >= 'a' && w <= 'z') ? w - 32 : w); }
const char* kWctypeNames[] = {nullptr, "alnum", "alpha", "blank", "cntrl", "digit", "graph",
                              "lower", "print", "punct", "space", "upper", "xdigit"};
void h_wctype(Ctx& c) {
    const char* n = HS(c.arg(0));
    for (u32 i = 1; i < sizeof kWctypeNames / sizeof *kWctypeNames; ++i)
        if (std::strcmp(n, kWctypeNames[i]) == 0) { c.ret(i); return; }
    c.ret(0);
}
void h_iswctype(Ctx& c) {
    u32 w = c.arg(0);
    if (w > 0x7F) { c.ret(0); return; }  // C locale: only ASCII has classes
    int ch = static_cast<int>(w);
    bool r = false;
    switch (c.arg(1)) {
    case 1: r = std::isalnum(ch); break;
    case 2: r = std::isalpha(ch); break;
    case 3: r = ch == ' ' || ch == '\t'; break;
    case 4: r = std::iscntrl(ch); break;
    case 5: r = std::isdigit(ch); break;
    case 6: r = std::isgraph(ch); break;
    case 7: r = std::islower(ch); break;
    case 8: r = std::isprint(ch); break;
    case 9: r = std::ispunct(ch); break;
    case 10: r = std::isspace(ch); break;
    case 11: r = std::isupper(ch); break;
    case 12: r = std::isxdigit(ch); break;
    }
    c.ret(r ? 1 : 0);
}
void h_btowc(Ctx& c) { s32 ch = static_cast<s32>(c.arg(0)); c.ret(ch >= 0 && ch < 0x80 ? static_cast<u32>(ch) : kWEOF); }
void h_wctob(Ctx& c) { u32 w = c.arg(0); c.ret(w < 0x80 ? w : 0xFFFFFFFFu); }
void h_mbrtowc(Ctx& c) {
    u32 pwc = c.arg(0), s = c.arg(1), n = c.arg(2);
    if (!s) { c.ret(0); return; }
    if (!n) { c.ret(0xFFFFFFFEu); return; }
    u8 b = *HP<u8>(s);
    if (pwc) wr32(pwc, b);
    c.ret(b ? 1 : 0);
}
void h_wcrtomb(Ctx& c) {
    u32 s = c.arg(0), w = c.arg(1);
    if (!s) { c.ret(1); return; }
    if (w > 0xFF) { set_guest_errno(84); c.ret(0xFFFFFFFFu); return; }
    *HP<u8>(s) = static_cast<u8>(w);
    c.ret(1);
}

// ---- locale -----------------------------------------------------------------------
u32 g_c_locale = 0;
void h_setlocale(Ctx& c) {
    if (!g_c_locale) g_c_locale = data_strdup("C");
    const char* req = HS(c.arg(1));
    if (!req || !*req || std::strcmp(req, "C") == 0 || std::strcmp(req, "POSIX") == 0) c.ret(g_c_locale);
    else c.ret(0);
}

// ---- ctype tables (bionic exposes pointer variables) ------------------------------
void make_ctype_tables() {
    constexpr u8 U = 0x01, L = 0x02, N = 0x04, S = 0x08, P = 0x10, C = 0x20, X = 0x40, B = 0x80;
    u32 tab = data_alloc(1 + 256, 4);
    u8* t = HP<u8>(tab) + 1;
    for (int ch = 0; ch < 128; ++ch) {
        u8 v = 0;
        if (ch < 0x20 || ch == 0x7F) v |= C;
        if (ch >= 0x09 && ch <= 0x0D) v |= S;
        if (ch == ' ') v |= S | B;
        if ((ch >= 0x21 && ch <= 0x2F) || (ch >= 0x3A && ch <= 0x40) || (ch >= 0x5B && ch <= 0x60) ||
            (ch >= 0x7B && ch <= 0x7E))
            v |= P;
        if (ch >= '0' && ch <= '9') v |= N;
        if (ch >= 'A' && ch <= 'Z') v |= U;
        if (ch >= 'a' && ch <= 'z') v |= L;
        if ((ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')) v |= X;
        t[ch] = v;
    }
    u32 lo = data_alloc(2 * 257, 4), up = data_alloc(2 * 257, 4);
    wr16(lo, 0xFFFF);
    wr16(up, 0xFFFF);
    for (int ch = 0; ch < 256; ++ch) {
        wr16(lo + 2 + 2 * ch, static_cast<u16>((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch));
        wr16(up + 2 + 2 * ch, static_cast<u16>((ch >= 'a' && ch <= 'z') ? ch - 32 : ch));
    }
    u32 p_ctype = data_alloc(4), p_lo = data_alloc(4), p_up = data_alloc(4);
    wr32(p_ctype, tab);
    wr32(p_lo, lo);
    wr32(p_up, up);
    hle_data("_ctype_", p_ctype);
    hle_data("_tolower_tab_", p_lo);
    hle_data("_toupper_tab_", p_up);
}

void h_errno(Ctx& c) { c.ret(errno_addr()); }

}  // namespace

void hle_register_libc() {
    make_ctype_tables();
#define R(n) hle_func(#n, h_##n)
    R(malloc); R(calloc); R(realloc); R(free); R(memcpy); R(memmove); R(memset); R(memcmp); R(memchr); R(memrchr);
    R(strlen); R(strcmp); R(strncmp); R(strcasecmp); R(strncasecmp); R(strcpy); R(strncpy); R(strcat); R(strlcat);
    R(strchr); R(strrchr); R(strstr); R(strdup); R(strtok_r); R(strcoll); R(strxfrm); R(strerror); R(strerror_r);
    R(basename); R(atoi); R(strtol); R(strtoul); R(strtoll); R(strtod); R(srand48); R(lrand48); R(qsort); R(bsearch);
    R(sinf); R(cosf); R(tanf); R(asinf); R(acosf); R(sqrtf); R(fabsf); R(ceilf); R(floorf); R(roundf); R(expf);
    R(logf); R(atan2f); R(powf); R(fmodf); R(fmaxf); R(fminf); R(sin); R(cos); R(tan); R(sqrt); R(ceil); R(floor);
    R(atan2); R(pow); R(fmax); R(difftime);
    R(wcslen); R(wmemcpy); R(wmemmove); R(wmemset); R(wmemchr); R(wmemcmp); R(wcscoll); R(wcsxfrm); R(towlower);
    R(towupper); R(wctype); R(iswctype); R(btowc); R(wctob); R(mbrtowc); R(wcrtomb); R(setlocale);
#undef R
    hle_func("__errno", h_errno);
}

}  // namespace compat
