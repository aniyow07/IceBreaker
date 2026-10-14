#!/usr/bin/env bash
# Fetches the third-party sources the compatibility layer is built with (not stored in this repo).
#  - dynarmic (0BSD), azahar-emu fork, the exact commit used for the tested builds, plus the bundled
#    externals it needs (fmt, mcl, oaknut, robin-map, xbyak, zycore, zydis)
#  - Boost 1.86.0 headers (BSL-1.0), checked against its published SHA-256
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p native/third_party/dl
cd native/third_party
if [ ! -d dynarmic ]; then
  git clone https://github.com/azahar-emu/dynarmic.git
  git -C dynarmic checkout a46601580d5512d324104f985b5f0209dc980ddc
  git -C dynarmic submodule update --init --depth 1 externals/fmt externals/mcl externals/oaknut \
    externals/robin-map externals/xbyak externals/zycore externals/zydis
fi
if [ ! -d boost ]; then
  curl -sSL -o dl/boost.tar.gz https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.gz
  echo "2575e74ffc3ef1cd0babac2c1ee8bdb5782a0ee672b1912da40e5b4b591ca01f  dl/boost.tar.gz" | sha256sum -c -
  tar -xzf dl/boost.tar.gz boost_1_86_0/boost boost_1_86_0/LICENSE_1_0.txt
  mv boost_1_86_0 boost
fi
echo "third-party sources ready in native/third_party/"
