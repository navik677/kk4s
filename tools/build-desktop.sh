#!/usr/bin/env bash
# Build the kk4s PC viewer (Linux, or Windows via MSYS2 UCRT64).
#   tools/build-desktop.sh          release-ish build -> out-desktop/kk4s[.exe]
#   ASAN=1 tools/build-desktop.sh   AddressSanitizer + UBSan (Linux)
#   DEPS=1 tools/build-desktop.sh   install dependencies first (apt / pacman)
set -euo pipefail
cd "$(dirname "$0")/.."

install_deps() {
    if command -v apt-get >/dev/null; then
        sudo apt-get update
        sudo apt-get install -y --no-install-recommends build-essential cmake pkg-config \
            libsdl2-dev libfreetype-dev libavcodec-dev libavformat-dev libavfilter-dev \
            libavutil-dev libswscale-dev libpng-dev libturbojpeg0-dev libjpeg-turbo8-dev libwebp-dev \
            libogg-dev libvorbis-dev libopus-dev libopusfile-dev zlib1g-dev libonig-dev
    elif command -v pacman >/dev/null; then
        local p=mingw-w64-ucrt-x86_64
        pacman -S --noconfirm --needed $p-gcc $p-cmake $p-ninja $p-pkgconf $p-SDL2 \
            $p-freetype $p-ffmpeg $p-libpng $p-libjpeg-turbo $p-libwebp $p-libogg \
            $p-libvorbis $p-opus $p-opusfile $p-zlib $p-oniguruma
    fi
}

[ -n "${DEPS:-}" ] && install_deps

BUILD_DIR="${BUILD_DIR:-build-desktop}"
ARGS=(-DKK4S_PLATFORM=desktop -DCMAKE_BUILD_TYPE=RelWithDebInfo)
[ -n "${ASAN:-}" ] && ARGS+=(-DKK4S_ASAN=ON)
command -v ninja >/dev/null && ARGS+=(-G Ninja)

cmake -S . -B "$BUILD_DIR" "${ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc)"

mkdir -p out-desktop
cp "$BUILD_DIR"/kk4s out-desktop/ 2>/dev/null || cp "$BUILD_DIR"/kk4s.exe out-desktop/
ls -la out-desktop
