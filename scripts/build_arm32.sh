#!/usr/bin/env bash
# Reproducible build of dist/Icebreaker-ARM32.apk (32-bit ARM devices):
#  - original classes.dex, resources.arsc, res/, assets/ unchanged
#  - manifest copy: minSdk 21 (the wrapper needs API 21), targetSdk 24 (Android 14+ installs it)
#  - lib/armeabi-v7a/libicebreaker_original.so = original libgame.so, byte-identical
#  - lib/armeabi-v7a/libcocosdenshion.so       = original, byte-identical
#  - lib/armeabi-v7a/libgame.so                = small wrapper (native/arm32_shim) that forwards every
#    native method and drops renderer/touch calls that arrive before the first nativeInit
#    (fixes the early-pause crash seen on Android 16, tests/device-s23/arm32)
set -euo pipefail
cd "$(dirname "$0")/.."
NDKBIN=/c/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin
BT=/c/Android/Sdk/build-tools/36.0.0
mkdir -p build/arm32 dist
"$NDKBIN/armv7a-linux-androideabi21-clang++.cmd" -shared -fPIC -O2 -std=c++17 -fvisibility=hidden \
  -Wl,-soname,libgame.so -Wl,-z,max-page-size=16384 native/arm32_shim/shim.cpp \
  -o build/arm32/libgame_shim.so -ldl -llog -static-libstdc++
"$NDKBIN/llvm-strip.exe" build/arm32/libgame_shim.so
python -I scripts/patch_manifest.py game_data/AndroidManifest.xml build/arm32/AndroidManifest.xml \
  --min-sdk 21 --target-sdk 24
python -I scripts/pack_apk.py game_data build/arm32/unsigned.apk \
  AndroidManifest.xml=build/arm32/AndroidManifest.xml \
  '!lib/' \
  lib/armeabi-v7a/libgame.so=build/arm32/libgame_shim.so \
  lib/armeabi-v7a/libicebreaker_original.so=game_data/lib/armeabi/libgame.so \
  lib/armeabi-v7a/libcocosdenshion.so=game_data/lib/armeabi/libcocosdenshion.so
"$BT/zipalign.exe" -f -p 16 build/arm32/unsigned.apk build/arm32/aligned.apk
source <(tr -d '\r' < android/signing/keystore.properties | sed 's/^/KS_/')
"$BT/apksigner.bat" sign --ks android/signing/$KS_storeFile --ks-key-alias "$KS_keyAlias" \
  --ks-pass "pass:$KS_storePassword" --key-pass "pass:$KS_keyPassword" \
  --v1-signing-enabled true --v2-signing-enabled true --v3-signing-enabled true \
  --out dist/Icebreaker-ARM32.apk build/arm32/aligned.apk
rm -f dist/Icebreaker-ARM32.apk.idsig
"$BT/apksigner.bat" verify dist/Icebreaker-ARM32.apk && echo "signature OK"
sha256sum dist/Icebreaker-ARM32.apk
