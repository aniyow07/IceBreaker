# Final delivery — Icebreaker Android preservation port

Private preservation builds only. The game's code and assets remain Nitrome's; nothing here is for redistribution.

## Deliverables

| File | ABI | Min / target Android | Size | SHA-256 |
|---|---|---|---|---|
| `C:\REA\dist\Icebreaker-ARM64.apk` (primary) | `arm64-v8a` only | 6.0 (API 23) / 9 (API 28) | 46,023,576 B | `26b8241a53a9654886bbff9ae3eabf4552dbc22ae7cee0581fd4c0e93780d59d` |
| `C:\REA\dist\Icebreaker-ARM32.apk` | `armeabi-v7a` only | 5.0 (API 21) / 7.0 (API 24) | 45,085,514 B | `6ff2949271a4aa8a53a3902fd581838ad2b709408217f58935f3115620739f1d` |

Package `com.nitrome.icebreaker`, versionName 1.0.2, versionCode 3 (unchanged; the game hard-codes `/data/data/com.nitrome.icebreaker/save.xml`).

## What each APK is

**ARM64 (primary).** The original, unmodified 32-bit `libgame.so` and `libcocosdenshion.so` (shipped as data in `assets/original-armeabi/`) run inside **dynarmic**, an ARM→AArch64 JIT, in a new arm64 library `lib/arm64-v8a/libgame.so` built from `native/`. That library loads and relocates the original ELF files, implements their 267 imports (libc, libm, stdio, pthread, time, setjmp, zlib, GLES 1.1, logging; networking deliberately offline), provides the ARM Linux kuser helpers the ARMv5 code needs for atomics, and bridges JNI both ways. The original `classes.dex`, `resources.arsc`, `res/` and `assets/` are byte-identical to the original APK. **This is the original game code under emulation, not a native recompilation.**

**ARM32.** The original libraries run natively on 32-bit-capable devices. A small armeabi-v7a wrapper `libgame.so` (`native/arm32_shim/`) forwards every native method to the original (renamed `libicebreaker_original.so`, byte-identical) and only drops renderer/touch calls that arrive before the first `nativeInit`.

## Changes relative to the original behaviour (all evidence-backed)

| Change | Where | Why / evidence |
|---|---|---|
| Renderer/touch calls ignored before the first `nativeInit` | both | Original code crashed (`SIGSEGV` null read in `nativeOnPause`) when Android 16 paused the activity before the GL surface existed — `tests/device-s23/arm32/logcat-launch*.txt` |
| `targetSdkVersion` raised (28 ARM64, 24 ARM32), `minSdkVersion` 23 / 21, `<uses-library org.apache.http.legacy>` (ARM64) | manifest copies | Android 14+ refuses targetSdk < 23; targetSdk < 28 shows the "built for an older version of Android" dialog (`AppWarnings` log). Original Java code references 41 Apache HTTP classes (`analysis/dex.strings.txt`). |
| `CCFileUtils::getFileDataFromZip` served from an indexed, memory-mapped APK (in-memory substitution, file untouched) | ARM64 | Original minizip lookup cost ~8.2 M emulated `fread` calls per screen load (1.5–2.8 s freezes on the S23). Substitute returns the same bytes: all 1,413 APK entries verified identical by size + CRC-32 (`tests/zipindex/`). Disable with `adb shell setprop debug.icebreaker.nohooks 1`. |
| Frame pacing: each frame is held until the game's requested interval has passed | ARM64 | The game requests 1/30 s (`AppDelegate::applicationDidFinishLaunching`, constant at libgame.so `0xfe9f0` = 0.0333…). The original Java pacing (`Cocos2dxRenderer.onDrawFrame`, sleeps 2×(interval−elapsed); REA/JADX evidence `ev_a3c9c47b…`) alternated short/long frames on the 120 Hz S23. |
| `Cocos2dxActivity.showPreloadCicle` not called | ARM64 | Its software-layer WebView rendered as a black box over the lower-right screen during menu transitions on Android 16 (screen recording, `tests/device-s23/arm64/blackscreen/`). Not fixable in the ARM32 build without changing Java code. |

