# Compatibility assessment (static only — nothing has been run on a device)

No device or emulator was available, so every runtime statement below is **unverified** and marked as expected behaviour.

## Blockers, by stage

| Stage | Finding | Status |
|---|---|---|
| **APK installation** | `targetSdkVersion 18`. Android 14 refuses to install apps targeting < 23, Android 15 < 24 (unless sideloaded with `adb install --bypass-low-target-sdk-block`). | **Verified blocker on Android 14+** (from manifest + platform rule) |
| **CPU ABI** | Only `lib/armeabi` (32-bit ARMv5TE). Devices whose CPUs or system images are 64-bit only cannot run it at all — e.g. Pixel 7 and newer, Snapdragon 8 Elite phones such as the Galaxy S25 series. The install itself is rejected with `INSTALL_FAILED_NO_MATCHING_ABIS`. | **Verified blocker on 64-bit-only devices** |
| Packaging/signing | v1 JAR signature by a test key (`ICBTEST1`). Modern Android still accepts v1-only APKs for minSdk < 24, but any rebuilt APK needs a new key, so it cannot update an existing install of the original. | Constraint, not a blocker |
| Native linker (32-bit capable devices) | Only public NDK libraries are needed; no text relocations; legacy bionic symbols are still exported on 32-bit; `armeabi` (v5) code runs on `armeabi-v7a` CPUs. | Expected to load — unverified |
| JNI | Name-bound `Java_*` exports match the classes in `classes.dex`. | Expected OK — unverified |
| OpenGL | GLES 1.1 fixed function. Still provided on current Android (sometimes through ANGLE). | Expected OK — unverified |
| Audio | Java MediaPlayer/SoundPool with `.m4a`/`.ogg`. | Expected OK — unverified |
| Lifecycle | cocos2d-x 0.12 GL-context loss on pause recreates textures; known-fragile on modern GPUs. | Unknown |
| Save data | Hard-coded `/data/data/com.nitrome.icebreaker/save.xml`. `/data/data` still resolves to user 0's data dir; secondary users / work profiles would break saving. | Works for the primary user only (expected) |
| Network / stores | Flurry, Amazon GameCircle/IAP, Google Play Games and Play Billing AIDL v3 are long-retired services. Expected to fail quietly offline; purchases (3 world unlocks, power-up packs) will likely not complete. | Unknown; must not be bypassed |

## What this means

- On a **32-bit-capable phone running Android ≤ 13**, the original APK probably installs and runs as-is (untested).
- On **Android 14/15+ with 32-bit support**, it needs only a higher targetSdk and a re-sign.
- On **64-bit-only devices** (the stated primary target), the shipped machine code cannot run. A real `arm64-v8a` library has to be produced, and the game layer has no source code. This is the core blocker.

## Update after device testing (Galaxy S23, Android 16)

- **Confirmed:** the original APK's 32-bit code loads on a 32-bit-capable Android 16 device, then crashes in `nativeOnPause` when the system pauses the activity before the GL surface exists (`tests/device-s23/arm32/`). This was the first real runtime blocker. The "built for an older version of Android" dialog is only a warning, not a refusal.
- **Confirmed:** the ARM64 compatibility build runs the original code on the arm64 CPU and is playable. One more modern-Android issue was found and fixed: the original preloader WebView draws a black box (Java side).

### Compatibility matrix

| Configuration | ABI | Status |
|---|---|---|
| Galaxy S23, Android 16, ARM64 build | arm64-v8a | **Tested**: installs, boots, menus, levels played by the tester, ~29 fps steady |
| Galaxy S23, Android 16, ARM32 build (with wrapper) | armeabi-v7a | **Tested**: installs and starts, world screen, ~80 s without crash; 32-bit warning dialog |
| Other 64-bit ARM phones/tablets, Android 6.0–16 | arm64-v8a | Expected to work (only public NDK libraries and GLES 1.1 are used); **untested** |
| 64-bit-only devices (Pixel 7+, Galaxy S25, etc.) | arm64-v8a | Expected to work (the ARM64 build contains no 32-bit native code); **untested** |
| 32-bit-only ARM devices, Android 5.0+ | armeabi-v7a | Expected to work with the ARM32 build; **untested** |
| x86/x86_64 devices and emulators | — | Not supported (no x86 build) |
| Screen sizes / aspect ratios | — | Only the S23's 2340×1080 was seen; the engine letterboxes to its design aspect. Other ratios **untested** |
| Manufacturer-specific behaviour | — | Only Samsung One UI seen; no manufacturer APIs are used |
