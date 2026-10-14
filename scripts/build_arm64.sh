#!/usr/bin/env bash
# Reproducible build of dist/Icebreaker-ARM64.apk:
#  - original classes.dex, resources.arsc, res/, assets/ unchanged
#  - manifest copy: minSdk 23, targetSdk 28 (no "built for an older Android" warning),
#    <uses-library org.apache.http.legacy> because the original Java code uses Apache HTTP
#  - lib/arm64-v8a/libgame.so = compatibility layer (dynarmic + bridges), built from native/
#  - lib/arm64-v8a/libcocosdenshion.so = empty stand-in so the original loadLibrary call succeeds
#  - original ARM32 libraries carried as data in assets/original-armeabi/ (never loaded by Android)
set -euo pipefail
cd "$(dirname "$0")/.."
NDK=/c/Android/Sdk/ndk/28.2.13676358
BT=/c/Android/Sdk/build-tools/36.0.0
STRIP=$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe
MIN_SDK=${MIN_SDK:-23}
TARGET_SDK=${TARGET_SDK:-28}
mkdir -p build/android-arm64 build/arm64 dist
( cd build/android-arm64 && cmake -G Ninja ../../native -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 -DCMAKE_BUILD_TYPE=Release -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON >/dev/null \
  && ninja game cocosdenshion )
"$STRIP" -o build/arm64/libgame.so build/android-arm64/libgame.so
"$STRIP" -o build/arm64/libcocosdenshion.so build/android-arm64/libcocosdenshion.so
python -I scripts/patch_manifest.py game_data/AndroidManifest.xml build/arm64/AndroidManifest.xml \
  --min-sdk "$MIN_SDK" --target-sdk "$TARGET_SDK" --uses-library org.apache.http.legacy
python -I scripts/pack_apk.py game_data build/arm64/unsigned.apk \
  AndroidManifest.xml=build/arm64/AndroidManifest.xml \
  '!lib/' \
  lib/arm64-v8a/libgame.so=build/arm64/libgame.so \
  lib/arm64-v8a/libcocosdenshion.so=build/arm64/libcocosdenshion.so \
  assets/original-armeabi/libgame.so=game_data/lib/armeabi/libgame.so \
  assets/original-armeabi/libcocosdenshion.so=game_data/lib/armeabi/libcocosdenshion.so
"$BT/zipalign.exe" -f -p 16 build/arm64/unsigned.apk build/arm64/aligned.apk
source <(tr -d '\r' < android/signing/keystore.properties | sed 's/^/KS_/')
"$BT/apksigner.bat" sign --ks android/signing/$KS_storeFile --ks-key-alias "$KS_keyAlias" \
  --ks-pass "pass:$KS_storePassword" --key-pass "pass:$KS_keyPassword" \
  --v1-signing-enabled true --v2-signing-enabled true --v3-signing-enabled true \
  --out dist/Icebreaker-ARM64.apk build/arm64/aligned.apk
rm -f dist/Icebreaker-ARM64.apk.idsig
"$BT/apksigner.bat" verify dist/Icebreaker-ARM64.apk && echo "signature OK"
sha256sum dist/Icebreaker-ARM64.apk
