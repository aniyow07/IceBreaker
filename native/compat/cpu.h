#pragma once
#include <array>
#include <cstddef>
#include <initializer_list>

#include "common.h"

namespace compat {

// View of the guest CPU handed to an import implementation (HLE function).
// Arguments follow the ARM AAPCS soft-float convention used by the original
// binaries: r0-r3 then the stack; 64-bit values are 8-byte aligned in pairs.
class Ctx {
public:
    explicit Ctx(std::array<u32, 16>& regs) : regs_(regs) {}
    u32 reg(int i) const { return regs_[i]; }
    void set_reg(int i, u32 v) { regs_[i] = v; }
    u32 sp() const { return regs_[13]; }
    u32 lr() const { return regs_[14]; }

    // Word-sized argument by slot index (0-3 registers, then stack words).
    u32 arg(int slot) const { return slot < 4 ? regs_[slot] : rd32(regs_[13] + 4 * (slot - 4)); }
    float argf(int slot) const { return f32_from(arg(slot)); }
    // 64-bit argument occupying slots (slot, slot+1); caller supplies the aligned slot.
    u64 arg64(int slot) const { return u64(arg(slot)) | (u64(arg(slot + 1)) << 32); }
    double argd(int slot) const { return f64_from(arg(slot), arg(slot + 1)); }

    void ret(u32 v) { regs_[0] = v; }
    void ret64(u64 v) { regs_[0] = u32(v); regs_[1] = u32(v >> 32); }
    void retf(float f) { regs_[0] = bits_from(f); }
    void retd(double d) { ret64(bits_from(d)); }

    std::array<u32, 16>& regs() { return regs_; }
    std::array<u32, 64>* ext = nullptr;  // VFP registers (S0-S63 view)
    u32 svc = 0;                         // which import stub is running
    u32 cpsr = 0;                        // guest CPSR; set cpsr_dirty to write it back
    bool cpsr_dirty = false;

private:
    std::array<u32, 16>& regs_;
};

using HleFn = void (*)(Ctx&);

// Allocates an ARM-mode stub "SVC #n; BX LR" in guest memory that runs `fn`.
u32 make_stub(const char* name, HleFn fn);
// Writes such a stub at a fixed guest address (used for the ARM Linux kuser helpers).
void make_stub_at(u32 addr, const char* name, HleFn fn);
const char* stub_name(u32 svc);

// Calls guest code at `addr` (bit 0 set = Thumb) with up to N word arguments.
// Re-entrant: a call made while guest code is already running on this thread
// (from inside an HLE function) runs on a nested CPU instance that continues
// on the same guest stack below the caller's frame.
u64 call_guest(u32 addr, std::initializer_list<u32> args = {});
u64 call_guest_v(u32 addr, const u32* args, size_t n);

// Current guest register file of the innermost running CPU on this thread
// (used by setjmp/longjmp style imports).
std::array<u32, 16>* current_regs();
std::array<u32, 64>* current_ext_regs();

void cpu_init();

// Profiling counters (per thread): import calls and nanoseconds spent in their host implementations.
struct CpuStats { u64 svc_calls = 0; u64 svc_ns = 0; u64 per_name[512] = {}; u64 per_name_ns[512] = {}; };
CpuStats& cpu_stats();
void cpu_stats_dump(const char* tag);

}  // namespace compat
