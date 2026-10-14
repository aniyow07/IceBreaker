#include "guest_mem.h"

#include <cstdarg>
#include <map>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace compat {

u8* g_base = nullptr;

void fatal(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    LOGE("FATAL: %s", buf);
    std::abort();
}

static constexpr u64 kSpace = 1ull << 32;
static constexpr u32 kPage = 4096;

bool mem_init() {
    if (g_base) return true;
#if defined(_WIN32)
    g_base = static_cast<u8*>(VirtualAlloc(nullptr, kSpace, MEM_RESERVE, PAGE_NOACCESS));
#else
    void* p = mmap(nullptr, kSpace, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    g_base = p == MAP_FAILED ? nullptr : static_cast<u8*>(p);
#endif
    if (!g_base) {
        LOGE("could not reserve 4 GiB guest address space");
        return false;
    }
    mem_commit(kStubBase, kDataEnd - kStubBase);
    return true;
}

void mem_commit(u32 addr, u32 size) {
    u64 start = addr & ~u64(kPage - 1);
    u64 end = (u64(addr) + size + kPage - 1) & ~u64(kPage - 1);
#if defined(_WIN32)
    if (!VirtualAlloc(g_base + start, end - start, MEM_COMMIT, PAGE_READWRITE))
        fatal("commit %08x+%x failed", addr, size);
#else
    if (mprotect(g_base + start, end - start, PROT_READ | PROT_WRITE) != 0)
        fatal("commit %08x+%x failed", addr, size);
#endif
}

bool mem_is_committed(u32 addr) {
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(g_base + addr, &mbi, sizeof mbi)) return false;
    return mbi.State == MEM_COMMIT;
#else
    unsigned char vec = 0;
    return mincore(g_base + (addr & ~u32(kPage - 1)), kPage, &vec) == 0;
#endif
}

static void mem_release(u32 addr, u32 size) {
    // Give physical pages back without unmapping (contents become zero).
#if defined(_WIN32)
    VirtualAlloc(g_base + addr, size, MEM_RESET, PAGE_READWRITE);
#else
    madvise(g_base + addr, size, MADV_DONTNEED);
#endif
}

// ---- permanent data ---------------------------------------------------------
static std::mutex g_data_mu;
static u32 g_data_top = kDataBase;

u32 data_alloc(u32 size, u32 align) {
    std::lock_guard<std::mutex> lk(g_data_mu);
    u32 a = (g_data_top + align - 1) & ~(align - 1);
    if (a + size > kDataEnd) fatal("compat data region exhausted");
    g_data_top = a + size;
    std::memset(H(a), 0, size);
    return a;
}

u32 data_strdup(const char* s) {
    u32 n = static_cast<u32>(std::strlen(s)) + 1;
    u32 a = data_alloc(n, 1);
    std::memcpy(H(a), s, n);
    return a;
}

// ---- guest heap ---------------------------------------------------------------
// Block layout: [u32 usable_size][u32 magic][payload...]; payload is 8-aligned.
static constexpr u32 kHdr = 8;
static constexpr u32 kMagicUsed = 0x1CEB10C5;
static constexpr u32 kMagicFree = 0xF4EEB10C;
static constexpr u32 kSmallMax = 32768;
static constexpr u32 kCommitChunk = 16u << 20;

static std::mutex g_heap_mu;
static u32 g_heap_top = kHeapBase;
static u32 g_heap_committed = kHeapBase;
static std::vector<u32> g_small_free;          // per size class: head payload address
static std::multimap<u32, u32> g_large_free;   // usable size -> payload address

static u32 small_round(u32 n) {
    if (n <= 256) return (n + 15) & ~15u;
    if (n <= 2048) return (n + 63) & ~63u;
    return (n + 255) & ~255u;
}
static u32 small_class(u32 rounded) {
    if (rounded <= 256) return rounded / 16;
    if (rounded <= 2048) return 16 + (rounded - 256) / 64;
    return 16 + 28 + (rounded - 2048) / 256;
}

