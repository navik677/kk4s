#!/usr/bin/env bash
# Build kk4s.nro inside the devkitpro/devkita64 container (CI or Docker).
#   tools/build-switch.sh            install deps + build
#   SKIP_DEPS=1 tools/build-switch.sh  build only
set -euo pipefail

cd "$(dirname "$0")/.."
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
PORTLIBS="$DEVKITPRO/portlibs/switch"
BUILD_DIR="${BUILD_DIR:-build-switch}"

PKGS=(
    switch-pkg-config switch-cmake
    switch-sdl2 switch-freetype switch-ffmpeg
    switch-libpng switch-libjpeg-turbo switch-libwebp
    switch-libogg switch-libvorbis switch-libopus switch-opusfile
    switch-zlib switch-bzip2
)

install_deps() {
    dkp-pacman -Sy --noconfirm || true
    for p in "${PKGS[@]}"; do
        dkp-pacman -S --noconfirm --needed "$p" || echo "WARNING: package $p not available"
    done
    if ! dkp-pacman -S --noconfirm --needed switch-oniguruma 2>/dev/null; then
        build_oniguruma
    fi
}

# oniguruma (TJS RegExp) is not always in switch-portlibs: build it from source.
build_oniguruma() {
    if [ -f "$PORTLIBS/lib/libonig.a" ]; then return; fi
    echo "== building oniguruma from source"
    local ver=6.9.9 tmp
    tmp="$(mktemp -d)"
    curl -fsSL "https://github.com/kkos/oniguruma/releases/download/v${ver}/onig-${ver}.tar.gz" -o "$tmp/onig.tgz"
    tar -xzf "$tmp/onig.tgz" -C "$tmp"
    "$PORTLIBS/bin/aarch64-none-elf-cmake" -S "$tmp/onig-${ver}" -B "$tmp/build" \
        -DCMAKE_INSTALL_PREFIX="$PORTLIBS" -DBUILD_SHARED_LIBS=OFF \
        -DENABLE_BINARY_COMPATIBLE_POSIX_API=OFF -DBUILD_TEST=OFF
    cmake --build "$tmp/build" -j"$(nproc)"
    cmake --install "$tmp/build"
    if [ ! -f "$PORTLIBS/lib/pkgconfig/oniguruma.pc" ]; then
        mkdir -p "$PORTLIBS/lib/pkgconfig"
        cat > "$PORTLIBS/lib/pkgconfig/oniguruma.pc" <<EOF
prefix=$PORTLIBS
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: oniguruma
Description: Regular expression library
Version: ${ver}
Libs: -L\${libdir} -lonig
Cflags: -I\${includedir}
EOF
    fi
    rm -rf "$tmp"
}

if [ -z "${SKIP_DEPS:-}" ]; then
    install_deps
fi
if [ -n "${DEPS_ONLY:-}" ]; then
    exit 0
fi

"$PORTLIBS/bin/aarch64-none-elf-cmake" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$BUILD_DIR" -j"$(nproc)"

mkdir -p out
cp "$BUILD_DIR/kk4s.nro" "$BUILD_DIR/kk4s.elf" out/
ls -la out
