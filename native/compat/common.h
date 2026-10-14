// Shared definitions for the Icebreaker ARM32 compatibility layer.
// The layer runs the original, unmodified 32-bit ARM libraries under an ARM CPU JIT
// (dynarmic) and services their imports with host implementations.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#define CLOG(prio, ...) __android_log_print(prio, "IcebreakerCompat", __VA_ARGS__)
#define LOGI(...) CLOG(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOGW(...) CLOG(ANDROID_LOG_WARN, __VA_ARGS__)
#define LOGE(...) CLOG(ANDROID_LOG_ERROR, __VA_ARGS__)
#else
#define LOGI(...) (std::fprintf(stderr, "[I] " __VA_ARGS__), std::fputc('\n', stderr))
#define LOGW(...) (std::fprintf(stderr, "[W] " __VA_ARGS__), std::fputc('\n', stderr))
#define LOGE(...) (std::fprintf(stderr, "[E] " __VA_ARGS__), std::fputc('\n', stderr))
#endif

namespace compat {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

[[noreturn]] void fatal(const char* fmt, ...);

// Guest (32-bit) address <-> host pointer. The whole 4 GiB guest space is one
// contiguous host reservation, so a guest address is an offset from g_base.
extern u8* g_base;
inline void* H(u32 a) { return g_base + a; }
template <typename T> inline T* HP(u32 a) { return reinterpret_cast<T*>(g_base + a); }
inline const char* HS(u32 a) { return a ? reinterpret_cast<const char*>(g_base + a) : nullptr; }
inline u32 G(const void* p) { return static_cast<u32>(static_cast<const u8*>(p) - g_base); }

inline u32 rd32(u32 a) { u32 v; std::memcpy(&v, g_base + a, 4); return v; }
inline void wr32(u32 a, u32 v) { std::memcpy(g_base + a, &v, 4); }
inline u16 rd16(u32 a) { u16 v; std::memcpy(&v, g_base + a, 2); return v; }
inline void wr16(u32 a, u16 v) { std::memcpy(g_base + a, &v, 2); }
inline u64 rd64(u32 a) { u64 v; std::memcpy(&v, g_base + a, 8); return v; }
inline void wr64(u32 a, u64 v) { std::memcpy(g_base + a, &v, 8); }

inline float f32_from(u32 bits) { float f; std::memcpy(&f, &bits, 4); return f; }
inline u32 bits_from(float f) { u32 b; std::memcpy(&b, &f, 4); return b; }
inline double f64_from(u32 lo, u32 hi) { u64 b = (u64(hi) << 32) | lo; double d; std::memcpy(&d, &b, 8); return d; }
inline u64 bits_from(double d) { u64 b; std::memcpy(&b, &d, 8); return b; }

}  // namespace compat