static u32 bump(u32 total) {
    u32 a = g_heap_top;
    if (u64(a) + total > kHeapEnd) return 0;
    g_heap_top = a + total;
    if (g_heap_top > g_heap_committed) {
        u32 want = (g_heap_top - g_heap_committed + kCommitChunk - 1) / kCommitChunk * kCommitChunk;
        if (u64(g_heap_committed) + want > kHeapEnd) want = kHeapEnd - g_heap_committed;
        mem_commit(g_heap_committed, want);
        g_heap_committed += want;
    }
    return a;
}

static u32 alloc_locked(u32 size) {
    if (size == 0) size = 1;
    if (size <= kSmallMax) {
        u32 r = small_round(size);
        u32 c = small_class(r);
        if (g_small_free.size() <= c) g_small_free.resize(c + 1, 0);
        if (u32 p = g_small_free[c]) {
            g_small_free[c] = rd32(p);
            wr32(p - 4, kMagicUsed);
            return p;
        }
        u32 b = bump(r + kHdr);
        if (!b) return 0;
        wr32(b, r);
        wr32(b + 4, kMagicUsed);
        return b + kHdr;
    }
    u32 r = (size + kPage - 1) & ~(kPage - 1);
    auto it = g_large_free.lower_bound(r);
    if (it != g_large_free.end() && it->first <= r + r / 2 + (1u << 20)) {
        u32 p = it->second;
        g_large_free.erase(it);
        wr32(p - 4, kMagicUsed);
        return p;
    }
    // Keep the payload page-aligned for large blocks so release works page-wise.
    u32 b = bump(r + kPage);
    if (!b) return 0;
    u32 p = b + kPage;
    wr32(p - 8, r);
    wr32(p - 4, kMagicUsed);
    return p;
}

u32 gmalloc(u32 size) {
    std::lock_guard<std::mutex> lk(g_heap_mu);
    u32 p = alloc_locked(size);
    if (!p) LOGE("guest heap exhausted (request %u)", size);
    return p;
}

u32 gcalloc(u32 n, u32 size) {
    u64 total = u64(n) * size;
    if (total > 0xFFFFFFF0ull) return 0;
    u32 p = gmalloc(static_cast<u32>(total));
    if (p) std::memset(H(p), 0, static_cast<size_t>(total));
    return p;
}

u32 gusable(u32 p) { return p ? rd32(p - 8) : 0; }

void gfree(u32 p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(g_heap_mu);
    if (rd32(p - 4) != kMagicUsed) {
        LOGE("guest free of invalid or already-freed block %08x (magic %08x)", p, rd32(p - 4));
        return;
    }
    u32 r = rd32(p - 8);
    wr32(p - 4, kMagicFree);
    if (r <= kSmallMax) {
        u32 c = small_class(r);
        wr32(p, g_small_free[c]);
        g_small_free[c] = p;
    } else {
        if (r >= (1u << 20)) mem_release(p, r);
        g_large_free.emplace(r, p);
    }
}

u32 grealloc(u32 p, u32 size) {
    if (!p) return gmalloc(size);
    if (size == 0) { gfree(p); return 0; }
    u32 old = gusable(p);
    if (size <= old) return p;
    u32 n = gmalloc(size);
    if (!n) return 0;
    std::memcpy(H(n), H(p), old);
    gfree(p);
    return n;
}

u32 gstrdup(const char* s) {
    u32 n = static_cast<u32>(std::strlen(s)) + 1;
    u32 a = gmalloc(n);
    if (a) std::memcpy(H(a), s, n);
    return a;
}

static std::mutex g_stack_mu;
static u32 g_stack_next = kStackBase;

u32 stack_alloc() {
    std::lock_guard<std::mutex> lk(g_stack_mu);
    if (g_stack_next + kStackSize + kPage > kStackEnd) fatal("out of guest stacks");
    u32 lo = g_stack_next + kPage;  // leave an unmapped guard page below each stack
    g_stack_next += kStackSize + kPage;
    mem_commit(lo, kStackSize);
    return lo + kStackSize;
}

}  // namespace compat
