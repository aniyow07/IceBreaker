# Icebreaker Android preservation port

Tools to make the 2013 Android release of **Icebreaker** (Nitrome, `com.nitrome.icebreaker` 1.0.2) run on modern phones, including 64-bit-only devices, **without changing the game's own code**.

> This repository contains **no game files**. Icebreaker's code, art, sound and levels belong to Nitrome. To build anything you need your own copy of the original Icebreaker APK. Builds are for private preservation use only; do not distribute them.

## How it works

The original game is a cocos2d-x 0.12 title whose gameplay exists only as 32-bit ARMv5 machine code (`libgame.so`). Two builds are produced:

- **ARM64 (main):** a new arm64 `libgame.so` runs the *original, unmodified* 32-bit libraries inside [dynarmic](https://github.com/azahar-emu/dynarmic), an ARM→AArch64 JIT. It loads and relocates the original ELF files and implements their 267 imports (libc, stdio, pthreads, zlib, OpenGL ES 1.1, …). It also provides the ARM Linux kuser helpers the ARMv5 code needs and bridges JNI in both directions. The original Java code, resources and assets are used unchanged. Source: `native/compat/`.
- **ARM32:** the original libraries run natively behind a tiny forwarding wrapper (`native/arm32_shim/`) that stops a start-up crash on Android 13+.

On top of running the original code, the ARM64 build makes a few compatibility changes, each documented with evidence in `reports/FINAL-DELIVERY.md`:
- it ignores lifecycle calls that arrive before the engine starts;
- it raises the target SDK;
- it serves the game's APK file lookup from an index, returning byte-identical data;
- it paces frames evenly at the game's own 30 fps;
- it skips a loading-spinner WebView that renders as a black box on modern Android.

Tested on a Samsung Galaxy S23 running Android 16. No other devices have been tested; see `reports/test-results.md`.

## Building

Requirements (Windows paths are hard-coded in the scripts; adjust for your setup):
- Android SDK at `C:\Android\Sdk` with build-tools 36.0.0 and NDK 28.2.13676358
- a JDK (tested with Temurin 25) for `apksigner`
- CMake, Ninja, Python 3, Git Bash

Steps:
1. Extract **your own** original Icebreaker 1.0.2 APK into `game_data/`. It is a zip file, and `AndroidManifest.xml`, `classes.dex`, `lib/armeabi/…` and `assets/…` should sit directly inside `game_data/`. `reports/apk-inventory.md` lists what to expect.
2. Fetch the third-party sources: `bash scripts/fetch_deps.sh`
3. Create your own signing key: `keytool -genkeypair -keystore android/signing/icebreaker-preservation.jks -storetype PKCS12 -alias icebreaker -keyalg RSA -keysize 3072 -validity 10000`, then write `android/signing/keystore.properties` with `storeFile`, `storePassword`, `keyAlias` and `keyPassword` lines. Both files are git-ignored.
4. Build:
   - `bash scripts/build_arm64.sh` → `dist/Icebreaker-ARM64.apk`
   - `bash scripts/build_arm32.sh` → `dist/Icebreaker-ARM32.apk`
5. Validate: `python -I scripts/validate_apk.py dist/Icebreaker-ARM64.apk arm64-v8a`

The PC test harness (`native/harness/`) runs the same layer on x86-64 Windows with desktop OpenGL and an embedded JVM, for debugging.

## Repository layout

| Path | Contents |
|---|---|
| `native/compat/` | ELF loader, dynarmic CPU wrapper, import implementations, JNI bridge, Java entry points |
| `native/arm32_shim/` | ARM32 forwarding wrapper |
| `native/harness/` | PC proof-of-concept harness (x86-64) |
| `scripts/` | build, packaging, manifest patching, validation and analysis scripts (including Ghidra headless scripts) |
| `reports/` | investigation, compatibility, test and delivery reports |

Third-party code used at build time (fetched, not stored here): dynarmic (0BSD) and its bundled externals (MIT), and Boost headers (BSL-1.0).
