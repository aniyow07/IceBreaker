# Evidence index

| ID | Claim | Source / command | Confidence |
|---|---|---|---|
| E1 | `libgame.so` SHA-256 `f7946f81…72a1`, ELF ARM | REA `open_binary`; `sha256sum` | Verified |
| E2 | REA cannot open ARM ELF natively on this host (Ghidra unset; Windows Ghidra profile accepts PE/6502 only) | REA `binary_overview` error `provider_unavailable` / `target_format_unsupported` | Verified |
| E3 | Extracted tree is complete and unmodified vs. signed manifest | `scripts/verify_manifest_digests.py` → 1408 ok, 0 mismatch | Verified |
| E4 | Package `com.nitrome.icebreaker` 1.0.2, minSdk 9, targetSdk 18 | `scripts/axml_decode.py` → `analysis/AndroidManifest.decoded.xml` | Verified |
| E5 | Only ABI is `armeabi`; ISA ARMv5TE/Thumb-1/VFPv2 | `readelf -h -A` | Verified |
| E6 | Engine `cocos2d-1.0.1-x-0.12.0`, GLES 1.1 imports | `strings`, `readelf --dyn-syms` | Verified |
| E7 | 8,881 named functions, 267 imports, no TEXTREL, BIND_NOW | `readelf -d`, `analysis/libgame.classes.txt` | Verified |
| E8 | JNI entry points and addresses (`JNI_OnLoad` 0xd74fd, `nativeInit` 0xd750d, `nativeRender` 0x210055, `nativeSetPaths` 0x210151) | `analysis/libgame.dynsym.txt` | Verified (symbol table); behaviour unverified |
| E9 | Hard-coded save path `/data/data/com.nitrome.icebreaker/save.xml` | `strings` | Verified string; usage unverified |
| E10 | IAP product IDs incl. three world unlocks | `analysis/dex.strings.txt` | Verified strings |
| E11 | `libcocosdenshion.so` = SimpleAudioEngine JNI wrapper over Java audio | dynsym + strings | High (symbol-based) |
| E12 | REA on Windows refuses ARM ELF even with Ghidra configured (`target_format_unsupported`) | `rea inspect` with `GHIDRA_INSTALL_DIR` set | Verified |
| E13 | Startup path and constants (see native-analysis stage 2) | Ghidra decompiles in `analysis/decomp/` | Verified |
| E14 | The original pacing sleeps 2×(interval−elapsed) | REA CLI + JADX, Evidence `ev_a3c9c47b06b4a9b8e06be6bd72c45b11953042b8f98a7094c08d9e67894aafc4`, `analysis/rea-onDrawFrame.json` | Verified |
| E15 | The original ARM32 code crashes in `nativeOnPause` on an early pause | tombstone in `tests/device-s23/arm32/logcat-launch.txt` | Verified |
| E16 | The ARM64 build runs and renders on the Galaxy S23 | `tests/device-s23/arm64/` screenshots and logs | Verified |
| E17 | The zip substitution returns identical bytes for all 1,413 entries | `tests/zipindex/expected.tsv` vs `actual.tsv` | Verified |
| E18 | The black box is the preloader WebView | `tests/device-s23/arm64/blackscreen/rec.mp4` at 32.6 s; REA/JADX decompile of `showPreloadCicle` | Verified |
| E19 | Both APKs pass static validation | `tests/static-validation-*.txt` | Verified |
