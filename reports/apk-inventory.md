# APK inventory — Icebreaker (Android)

Source: `C:\REA\game_data\` (extracted APK, unchanged). All facts below come from the files themselves; decoded copies live in `analysis/`.

## Identity

| Field | Value | Evidence |
|---|---|---|
| Package | `com.nitrome.icebreaker` | `analysis/AndroidManifest.decoded.xml` |
| Version | `1.0.2` (versionCode 3) | manifest |
| minSdk / targetSdk | **9** (Android 2.3) / **18** (Android 4.3) | manifest |
| Signing | JAR (v1) signature only, signer alias `ICBTEST1` (`META-INF/ICBTEST1.RSA/.SF`), built with Oracle JDK 1.7.0_51. No v2/v3 data is recoverable from an extracted tree. | `META-INF/` |
| Integrity | 1,408/1,408 signed entries match their SHA-1 digests | `scripts/verify_manifest_digests.py` |
| Hashes | see `reports/original-hashes.sha256` and `analysis/original-all-files.sha256` (all 1,411 files) | |

## Manifest

- **Launcher activity:** `com.nitrome.icebreaker.Icebreaker`, `screenOrientation="0"` (landscape), fullscreen system theme `0x01030007`.
- **Permissions:** `INTERNET`, `ACCESS_NETWORK_STATE`, `com.android.vending.BILLING`, `org.onepf.openiab.permission.BILLING`. No storage permission (saves go to the app's private dir).
- **GL:** declares `glEsVersion 0x20000` (GLES 2.0) but the native code links **`libGLESv1_CM.so`** and calls only fixed-function GLES 1.x entry points (`glMatrixMode`, `glVertexPointer`, `glTexEnvi`…).
- **Third-party components:** Amazon GameCircle overlay activities, Amazon login (`AuthorizationActivity`), Amazon in-app purchasing receiver, Google Play Games app-id meta-data.
- No `android:exported` attributes (fine for targetSdk 18, mandatory for targetSdk ≥ 31).

## Java side (`classes.dex`, dex 035, 3,582 classes)

| Package | Classes | Role |
|---|---|---|
| `com.google.android` (GMS, Play Games) | 1,843 | sign-in / achievements |
| `android.support.v4` | 502 | support lib |
| `com.amazon.*` (ags, identity, insights, inapp) | 749 | Amazon GameCircle, IAP |
| `com.google.gson`, `com.google.ads` | 189 | |
| `com.flurry` | 78 | analytics |
| `org.onepf.oms`, `com.skplanet`, `com.tmoney`, `com.sec.android` | ~113 | OpenIAB multi-store billing |
| **`org.cocos2dx.lib`** | 45 | engine glue: `Cocos2dxActivity`, `Cocos2dxGLSurfaceView`, `Cocos2dxRenderer`, `Cocos2dxMusic`, `Cocos2dxSound`, `Cocos2dxBitmap`, `Cocos2dxAccelerometer`, `Prelo` |
| `com.nitrome.icebreaker`, `com.nitrome.util`, `com.nitrome.store` | 50 | `Icebreaker` activity, `InApps`, Google/Amazon store wrappers, `IabHelper` (Play Billing AIDL v3), `FlurryHelper`, `ExtrasHelper` |

A stray source file `com/nitrome/icebreaker/Icebreaker.java-e` ships in the APK: it is the cocos2d-x 2010-2012 template (`package com.nitrome.icebreakertesting`) loading `cocosdenshion` then `game` via `System.loadLibrary`. It is a leftover, not the compiled activity.

Full class list: `analysis/dex.classes.txt`; strings: `analysis/dex.strings.txt`. Dex was parsed with my own read-only script; no decompilation (JADX is not installed).

## Native libraries

Only one ABI directory exists: **`lib/armeabi/`**. There is no `armeabi-v7a`, `arm64-v8a`, `x86` or `x86_64`.

| Library | Size | ELF | Arch / ISA | NEEDED | Exports |
|---|---|---|---|---|---|
| `libgame.so` | 3,568,748 | ELF32 LE, DYN, EABI5 | ARMv5TE, Thumb-1, VFPv2 (softfp) | `libcocosdenshion.so liblog.so libz.so libGLESv1_CM.so libstdc++.so libm.so libc.so libdl.so` | 8,881 named functions, 267 imports |
| `libcocosdenshion.so` | 87,280 | ELF32 LE, DYN, EABI5 | ARMv5TE | `liblog libstdc++ libm libc libdl` | 199 functions (`CocosDenshion::SimpleAudioEngine` + JNI glue to `Cocos2dxActivity`) |

Build: GCC 4.6 (prerelease 20120106) and GCC 4.4.0 objects, linked with gold, `BIND_NOW`, `SYMBOLIC`. **Not stripped of dynamic symbols** — all C++ class/method names are visible. No `TEXTREL`. Not packed or obfuscated (normal `.text`/`.rodata`, readable strings). Details in `reports/native-analysis.md`.

## Assets (`assets/`, flat directory, 1,275 files)

| Type | Count | Purpose (from names/contents) |
|---|---|---|
| `.xml` | 589 | 219 `p*.xml` level layouts (`<level theme=…><bounds…><exterior type="boat"…>`), 137 `level_*.xml` localized level names (en/fr/it/de/es…), achievements, IAP tutorial animations, fonts |
| `.png` + `.plist` | 274 + 91 | sprite sheets with cocos2d plist frame maps (`active-*.png/.plist`, iPad/iPhone variants), iOS icons/splashes left in |
| `.ogg` | 275 | sound effects (`Cocos2dxSound` / SoundPool) |
| `.m4a` | 12 | music tracks (`Cocos2dxMusic` / MediaPlayer) |
| `.wav` | 7 | a few effects |
| `.fnt` | 9 | BMFont bitmap fonts |
| `*_old`, `*_tmp` | 10 | leftover build files — preserved |
| other | `api_key.txt` (analytics key — not reproduced here), `app.config.txt` (cocos trace/assert channels), `app.icf`, `convert-multiple-fonts.rb`, `iTunesArtwork`, `prelo.html`, `Info.plist` | |

`res/` holds the launcher icon, Google sign-in button drawables, three layouts (`game_demo.xml` with the GL view, two Amazon modal overlays) and `raw/amazon_gc_prototype.zip`.

## Save data

`libgame.so` contains the literal path **`/data/data/com.nitrome.icebreaker/save.xml`** (TinyXML-based `SavedData` class, 120 methods). The save location is hard-coded to this package name, so any port must keep the package name `com.nitrome.icebreaker` or saving breaks.

## In-app purchases (not to be bypassed)

Product IDs found in `classes.dex`: three **world unlocks** (`unlock_kraken`, `unlock_trollmarsh`, `unlock_underdwell`) and consumable "god powers" packs (`10/30/80 fingers_of_god`, `gravity_of_gods`, `magnets_of_gods`, `sack/crate/barrel`). Store code uses the legacy Play Billing AIDL v3 (`IabHelper`) and Amazon IAP v1. Whether those worlds can also be unlocked by normal play is not yet established.
