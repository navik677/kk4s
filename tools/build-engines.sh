#!/usr/bin/env bash
# Build the Switch programs of the other engines kk4s starts (docs/engines.md):
#   out-engines/switch/kk4s/engines/onscripter/onscripter.nro   NScripter games
#   out-engines/switch/kk4s/engines/renpy/renpy.nro             Ren'Py 7.x games
#   out-engines/switch/kk4s/engines/rlvm/rlvm.nro               RealLive games
# Copy out-engines/switch/ to the root of the SD card.
#
#   tools/build-engines.sh onscripter
#   tools/build-engines.sh renpy <renpy-switch.elf> <Quickstart romfs/Contents folder>
#   tools/build-engines.sh rlvm
#
# onscripter: needs devkitPro with switch-sdl2_image switch-sdl2_mixer
#   switch-sdl2_ttf switch-smpeg2 switch-libmodplug switch-mpg123 switch-libvorbisidec.
# renpy: renpy-switch.elf is the Ren'Py Switch SDK binary (uyjulian; it is not in
#   this repository). The Contents folder holds lib.zip and renpy/common (from
#   RenpyNXPorts Quickstart); kk4s' engines/renpy/renpy.py replaces its renpy.py.
# rlvm: needs devkitPro with switch-sdl2_image switch-sdl2_mixer switch-sdl2_ttf
#   switch-mesa switch-libvorbis; Boost and libsndfile are built first by tools/build-rlvm-deps.sh
#   into out-engines/rlvm-deps/prefix (downloads them; a host g++ is needed once).
set -euo pipefail

cd "$(dirname "$0")/.."
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export PATH="$DEVKITPRO/portlibs/switch/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH"
OUT="${OUT:-out-engines}/switch/kk4s/engines"

build_onscripter() {
    make -C engines/onscripter-nx -j"$(nproc 2>/dev/null || echo 4)"
    mkdir -p "$OUT/onscripter"
    cp engines/onscripter-nx/onscripter-nx.nro "$OUT/onscripter/onscripter.nro"
    echo "== $OUT/onscripter/onscripter.nro"
}

build_renpy() {
    local elf="${1:?renpy-switch.elf}" contents="${2:?Quickstart romfs/Contents folder}"
    [ -f "$contents/lib.zip" ] || { echo "no lib.zip in $contents" >&2; exit 1; }
    [ -d "$contents/renpy/common" ] || { echo "no renpy/common in $contents" >&2; exit 1; }
    local stage
    stage="$(mktemp -d)"
    mkdir -p "$stage/romfs/Contents/renpy"
    cp "$contents/lib.zip" "$stage/romfs/Contents/"
    cp -r "$contents/renpy/common" "$stage/romfs/Contents/renpy/"
    cp engines/renpy/renpy.py "$stage/romfs/Contents/renpy.py"
    nacptool --create "Ren'Py (kk4s)" "Tom Rothamel, uyjulian; kk4s" "7.3.5" "$stage/renpy.nacp"
    mkdir -p "$OUT/renpy"
    elf2nro "$elf" "$OUT/renpy/renpy.nro" --nacp="$stage/renpy.nacp" --romfsdir="$stage/romfs"
    rm -rf "$stage"
    echo "== $OUT/renpy/renpy.nro"
}

build_rlvm() {
    tools/build-rlvm-deps.sh
    make -C engines/rlvm -j"$(nproc 2>/dev/null || echo 4)" \
        RLVM_PREFIX="${RLVM_DEPS:-$PWD/out-engines/rlvm-deps}/prefix"
    mkdir -p "$OUT/rlvm"
    cp engines/rlvm/rlvm.nro "$OUT/rlvm/rlvm.nro"
    echo "== $OUT/rlvm/rlvm.nro"
}

case "${1:-}" in
    onscripter) build_onscripter ;;
    renpy) shift; build_renpy "$@" ;;
    rlvm) build_rlvm ;;
    *) sed -n '2,20p' "$0"; exit 1 ;;
esac
