#!/usr/bin/env bash
# kk4s: Windows build of the standalone rlvm program (docs/engines.md).
#
#   tools/build-rlvm-win.sh          # -> engines/rlvm/build-win/rlvm.exe
#
# Needs MSYS2 UCRT64 with boost, glew, libsndfile, SDL2{,_ttf,_image,_mixer}.
# Unlike the Switch build this is not linked into kk4s: the launcher starts it
# from <kk4s root>/engines/rlvm/rlvm.exe with the game folder as the argument.
set -euo pipefail

if ! command -v g++ >/dev/null 2>&1; then
    export PATH=/c/msys64/ucrt64/bin:$PATH
fi

cd "$(dirname "$0")/.."
RLVM=$PWD/engines/rlvm
OUT=$RLVM/build-win
CXX=${CXX:-g++}
JOBS=${JOBS:-$(nproc)}
# A header change is not visible in the object timestamps, so ask for a clean
# build with: tools/build-rlvm-win.sh clean
if [ "${1:-}" = "clean" ]; then
    rm -rf "$OUT/obj"
fi

export RLVM OUT CXX

# KK4S_GLES: the rlvm code paths the Switch build uses (OpenGL ES 3.2, the
# core-profile renderer). On Windows the ES context and its libraries come from
# ANGLE (libEGL/libGLESv2, mingw-w64-ucrt-x86_64-angleproject), which SDL2 picks
# up through SDL_GL_CreateContext when the ES profile is requested.
CXXFLAGS="-std=gnu++17 -O2 -g -w -fexceptions -frtti -funsigned-char \
-DHAVE_CONFIG_H -DNO_SDL_GLEXT -D_THREAD_SAFE -DNDEBUG -DBOOST_DISABLE_ASSERTS \
-DBOOST_HAS_GETTIMEOFDAY -DKK4S_GLES  \
-include $RLVM/src/platforms/win32/win_compat.h \
-I$RLVM -I$RLVM/src -I$RLVM/vendor -I$RLVM/vendor/guichan/include \
-I$RLVM/vendor/zita-resampler/include -I$RLVM/vendor/xclannad \
-I$RLVM/src/platforms/win32/shims -I/c/msys64/ucrt64/include/SDL2"
export CXXFLAGS

# The same sources as the Switch Makefile, with the Windows front end instead of
# rlvm_switch.cc.
mapfile -t SRCS < <(cd "$RLVM" && {
    find src -name '*.cc' -not -path 'src/platforms/*'
    find src/platforms/gcn -name '*.cc'
    echo src/platforms/win32/rlvm_win32.cc
    echo src/platforms/win32/win_compat.cc
    find vendor/xclannad \( -name '*.cc' -o -name '*.cpp' \)
    echo vendor/pygame/alphablit.cc
    find vendor/zita-resampler/src -name '*.cc'
    find vendor/guichan/src -name '*.cpp'
})

# Incremental builds are only valid while the flags stay the same: the objects
# of an older flag set (say, before -include win_compat.h) would otherwise be
# reused and fail at link time with half-renamed symbols.
mkdir -p "$OUT/obj"
if [ "$(cat "$OUT/obj/.cxxflags" 2>/dev/null)" != "$CXXFLAGS" ]; then
    rm -rf "$OUT/obj"
    mkdir -p "$OUT/obj"
    printf '%s' "$CXXFLAGS" > "$OUT/obj/.cxxflags"
fi

compile() {
    local src=$1
    local obj="$OUT/obj/${src%.*}.o"
    mkdir -p "$(dirname "$obj")"
    if [ -f "$obj" ] && [ "$obj" -nt "$RLVM/$src" ]; then return 0; fi
    "$CXX" $CXXFLAGS -c "$RLVM/$src" -o "$obj"
}
export -f compile 2>/dev/null || true

echo "compiling ${#SRCS[@]} files to $OUT/obj"
printf '%s\n' "${SRCS[@]}" | xargs -d '\n' -P "$JOBS" -n 1 bash -c 'compile "$1"' _

OBJS=()
for s in "${SRCS[@]}"; do OBJS+=("$OUT/obj/${s%.*}.o"); done

echo "linking $OUT/rlvm.exe"
"$CXX" -o "$OUT/rlvm.exe" "${OBJS[@]}" \
    -lboost_program_options-mt -lboost_serialization-mt -lboost_iostreams-mt \
    -lboost_filesystem-mt -lboost_date_time-mt \
    -lsndfile -lSDL2_ttf -lSDL2_image -lSDL2_mixer -lSDL2 \
    -lvorbisfile -lvorbis -logg -lGLESv2 -lEGL -lz

echo "built $OUT/rlvm.exe"
