#include "cpu.h"

#include <algorithm>
#include <chrono>
#include <memory>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <mutex>
#include <string>
#include <vector>

#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>

#include "guest_mem.h"

namespace compat {

namespace {

constexpr u32 kSvcReturn = 0xFFFFFF;   // "return to host" marker
constexpr u32 kArmSvc = 0xEF000000;    // SVC #imm24 (ARM, cond=AL)
constexpr u32 kArmBxLr = 0xE12FFF1E;   // BX LR

struct StubEntry {
    const char* name;
    HleFn fn;
};
std::mutex g_stub_mu;
std::vector<StubEntry> g_stubs;  // index = SVC number
u32 g_stub_top = kStubBase;
u32 g_return_stub = 0;
bool g_trace = std::getenv("COMPAT_TRACE") != nullptr;
bool g_heapcheck = std::getenv("COMPAT_HEAPCHECK") != nullptr;
// Per-import timing: env COMPAT_PROFILE on the PC, `adb shell setprop debug.icebreaker.profile 1` on Android.
bool read_profile_flag() {
#if defined(__ANDROID__)
    char v[PROP_VALUE_MAX] = {};
    return __system_property_get("debug.icebreaker.profile", v) > 0 && v[0] == '1';
#else
    return std::getenv("COMPAT_PROFILE") != nullptr;
#endif
}
bool g_profile = read_profile_flag();

class GuestCpu final : public Dynarmic::A32::UserCallbacks {
public:
    explicit GuestCpu(size_t cache_bytes) {
        Dynarmic::A32::UserConfig cfg{};
        cfg.callbacks = this;
        cfg.arch_version = Dynarmic::A32::ArchVersion::v5TE;
        if (!std::getenv("COMPAT_NO_FASTMEM")) cfg.fastmem_pointer = reinterpret_cast<uintptr_t>(g_base);
        cfg.enable_cycle_counting = false;
        cfg.code_cache_size = cache_bytes;
        cfg.define_unpredictable_behaviour = true;
        jit = std::make_unique<Dynarmic::A32::Jit>(cfg);
    }

    // Memory accesses that miss the fastmem fast path land here.
    void check(u32 a) {
        if (g_trace && !mem_is_committed(a)) fatal("guest access to unmapped address %08x (pc~%08x lr=%08x)", a, jit->Regs()[15], jit->Regs()[14]);
    }
    u8 MemoryRead8(u32 a) override { check(a); return g_base[a]; }
    u16 MemoryRead16(u32 a) override { check(a); return rd16(a); }
    u32 MemoryRead32(u32 a) override { check(a); return rd32(a); }
    u64 MemoryRead64(u32 a) override { check(a); return rd64(a); }
    void MemoryWrite8(u32 a, u8 v) override { check(a); g_base[a] = v; }
    void MemoryWrite16(u32 a, u16 v) override { check(a); wr16(a, v); }
    void MemoryWrite32(u32 a, u32 v) override { check(a); wr32(a, v); }
    void MemoryWrite64(u32 a, u64 v) override { check(a); wr64(a, v); }

    void InterpreterFallback(u32 pc, size_t n) override {
        fatal("interpreter fallback requested at %08x (%zu instructions)", pc, n);
    }

    void CallSVC(u32 swi) override {
        if (swi == kSvcReturn) {
            returned = true;
            jit->HaltExecution();
            return;
        }
        if (swi >= g_stubs.size()) fatal("guest SVC #%x with no handler (lr=%08x)", swi, jit->Regs()[14]);
        if (g_trace) LOGI("svc %s r0=%08x r1=%08x r2=%08x lr=%08x", g_stubs[swi].name, jit->Regs()[0], jit->Regs()[1], jit->Regs()[2], jit->Regs()[14]);
        Ctx ctx(jit->Regs());
        ctx.ext = &jit->ExtRegs();
        ctx.svc = swi;
        ctx.cpsr = jit->Cpsr();
        if (g_profile) {
            auto t0 = std::chrono::steady_clock::now();
            g_stubs[swi].fn(ctx);
            u64 ns = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
            CpuStats& st = cpu_stats();
            ++st.svc_calls;
            st.svc_ns += ns;
            if (swi < 512) { ++st.per_name[swi]; st.per_name_ns[swi] += ns; }
        } else {
            g_stubs[swi].fn(ctx);
        }
        if (ctx.cpsr_dirty) jit->SetCpsr(ctx.cpsr);
        if (g_trace) LOGI("svc done r0=%08x", jit->Regs()[0]);
#if defined(_WIN32)
        if (g_heapcheck && _heapchk() != _HEAPOK) fatal("host heap corrupted after import %s (lr=%08x)", g_stubs[swi].name, jit->Regs()[14]);
#endif
    }

    void ExceptionRaised(u32 pc, Dynarmic::A32::Exception e) override {
        using E = Dynarmic::A32::Exception;
        switch (e) {
        case E::PreloadData:
        case E::PreloadDataWithIntentToWrite:
        case E::PreloadInstruction:
        case E::Yield:
        case E::SendEvent:
        case E::SendEventLocal:
        case E::WaitForEvent:
        case E::WaitForInterrupt:
            return;  // hints
        default:
            break;
        }
        const auto& r = jit->Regs();
        fatal("guest CPU exception %d at pc=%08x lr=%08x sp=%08x r0=%08x", static_cast<int>(e), pc, r[14],
              r[13], r[0]);
    }