Purchases and the three paid-world unlocks were not touched.

## Static validation (`scripts/validate_apk.py`, results in `tests/static-validation-*.txt`)

Both APKs: zip CRCs OK; package name; only the intended ABI directory; every native library has the right ELF class/machine (ARM64: ELF64 AArch64; ARM32: ELF32 ARM); no development paths embedded; `apksigner verify` passes with v1+v2+v3; all original assets plus `classes.dex` and `resources.arsc` byte-identical to `game_data/`. **ALL CHECKS PASSED** for both.

Original files: all 1,411 files in `game_data/` still match `analysis/original-all-files.sha256`.

## Runtime tests actually performed

Device: Samsung Galaxy S23 (SM-S911B), Android 16, ABIs `arm64-v8a, armeabi-v7a, armeabi`. Details in `test-results.md`.

- ARM64: install, start-up (boot of original code 150–170 ms), world map, Hammerfest world screen, shop, level start and play by the tester, music/effects through the original Java audio classes, menu transitions; steady ~29 fps with 95 % of frames ≤ 35–38 ms apart at the game's own 30 fps target; loading stalls reduced to ~0.15–0.5 s; no bridge errors logged; black box fixed (confirmed by tester).
- ARM32 (first build, without wrapper): installs, original libraries load on Android 16, then the early-pause crash above.
- ARM32 with wrapper: installs, starts without crashing, renders the Hammerfest world screen, ran ~80 s, and kept the save made with the ARM64 build. Android 16 shows its 32-bit-app warning dialog.
- PC harness (x86-64 dynarmic): boot and image decoding of the original code.

## Not tested / assumptions

- No other phones, tablets, Android versions or manufacturers were tested. Expected to work on 64-bit ARM devices with Android 6.0+ that provide OpenGL ES 1.1 (all Android devices do); not verified.
- Save/load across app restarts, long play sessions, every level, boss fights, pause/resume via home button, rotation and multi-window, tablets and other aspect ratios: not systematically tested.
- Exact floating-point results of `sinf`/`cosf`/`powf`-style imports come from the device's libm, which may differ in the last bit from 2013 bionic.
- Store, achievements, Flurry analytics and news use retired online services; they fail quietly offline. Purchases cannot be made or restored.

## Known limitations

- ARM64 build needs Android 6.0+; ARM32 build needs Android 5.0+ and a CPU/OS with 32-bit support.
- New signing key: uninstall any other Icebreaker build before installing (this deletes its local save).
- The ARM32 build shows Android's 32-bit-app warning on new Android versions and still has the preloader black box (original Java behaviour).

## Build commands

Prerequisites (installed during this project): Temurin JDK 25, Android SDK `C:\Android\Sdk` (build-tools 36.0.0, platform 36, NDK 28.2.13676358), CMake + Ninja (MinGW), Python 3.12, dynarmic + Boost headers in `native/third_party/`.

```
bash scripts/build_arm64.sh   # -> dist/Icebreaker-ARM64.apk
bash scripts/build_arm32.sh   # -> dist/Icebreaker-ARM32.apk
python -I scripts/validate_apk.py dist/Icebreaker-ARM64.apk arm64-v8a
python -I scripts/validate_apk.py dist/Icebreaker-ARM32.apk armeabi-v7a
```

Signing key: `android/signing/icebreaker-preservation.jks` (password in `keystore.properties` next to it; keep both private and backed up — future updates must be signed with the same key).

## Manual installation

1. Copy the APK to the phone (USB, cloud drive, etc.).
2. Open it from a file manager and allow "Install unknown apps" for that app when Android asks.
3. If another Icebreaker build is installed, uninstall it first (different signing key).
4. Tap OK if Android asks about notifications; the game does not need them.
5. No PC, network or companion app is needed to play.
