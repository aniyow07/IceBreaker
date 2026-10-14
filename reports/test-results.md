# Test results

Only tests that were actually run are listed. Everything else is untested.

## Test device

Samsung Galaxy S23 (SM-S911B, Snapdragon 8 Gen 2 / SM8550), Android 16 (SDK 36), One UI build `S911BXXSAFZI1`. ABIs reported by `ro.product.cpu.abilist`: `arm64-v8a, armeabi-v7a, armeabi`. Connected over USB/adb. Icebreaker was not previously installed for user 0.

## dist/Icebreaker-ARM32.apk (original native code, targetSdk 24) — 2026-10-14

| Check | Result | Evidence |
|---|---|---|
| Install | ✅ `Success`, `primaryCpuAbi=armeabi` | adb output |
| Native libraries load | ✅ original `libgame.so`/`libcocosdenshion.so` loaded by `app_process32` | `tests/device-s23/arm32/logcat-launch.txt` |
| Java start-up | ✅ IAB helper, GameHelper, Flurry, SoundPool initialise | same log |
| Old-target warning | ⚠️ Android shows *"This app was built for an older version of Android…"* (`DeprecatedTargetSdkVersionDialog`) | `launch2-15s.png`, logcat `AppWarnings` |
| Start-up | ❌ **Crash, twice in a row**: `SIGSEGV` null-pointer read in `Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause` (libgame.so pc `0x210068`) on the GL thread, called from `Cocos2dxRenderer.handleOnPause` | tombstone in `logcat-launch.txt`, `logcat-launch2.txt` |
| Cause (inferred from the log order) | The activity is paused by the system warning dialog and the notification-permission prompt (`REQUEST_PERMISSIONS_FOR_OTHER`) **before** the GL surface was created, so `nativeOnPause` runs before `nativeInit` created the engine objects it dereferences. | |

## dist/Icebreaker-ARM32.apk with the early-pause wrapper — 2026-10-14

| Check | Result | Evidence |
|---|---|---|
| Install over the ARM64 build | ✅ `primaryCpuAbi=armeabi-v7a` | adb output |
| Start-up | ✅ no crash; process alive after ~80 s; Hammerfest world screen rendered | `tests/device-s23/arm32-fixed/launch-18s.png`, `logcat*.txt` |
| Save data | ✅ world progress (60 %) carried over from the ARM64 build | screenshot |
| System dialog | ⚠️ Android 16 shows `DeprecatedAbiDialog` (32-bit app warning) | logcat `AppWarnings` |

## dist/Icebreaker-ARM64.apk (compatibility layer, first build) — 2026-10-14

| Check | Result | Evidence |
|---|---|---|
| Install | ✅ `Success`, `primaryCpuAbi=arm64-v8a` | adb output |
| Boot of original ARM32 code under dynarmic (AArch64 backend) | ✅ both libraries loaded/linked, 3 + 310 constructors, guest `JNI_OnLoad` returned `0x10004`, boot 154 ms | `tests/device-s23/arm64/run1-logcat.txt` |
| Early pause | ✅ no crash (bridge ignores renderer calls before the first `nativeInit`) | same |
| Rendering | ✅ world map ("The First Cut") rendered correctly in landscape at 20 s | `tests/device-s23/arm64/run1-20s.png` |
| Bridge errors | ✅ none logged (no unimplemented import, no failed JNI lookup) | grep of `IcebreakerCompat` lines |
| Level start | ✅ guest logged textures loading and Flurry recorded `LEVEL_STARTED_FIRST_TIME` while the tester played | `run1-logcat-b.txt` |
| Music | ⚠️ `MediaPlayer` active in the process log; audible output not yet confirmed by the tester | |
| CPU | 7.6 % of one `top` sample on the map screen | adb `top` |
| Touch and gameplay | ✅ tester navigated title → world → map → levels and played ("it's working"); screen recording shows menu navigation | thread messages, `blackscreen/rec.mp4` |
| Pause/resume, save/load across restarts | ⚠️ not systematically tested; progress survived several `adb install -r` updates | |

## dist/Icebreaker-ARM64.apk — performance work (same device)

| Build | Frame-to-frame time | Worst load stall | Notes |
|---|---|---|---|
| First build | p50 ~49–55 ms (alternating short/long frames) | 1.5–2.8 s | 8.2 M emulated `fread` calls per screen load; `nativeRender` itself ~1.9 ms per frame |
| + memory-mapped files | — | 0.87–1.8 s | host time in `fread` 1430 → 494 ms per load window |
| + indexed `getFileDataFromZip` | — | 0.16–0.5 s | identical bytes for all 1,413 APK entries (`tests/zipindex/`) |
| + frame pacing at the game's 1/30 s | p50 34.0–34.8 ms, p95 34.7–38 ms | ~0.13–0.27 s | ~29 fps steady, the game's own target |

Logs: `tests/device-s23/arm64/*.txt`. The `PERF` lines come from the build itself (per-import profiling is enabled with `adb shell setprop debug.icebreaker.profile 1`).

## Black screens reported by the tester

The screen recording (`tests/device-s23/arm64/blackscreen/rec.mp4`, 180 s) shows an opaque black rectangle over the lower-right of the screen at 32.6 s, during the fade from the levels page to the main menu. Cause: the original `Cocos2dxActivity.showPreloadCicle` (decompiled through the REA CLI with JADX) adds a software-layer WebView, sized to the full screen and placed at the screen centre, to show `prelo.gif`. Fix: the bridge skips that call. The tester confirmed the black box was gone on the next build, and the log shows the skip.

## PC harness (x86-64 dynarmic backend) — PoC 1

Original libraries boot, run constructors and `JNI_OnLoad`, `nativeInit` completes (185 ms) and PNG decoding works. One crash found there was a bug in my zlib bridge (`inflateEnd` reused a clobbered register), fixed. Further PC work was stopped by the user's choice to test on the phone.
