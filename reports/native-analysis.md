# Native analysis — stage 1 (ELF-level, no decompiler yet)

**Tooling used:** REA `open_binary` (identity only: format ELF, arch ARM, SHA-256 `f7946f81a84e9f565f4fd32c851f3bbfc5eba48c1d4e61a970ffbab2b22c72a1`), GNU `readelf` 2.45, `strings`, and `scripts/classify_symbols.py`. **REA native analysis (decompilation, xrefs, call graphs) has not been run**: no provider can open ARM ELF on this Windows host (see `environment.md`). Nothing below comes from decompiled code.

## libgame.so

| Fact | Value | Evidence |
|---|---|---|
| ELF | ELF32, little-endian, `ET_DYN`, EABI5, entry 0 | `readelf -h` |
| ISA | ARMv5TE, Thumb-1, VFPv2, `Tag_ABI_align_needed: 8-byte`, wchar_t 4 | `readelf -A` |
| Code mode | Exported function addresses are odd (e.g. `JNI_OnLoad` = `0x000d74fd`) → most game/engine code is **Thumb-1** | `analysis/libgame.dynsym.txt` |
| Compiler | GCC 4.6 (20120106 prerelease) + GCC 4.4.0 objects; gold linker | `.comment`, `.note.gnu.gold-version` |
| Engine | **cocos2d-1.0.1-x-0.12.0** (cocos2d-x 0.12, GLES 1.1 branch) | embedded string |
| Bundled libs (statically linked) | Box2D (~977 `b2*` symbols), TinyXML, ClipperLib, libpng 1.4.4, libjpeg, libcurl 7.21.4, zlib (via system `libz`), gnustl/libsupc++ (`__cxxabiv1`) | symbols/strings |
| Sections | `.text` 0x218320 bytes at 0xd74d8; `.ARM.exidx`/`.ARM.extab` present (C++ exceptions); `.init_array` 310 entries; `.bss` 0xe41c | `readelf -S` |
| Dynamic flags | `BIND_NOW`, `SYMBOLIC`; REL relocations (22,823 relative + 2088 B of PLT) ; **no TEXTREL** | `readelf -d` |
| Stripping | Dynamic symbols kept: **8,881 named functions**; no `.symtab`/DWARF | `readelf -S`, dynsym |
| Packing / obfuscation | None observed (standard sections, readable strings) | |
| Imports | 267 symbols from libc/libm/libdl/liblog/libz/libGLESv1_CM/libstdc++ — full list in `analysis/libgame.classes.txt` | |
| Legacy bionic symbols imported | `__sF`, `_ctype_`, `_tolower_tab_`, `_toupper_tab_`, `__errno` — still exported by 32-bit bionic for compatibility | dynsym |
| Hard-coded path | `/data/data/com.nitrome.icebreaker/save.xml` | strings |

### JNI surface

`JNI_OnLoad` at `0x000d74fd`. 24 `Java_org_cocos2dx_lib_*` exports bound by name (no `RegisterNatives` evidence yet), including:

- Renderer: `nativeInit` (`0x000d750d`), `nativeRender` (`0x00210055`), `nativeOnPause/Resume`, `nativeTouchesBegin/Move/End/Cancel`, `nativeKeyDown`, `setMetrics`, text-input methods.
- Activity: `nativeSetPaths` (`0x00210151`), `doTransition`, `setPressed`, `isAmazonStore`, `purchaseConfirmed`, `restorePurchase`, `setPrice`, `recheckAchievements`, `confirmDelete`.
- `Cocos2dxBitmap_nativeInitBitmapDC`, `Cocos2dxAccelerometer_onSensorChanged`.

Native→Java callbacks use only a handful of classes (`Cocos2dxActivity`, `Cocos2dxBitmap`, `Cocos2dxGLSurfaceView`, `Cocos2dxRenderer`, `com/nitrome/util/ExtrasHelper`, `com/nitrome/util/FlurryHelper`, `java/util/HashMap`) and ~10 method signatures.

### Game code size

