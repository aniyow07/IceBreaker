# Proof of concept plan — ARM32 compatibility layer

## What the layer is

| Part | Choice | Evidence / status |
|---|---|---|
| CPU | **dynarmic** (azahar-emu fork, commit `a46601580d55`, 0BSD) — a JIT that translates 32-bit ARM/Thumb guest code to host machine code | README lists guest `v5TE` and hosts `x86-64` and `AArch64` (read from `native/third_party/dynarmic/README.md`). Built successfully for x86-64 with MinGW on this PC. |
| ARMv5TE specifics | Configured as `ArchVersion::v5TE` (matches `Tag_CPU_arch: v5TE` of `libgame.so`) | Source check: v5+ load-to-PC interworking is modelled (`LoadWritePC`, `a32_ir_emitter.cpp`). **Unaligned LDR rotation (ARMv5 legacy behaviour) is not modelled**; dynarmic performs a plain unaligned access. That is how ARMv6/v7 Android phones (with unaligned access enabled) ran this same binary in 2013, so it should match the original platform's real behaviour — inference, unverified. |
| Loader | Own ELF32 loader (`native/compat/elf_loader.cpp`): maps both original `.so` files unchanged into a 4 GiB guest space, applies their 4 relocation types (`RELATIVE`, `ABS32`, `GLOB_DAT`, `JUMP_SLOT` — the only ones present), runs their constructors | Relocation inventory from `readelf -r` |
| Android/libc dependencies | Each of the 267 imported symbols (`analysis/libgame.imports.txt`) gets a host implementation reached through an `SVC` trampoline: libc/libm/stdio/pthread/time/setjmp/zlib/GLES 1.1. Networking is deliberately disabled (offline). | Callers of risky imports checked in Ghidra (`analysis/libgame.import-callers.tsv`): e.g. `pthread_create` only from `addImageAsync`, which nothing calls |
| Graphics | GLES 1.1 calls forwarded to the device's real `libGLESv1_CM` with pointer translation | Not a rewrite of the renderer |
| Audio, touch, lifecycle | The **original `classes.dex`** stays in charge (GLSurfaceView, MediaPlayer/SoundPool, touch queue, pause/resume). Its `native*` methods land in the new arm64 library, which forwards them into the original `Java_org_cocos2dx_*` functions; the game's JNI calls back to Java go through a bridged `JNIEnv` | JNI table offsets used by the game match the standard table (e.g. `0x1c4` GetStaticMethodID, `0x238` CallStaticVoidMethodV, `0x320` GetByteArrayRegion) |
| Packaging | One APK: original `classes.dex`, `resources.arsc`, `res/`, `assets/` unchanged; manifest raised to a modern targetSdk; `lib/arm64-v8a/libicebreaker_compat.so` (the layer + dynarmic, built with NDK r28c) plus the original 32-bit `.so` files stored as **data assets** (they are never loaded by Android's linker) | |

## PoC milestones (each must pass before the next)

1. **PC harness, x86-64 JIT:** load the original libraries, run constructors, `JNI_OnLoad`, `nativeSetPaths`, `nativeInit`, then render frames through desktop OpenGL in a hidden window; save screenshots; inject touches; measure ms/frame. Audio calls are captured at the JNI boundary.
2. **Android emulator (x86_64 system image) with the same layer built for x86_64:** proves the Java↔native bridge, real GLES 1.1, real audio, touch and pause/resume with the original `classes.dex`. Needs the Android Emulator + one system image (about 2–3 GB).
3. **arm64 build:** same source with dynarmic's AArch64 backend; static validation here. Runtime on arm64 needs a real phone, because x86 PCs cannot run arm64 Android images at usable speed.

## Licensing and redistribution

dynarmic 0BSD; fmt, mcl, oaknut, robin-map, Zydis/Zycore MIT; Boost (headers only) BSL-1.0; zlib from the Android system. All permit private builds. The game code and assets remain Nitrome's copyright, so the APK is for private preservation only (the user's stated intent), and it is signed with a new local key.
