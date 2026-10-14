// Time, threads, setjmp/longjmp, unwinding support, process and (disabled) networking imports.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>

#include "elf_loader.h"
#include "guest_mem.h"
#include "hle.h"

namespace compat {

namespace {

// ---- logging --------------------------------------------------------------------------------
void h___android_log_print(Ctx& c) {
    VaReader va(c, 3);
    std::string msg = guest_format(HS(c.arg(2)), va);
    const char* tag = c.arg(1) ? HS(c.arg(1)) : "";
    LOGI("[%s] %s", tag, msg.c_str());
    c.ret(1);
}

// ---- time -------------------------------------------------------------------------------------
void h_time(Ctx& c) {
    u32 t = static_cast<u32>(std::time(nullptr));
    if (c.arg(0)) wr32(c.arg(0), t);
    c.ret(t);
}
void h_gettimeofday(Ctx& c) {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    if (c.arg(0)) { wr32(c.arg(0), static_cast<u32>(us / 1000000)); wr32(c.arg(0) + 4, static_cast<u32>(us % 1000000)); }
    if (c.arg(1)) { wr32(c.arg(1), 0); wr32(c.arg(1) + 4, 0); }
    c.ret(0);
}
void h_clock_gettime(Ctx& c) {
    long long ns;
    if (c.arg(0) == 0)
        ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    else
        ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    wr32(c.arg(1), static_cast<u32>(ns / 1000000000));
    wr32(c.arg(1) + 4, static_cast<u32>(ns % 1000000000));
    c.ret(0);
}

// Guest struct tm (32-bit bionic): 9 ints, long tm_gmtoff, const char* tm_zone = 44 bytes.
u32 g_tm_static = 0, g_zone_utc = 0, g_zone_local = 0;
void tm_to_guest(const std::tm& t, u32 dst, bool utc) {
    const int v[9] = {t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst};
    for (int i = 0; i < 9; ++i) wr32(dst + 4 * i, static_cast<u32>(v[i]));
    wr32(dst + 36, 0);
    wr32(dst + 40, utc ? g_zone_utc : g_zone_local);
}
std::tm tm_from_guest(u32 src) {
    std::tm t{};
    int* f[9] = {&t.tm_sec, &t.tm_min, &t.tm_hour, &t.tm_mday, &t.tm_mon, &t.tm_year, &t.tm_wday, &t.tm_yday, &t.tm_isdst};
    for (int i = 0; i < 9; ++i) *f[i] = static_cast<int>(rd32(src + 4 * i));
    return t;
}
bool host_time(std::time_t tt, bool utc, std::tm& out) {
#if defined(_WIN32)
    return (utc ? gmtime_s(&out, &tt) : localtime_s(&out, &tt)) == 0;
#else
    return (utc ? gmtime_r(&tt, &out) : localtime_r(&tt, &out)) != nullptr;
#endif
}
void time_common(Ctx& c, bool utc, u32 dst) {
    std::tm t{};
    if (!host_time(static_cast<std::time_t>(static_cast<s32>(rd32(c.arg(0)))), utc, t)) { c.ret(0); return; }
    tm_to_guest(t, dst, utc);
    c.ret(dst);
}
void h_gmtime(Ctx& c) { time_common(c, true, g_tm_static); }
void h_localtime(Ctx& c) { time_common(c, false, g_tm_static); }
void h_gmtime_r(Ctx& c) { time_common(c, true, c.arg(1)); }
void h_strftime(Ctx& c) {
    std::tm t = tm_from_guest(c.arg(3));
    c.ret(static_cast<u32>(std::strftime(HP<char>(c.arg(0)), c.arg(1), HS(c.arg(2)), &t)));
}
void h_wcsftime(Ctx& c) {
    std::string fmt;
    for (u32 p = c.arg(2); rd32(p); p += 4) fmt += static_cast<char>(rd32(p));
    std::tm t = tm_from_guest(c.arg(3));
    char buf[512];
    size_t n = std::strftime(buf, sizeof buf, fmt.c_str(), &t);
    if (n + 1 > c.arg(1)) { c.ret(0); return; }
    for (size_t i = 0; i <= n; ++i) wr32(c.arg(0) + 4 * static_cast<u32>(i), static_cast<u8>(buf[i]));
    c.ret(static_cast<u32>(n));
}

// ---- pthreads -------------------------------------------------------------------------------
std::mutex g_pt_mu;
std::map<u32, std::recursive_mutex*> g_mutexes;
std::recursive_mutex* mutex_for(u32 a) {
    std::lock_guard<std::mutex> lk(g_pt_mu);
    auto& m = g_mutexes[a];
    if (!m) m = new std::recursive_mutex();
    return m;
}
void h_pthread_mutex_init(Ctx& c) { mutex_for(c.arg(0)); c.ret(0); }
void h_pthread_mutex_lock(Ctx& c) { mutex_for(c.arg(0))->lock(); c.ret(0); }
void h_pthread_mutex_unlock(Ctx& c) { mutex_for(c.arg(0))->unlock(); c.ret(0); }

u32 g_next_key = 1;
thread_local std::unordered_map<u32, u32> t_tls;
void h_pthread_key_create(Ctx& c) {
    std::lock_guard<std::mutex> lk(g_pt_mu);
    wr32(c.arg(0), g_next_key++);
    c.ret(0);
}
void h_pthread_key_delete(Ctx& c) { t_tls.erase(c.arg(0)); c.ret(0); }
void h_pthread_getspecific(Ctx& c) {
    auto it = t_tls.find(c.arg(0));
    c.ret(it == t_tls.end() ? 0 : it->second);
}
void h_pthread_setspecific(Ctx& c) { t_tls[c.arg(0)] = c.arg(1); c.ret(0); }
void h_pthread_once(Ctx& c) {
    u32 once = c.arg(0);
    {
        std::lock_guard<std::mutex> lk(g_pt_mu);
        if (rd32(once) != 0) { c.ret(0); return; }
        wr32(once, 1);
    }
    call_guest(c.arg(1));
    c.ret(0);
}
void h_pthread_create(Ctx& c) {
    // Only reachable through CCTextureCache::addImageAsync, which nothing in the game calls.
    LOGW("guest pthread_create refused (from %08x)", c.lr());
    c.ret(11 /* EAGAIN */);
}

struct Sem {
    std::mutex mu;
    std::condition_variable cv;
    u32 count = 0;
};
std::map<u32, Sem*> g_sems;
Sem* sem_for(u32 a) {
    std::lock_guard<std::mutex> lk(g_pt_mu);
    auto& s = g_sems[a];
    if (!s) s = new Sem();
    return s;
}
void h_sem_init(Ctx& c) { sem_for(c.arg(0))->count = c.arg(2); c.ret(0); }
void h_sem_post(Ctx& c) {
    Sem* s = sem_for(c.arg(0));
    { std::lock_guard<std::mutex> lk(s->mu); ++s->count; }
    s->cv.notify_one();
    c.ret(0);
}
void h_sem_wait(Ctx& c) {
    Sem* s = sem_for(c.arg(0));
    std::unique_lock<std::mutex> lk(s->mu);
    s->cv.wait(lk, [s] { return s->count > 0; });
    --s->count;
    c.ret(0);
}

// ---- setjmp / longjmp ---------------------------------------------------------------------------
// The guest jmp_buf is only interpreted by these functions, so the layout is ours:
// [0..7] r4-r11, [8] sp, [9] lr, [10..25] d8-d15 (as S16-S31).
void save_jmp(Ctx& c) {
    u32 env = c.arg(0);
    for (int i = 0; i < 8; ++i) wr32(env + 4 * i, c.reg(4 + i));
    wr32(env + 32, c.sp());
    wr32(env + 36, c.lr());
    for (int i = 0; i < 16; ++i) wr32(env + 40 + 4 * i, (*c.ext)[16 + i]);
    c.ret(0);
}
void restore_jmp(Ctx& c) {
    u32 env = c.arg(0), val = c.arg(1);
    for (int i = 0; i < 8; ++i) c.set_reg(4 + i, rd32(env + 4 * i));
    c.set_reg(13, rd32(env + 32));
    c.set_reg(14, rd32(env + 36));  // the stub's BX LR returns to the setjmp call site
    for (int i = 0; i < 16; ++i) (*c.ext)[16 + i] = rd32(env + 40 + 4 * i);
    c.ret(val ? val : 1);
}
void h_setjmp(Ctx& c) { save_jmp(c); }
void h_sigsetjmp(Ctx& c) { save_jmp(c); }
void h_longjmp(Ctx& c) { restore_jmp(c); }
void h_siglongjmp(Ctx& c) { restore_jmp(c); }

// ---- C++ exception support ----------------------------------------------------------------------
void h___gnu_Unwind_Find_exidx(Ctx& c) {
    const Module* m = module_for_address(c.arg(0));
    if (!m || !m->exidx) { if (c.arg(1)) wr32(c.arg(1), 0); c.ret(0); return; }
    if (c.arg(1)) wr32(c.arg(1), m->exidx_count);
    c.ret(m->exidx);
}
void h___cxa_atexit(Ctx& c) { c.ret(0); }  // process teardown is handled by Android
void h___cxa_finalize(Ctx&) {}

// ---- process ------------------------------------------------------------------------------------
void h_abort(Ctx& c) { fatal("guest called abort() from %08x", c.lr()); }
void h___stack_chk_fail(Ctx& c) { fatal("guest stack protector tripped (from %08x)", c.lr()); }
void h_exit(Ctx& c) {
    LOGI("guest called exit(%d)", static_cast<int>(c.arg(0)));
    std::exit(static_cast<int>(c.arg(0)));
}
void h_raise(Ctx& c) { fatal("guest raised signal %u (from %08x)", c.arg(0), c.lr()); }
void h_syscall(Ctx& c) {
    if (c.arg(0) == 240 /* __NR_futex on ARM EABI */) { c.ret(0); return; }
    LOGW("guest syscall %u not supported", c.arg(0));
    set_guest_errno(38);
    c.ret(0xFFFFFFFFu);
}
void h_sigaction(Ctx& c) {
    if (c.arg(2)) std::memset(H(c.arg(2)), 0, 16);
    c.ret(0);
}
void h_alarm(Ctx& c) { c.ret(0); }
void h_getenv(Ctx& c) { c.ret(0); }
void h_geteuid(Ctx& c) { c.ret(10000); }
void h_getpwuid(Ctx& c) { c.ret(0); }
void h_getcwd(Ctx& c) {
    if (c.arg(1) < 2) { set_guest_errno(34); c.ret(0); return; }
    std::strcpy(HP<char>(c.arg(0)), "/");
    c.ret(c.arg(0));
}
void h_gethostname(Ctx& c) {
    std::snprintf(HP<char>(c.arg(0)), c.arg(1), "%s", "localhost");
    c.ret(0);
}

// ---- networking: deliberately unavailable (the port is offline) --------------------------------------
void net_fail(Ctx& c) { set_guest_errno(101 /* ENETUNREACH */); c.ret(0xFFFFFFFFu); }
void h_getaddrinfo(Ctx& c) { if (c.arg(3)) wr32(c.arg(3), 0); c.ret(4 /* EAI_FAIL */); }
void h_freeaddrinfo(Ctx&) {}
void h_inet_pton(Ctx& c) { c.ret(0); }
void h_inet_ntop(Ctx& c) { c.ret(0); }
void h_poll(Ctx& c) { c.ret(0); }

}  // namespace

void hle_register_sys() {
    g_tm_static = data_alloc(44);
    g_zone_utc = data_strdup("UTC");
    g_zone_local = data_strdup("");
    u32 guard = data_alloc(4);
    wr32(guard, std::random_device{}());
    hle_data("__stack_chk_guard", guard);
#define R(n) hle_func(#n, h_##n)
    R(__android_log_print); R(time); R(gettimeofday); R(clock_gettime); R(gmtime); R(localtime); R(gmtime_r);
    R(strftime); R(wcsftime); R(pthread_mutex_init); R(pthread_mutex_lock); R(pthread_mutex_unlock);
    R(pthread_key_create); R(pthread_key_delete); R(pthread_getspecific); R(pthread_setspecific); R(pthread_once);
    R(pthread_create); R(sem_init); R(sem_post); R(sem_wait); R(setjmp); R(sigsetjmp); R(longjmp); R(siglongjmp);
    R(__gnu_Unwind_Find_exidx); R(__cxa_atexit); R(__cxa_finalize); R(abort); R(__stack_chk_fail); R(exit); R(raise);
    R(syscall); R(sigaction); R(alarm); R(getenv); R(geteuid); R(getpwuid); R(getcwd); R(gethostname);
    R(getaddrinfo); R(freeaddrinfo); R(inet_pton); R(inet_ntop); R(poll);
#undef R
    for (const char* n : {"socket", "connect", "bind", "listen", "accept", "send", "sendto", "recv", "recvfrom",
                          "getsockopt", "setsockopt", "getpeername", "getsockname", "fcntl", "ioctl"})
        hle_func(n, net_fail);
}

}  // namespace compat