Of 8,881 functions, the Nitrome game layer (classes outside cocos2d, Box2D, TinyXML, Clipper, curl/png/jpeg, libstdc++) is roughly 2,000–2,500 functions across ~300 classes (`SavedData` 120, `Hud` 55, `LevelLayer` 40, `MapScreen` 39, `Camera` 38, bosses, enemies, `Water`, `Rope`, `DeformableStructure`…). Breakdown: `analysis/libgame.classes.txt`. This is the part with **no available source**.

## libcocosdenshion.so

ELF32 ARMv5TE, 199 functions: `CocosDenshion::SimpleAudioEngine` (28 methods) + `JNI_OnLoad` (`0x00005d61`) and C helpers that call static methods on `org/cocos2dx/lib/Cocos2dxActivity`. Imports 27 libc/libstdc++ symbols only. This matches the open-source cocos2d-x Android `SimpleAudioEngine` JNI wrapper: audio is actually played by Java (`Cocos2dxMusic` = MediaPlayer, `Cocos2dxSound` = SoundPool).

## Pending (needs a native disassembler/decompiler)

Startup path from `nativeInit` → `AppDelegate::applicationDidFinishLaunching` (`0x000fe831`), main loop, touch dispatch, physics step, level loading, save I/O and audio calls are identified by **symbol names only** so far. Confirming them needs NDK `llvm-objdump` or REA+Ghidra (see `porting-plan.md`).

## Stage 2 — findings from Ghidra and from running the code (2026-10-14)

Decompilation tool: Ghidra 12.1.4 headless (addresses below are Ghidra addresses = ELF vaddr + 0x10000). Runtime facts come from running the original code under the compatibility layer on the Galaxy S23 and in the PC harness.

| Function / fact | Address | Evidence | Confidence |
|---|---|---|---|
| `JNI_OnLoad` only stores the JavaVM (`JniHelper::setJavaVM`) and returns `0x10004` | `0x000e74fc` | decompile; runtime returned `0x10004` | Verified |
| `nativeInit(w,h)`: the first call creates `CCEGLView`, sets the frame size, creates `AppDelegate` and runs `CCApplication::run`; later calls reload all textures and call `NitromeFeature::goForeground` | `0x000e750c` | `analysis/decomp/libgame.so_000e750c.c` | Verified |
| `AppDelegate::applicationDidFinishLaunching` seeds `srand48(time)`, enables retina unless iPad, sets the **animation interval to 1/30 s** and pushes the first scene | `0x0010e830` | decompile + constant at file offset `0xfe9f0` = 0.0333… | Verified |
| `nativeRender` = `CCDirector::sharedDirector()->mainLoop()` (vtable +0x24) | `0x00220054` | decompile | Verified |
| `nativeSetPaths` reads the APK path (`GetStringUTFChars`, JNI table +0x2a4) into `CCFileUtils::setResourcePath` | `0x00220150` | decompile | Verified |
| Asset loading: `CCFileUtils::getFileData` → `getFileDataFromZip` (minizip `unzOpen/unzLocateFile/unzReadCurrentFile` on the APK); ~8.2 M `fread` calls per screen load | `0x0021caa4`, `0x0021c924` | decompile + runtime import counts | Verified |
| Atomic operations use the ARM Linux kuser helpers (`__kuser_cmpxchg` at `0xffff0fc0`) | e.g. caller lr `0x022ec7c8` | runtime fault at `0xffff0fc0` before the helpers were provided | Verified |
| `pthread_create` is only called from `CCTextureCache::addImageAsync`, which has no callers | | Ghidra callers | Verified (static) |
| Network: libcurl is used only by `NitromeImageLoader` (news/feature images) | | Ghidra callers of `curl_easy_init` | Verified (static) |
| JNI usage matches the standard function table (e.g. +0x1c4 GetStaticMethodID, +0x238 CallStaticVoidMethodV, +0x320 GetByteArrayRegion) | | decompiles of JniHelper / wrappers | Verified |
| `_ctype_` is a pointer variable (double dereference) | `TiXmlBase::IsWhiteSpace` | decompile | Verified |
| Physics, collision and level logic | | not decompiled; they run unchanged under emulation | Not reconstructed (not needed) |