    void AddTicks(u64) override {}
    u64 GetTicksRemaining() override { return 1ull << 40; }

    std::unique_ptr<Dynarmic::A32::Jit> jit;
    bool returned = false;
};

struct ThreadCpus {
    std::vector<std::unique_ptr<GuestCpu>> levels;
    int depth = 0;          // number of guest calls in progress on this thread
    u32 stack_top = 0;
};
thread_local ThreadCpus t_cpus;

}  // namespace

static thread_local CpuStats t_stats;
CpuStats& cpu_stats() { return t_stats; }

void cpu_stats_dump(const char* tag) {
    if (!g_profile) return;
    CpuStats& st = t_stats;
    std::vector<std::pair<u64, u32>> top;
    for (u32 i = 0; i < 512 && i < g_stubs.size(); ++i)
        if (st.per_name[i]) top.push_back({st.per_name_ns[i], i});
    std::sort(top.rbegin(), top.rend());
    std::string line;
    for (size_t k = 0; k < top.size() && k < 8; ++k) {
        char b[96];
        std::snprintf(b, sizeof b, " %s=%llux/%.1fms", g_stubs[top[k].second].name,
                      static_cast<unsigned long long>(st.per_name[top[k].second]), top[k].first / 1e6);
        line += b;
    }
    LOGI("%s imports: %llu calls, %.1f ms in host;%s", tag, static_cast<unsigned long long>(st.svc_calls), st.svc_ns / 1e6, line.c_str());
    st = CpuStats{};
}

u32 make_stub(const char* name, HleFn fn) {
    std::lock_guard<std::mutex> lk(g_stub_mu);
    u32 svc = static_cast<u32>(g_stubs.size());
    g_stubs.push_back({name, fn});
    u32 a = g_stub_top;
    if (a + 8 > kStubEnd) fatal("stub region exhausted");
    wr32(a, kArmSvc | svc);
    wr32(a + 4, kArmBxLr);
    g_stub_top += 8;
    return a;
}

void make_stub_at(u32 addr, const char* name, HleFn fn) {
    std::lock_guard<std::mutex> lk(g_stub_mu);
    u32 svc = static_cast<u32>(g_stubs.size());
    g_stubs.push_back({name, fn});
    wr32(addr, kArmSvc | svc);
    wr32(addr + 4, kArmBxLr);
}

const char* stub_name(u32 svc) { return svc < g_stubs.size() ? g_stubs[svc].name : "?"; }

void cpu_init() {
    std::lock_guard<std::mutex> lk(g_stub_mu);
    if (g_return_stub) return;
    g_return_stub = g_stub_top;
    wr32(g_return_stub, kArmSvc | kSvcReturn);
    wr32(g_return_stub + 4, kArmBxLr);
    g_stub_top += 8;
}

std::array<u32, 16>* current_regs() {
    if (t_cpus.depth == 0) return nullptr;
    return &t_cpus.levels[t_cpus.depth - 1]->jit->Regs();
}

std::array<u32, 64>* current_ext_regs() {
    if (t_cpus.depth == 0) return nullptr;
    return &t_cpus.levels[t_cpus.depth - 1]->jit->ExtRegs();
}

u64 call_guest(u32 addr, std::initializer_list<u32> args) { return call_guest_v(addr, args.begin(), args.size()); }

u64 call_guest_v(u32 addr, const u32* av, size_t n) {
    ThreadCpus& tc = t_cpus;
    if (!tc.stack_top) tc.stack_top = stack_alloc();
    u32 sp;
    if (tc.depth == 0) {
        sp = tc.stack_top;
    } else {
        // Nested call: continue below the interrupted guest frame.
        sp = (tc.levels[tc.depth - 1]->jit->Regs()[13] - 256) & ~7u;
    }
    if (static_cast<int>(tc.levels.size()) <= tc.depth)
        tc.levels.push_back(std::make_unique<GuestCpu>(tc.depth == 0 ? (64u << 20) : (16u << 20)));
    GuestCpu& cpu = *tc.levels[tc.depth];

    if (n > 4) {
        u32 extra = static_cast<u32>(n - 4);
        sp = (sp - 4 * extra) & ~7u;
        for (u32 i = 0; i < extra; ++i) wr32(sp + 4 * i, av[4 + i]);
    }
    auto& r = cpu.jit->Regs();
    for (int i = 0; i < 13; ++i) r[i] = 0;
    for (size_t i = 0; i < n && i < 4; ++i) r[i] = av[i];
    r[13] = sp;
    r[14] = g_return_stub;
    r[15] = addr & ~1u;
    cpu.jit->SetCpsr(0x10 | ((addr & 1) ? 0x20 : 0));  // USR mode, T bit for Thumb targets
    cpu.jit->SetFpscr(0);
    cpu.returned = false;

    if (g_trace) LOGI("call_guest %08x depth %d sp %08x", addr, tc.depth, sp);
    ++tc.depth;
    while (true) {
        cpu.jit->Run();
        if (cpu.returned) break;
        // A halt we did not request (should not happen); resume.
        cpu.jit->ClearHalt(Dynarmic::HaltReason::UserDefined1);
    }
    --tc.depth;
    cpu.jit->ClearHalt(Dynarmic::HaltReason::UserDefined1);
    return u64(r[0]) | (u64(r[1]) << 32);
}

}  // namespace compat