// ---- ARM Linux "kuser" helpers -----------------------------------------------------------------------
// ARMv5 code has no LDREX/STREX, so the original libraries call the kernel-provided
// helpers at fixed addresses in the vector page (e.g. __kuser_cmpxchg at 0xffff0fc0).
namespace compat {
namespace {
std::mutex g_kuser_mu;
constexpr u32 kCFlag = 1u << 29;
void k_cmpxchg(Ctx& c) {  // r0 = oldval, r1 = newval, r2 = ptr; r0 = 0 and C set on success
    std::lock_guard<std::mutex> lk(g_kuser_mu);
    bool ok = rd32(c.arg(2)) == c.arg(0);
    if (ok) wr32(c.arg(2), c.arg(1));
    c.ret(ok ? 0 : 1);
    c.cpsr = ok ? (c.cpsr | kCFlag) : (c.cpsr & ~kCFlag);
    c.cpsr_dirty = true;
}
void k_cmpxchg64(Ctx& c) {  // r0 = &oldval, r1 = &newval, r2 = ptr
    std::lock_guard<std::mutex> lk(g_kuser_mu);
    bool ok = rd64(c.arg(2)) == rd64(c.arg(0));
    if (ok) wr64(c.arg(2), rd64(c.arg(1)));
    c.ret(ok ? 0 : 1);
    c.cpsr = ok ? (c.cpsr | kCFlag) : (c.cpsr & ~kCFlag);
    c.cpsr_dirty = true;
}
void k_barrier(Ctx&) { std::atomic_thread_fence(std::memory_order_seq_cst); }
thread_local u32 t_tls_word = 0;
void k_get_tls(Ctx& c) {
    if (!t_tls_word) t_tls_word = gcalloc(1, 256);
    c.ret(t_tls_word);
}
}  // namespace

void hle_install_kuser_helpers() {
    constexpr u32 page = 0xFFFF0000;
    mem_commit(page, 0x1000);
    make_stub_at(page + 0xF60, "__kuser_cmpxchg64", k_cmpxchg64);
    make_stub_at(page + 0xFA0, "__kuser_memory_barrier", k_barrier);
    make_stub_at(page + 0xFC0, "__kuser_cmpxchg", k_cmpxchg);
    make_stub_at(page + 0xFE0, "__kuser_get_tls", k_get_tls);
    wr32(page + 0xFFC, 5);  // __kuser_helper_version
}
}  // namespace compat
