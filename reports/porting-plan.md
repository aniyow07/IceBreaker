# Porting plan — proposed, awaiting approval

## Evidence that drives the choice

1. The game layer (~2,000–2,500 functions, ~300 classes) exists only as ARMv5TE/Thumb-1 machine code in `libgame.so`. No source is available.
2. The engine is the open-source **cocos2d-1.0.1-x-0.12.0** (MIT). Box2D, TinyXML, Clipper, libpng, libcurl are also open source, but the exact versions/patches Nitrome used are not confirmed.
3. The native↔platform surface is small and well defined: **267 imported symbols** (libc, libm, pthread, sockets, zlib, ~60 GLES 1.1 calls, logging) and **24 `Java_*` exports + ~10 Java callbacks**.
4. `classes.dex` already contains the Java engine glue (`org.cocos2dx.lib.*`) that calls those `Java_*` names.

## Options

| Option | What it is | Fidelity | Effort / risk | Verdict |
|---|---|---|---|---|
| A. Rebuild from source | Recompile engine + game for arm64 | Perfect | **Impossible**: game source does not exist | Rejected |
| B. Reconstruct game code | Decompile ~2,500 functions to C++ and rebuild with cocos2d-x 0.12 for arm64 | Risky: every mistranslated function changes physics or rules | Very large (many months), needs a native decompiler | Fallback only |
| C. Replace dependencies | Rebuild `libcocosdenshion` from cocos2d-x source | Fine | Small, but does not solve `libgame.so` | Part of B only |
| **E. ARM32 compatibility layer (recommended)** | Ship an **arm64-v8a** host library that loads the *original, unmodified* `libgame.so`/`libcocosdenshion.so` into a 32-bit ARM CPU emulator (JIT), resolves their 267 imports to arm64 host implementations (libc, GLES 1.1 passthrough, pthreads), and bridges JNI both ways. The host library exports the same `Java_org_cocos2dx_lib_*` names, so the original `classes.dex` keeps working. | **Highest available**: the original game code runs instruction-for-instruction; assets untouched | Large but bounded: ELF loader + relocations, ~270 import thunks, guest JNIEnv, C++ exception unwinding (`__gnu_Unwind_Find_exidx`), GL pointer translation | **Recommended** |
| D. Faithful reimplementation | New engine using original assets | Lowest | Very large | Last resort |

This is the same idea used by "so_loader" ports of Android games to other platforms, plus a CPU JIT because arm64-only CPUs cannot execute 32-bit ARM instructions. It must be described as an emulated original binary, not a native recompilation.

### CPU emulator candidates (third-party source, needs approval)

- **dynarmic** (0BSD): ARMv6K/v7 A32+Thumb JIT with an arm64 host backend (oaknut). Fast enough for a 2013 2D game with Box2D. Pulls `mcl`, `oaknut`, `fmt`, `robin-map` (all permissive).
- **Unicorn 2** (GPLv2/LGPL parts, QEMU-based): simpler API, slower. Licence is acceptable for a private build.

## Deliverables and milestones

| # | Milestone | Output |
|---|---|---|
| M0 | Install SDK/NDK/build tools (approval needed) | working `aapt2/d8/apksigner/clang` |
| M1 | **ARM32 build**: original libs + original dex, manifest copy with targetSdk raised (to clear the Android 14+ install block), new signing key, v1+v2+v3 signatures | `dist/Icebreaker-ARM32.apk` for 32-bit-capable phones |
| M2 | arm64 host: ELF loader + relocations + init_array run inside the emulator; libc/pthread thunks | `libgame.so` initialises in a host test harness (Windows/WSL, no Android needed) |
| M3 | JNI bridge + GLES 1.1 passthrough + touch/lifecycle | first frame renders |
| M4 | Audio (via existing Java Cocos2dxMusic/Sound), save file, pause/resume | playable build |
| M5 | Static validation, signing, reports | `dist/Icebreaker-ARM64.apk` |

Package name stays `com.nitrome.icebreaker` (required by the hard-coded save path). A new signing key means the original must be uninstalled first. Store/analytics code is left as-is and will be offline; purchases are not bypassed.

## Testing constraint

There is no Android device or emulator connected. An x86 Android emulator cannot run arm64 code natively, so M3+ runtime checks need either a real phone over USB (`adb`) or the user sideloading test builds.

## Status (2026-10-14)

The user approved Option E, and it is implemented: `dist/Icebreaker-ARM64.apk` runs the original libraries under dynarmic and was play-tested on a Galaxy S23. M1 (the ARM32 build) needed one addition beyond the plan: a forwarding wrapper library so the game survives Android's early activity pause. The performance work done after the first device test is listed in `test-results.md`.
