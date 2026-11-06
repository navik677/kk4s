#!/usr/bin/env bash
# Build the libraries rlvm needs that devkitPro does not ship, for the Switch
# (aarch64-none-elf, devkitA64 + libnx), into a local prefix:
#
#   Boost      filesystem, serialization, iostreams (zlib only), program_options,
#              date_time, system (+ headers)
#   libsndfile (no external codecs; rlvm's resampler reads/writes WAV with it)
#
#   tools/build-rlvm-deps.sh            -> out-engines/rlvm-deps/prefix
#   RLVM_DEPS=/some/dir tools/build-rlvm-deps.sh
#
# Nothing is installed into devkitPro's portlibs. Run it from MSYS2 (or Linux)
# bash with DEVKITPRO set (default /opt/devkitpro). Boost's b2 is built for the
# host first, so a host C++ compiler must be on PATH (MSYS2: the ucrt64 or
# mingw64 gcc; Linux: g++). Downloads are kept in $RLVM_DEPS/downloads.
#
# Same approach as rlvm-r/scripts/CMakeLists.txt (the Android port): the Boost
# release archive from archives.boost.io, bootstrap b2 for the host, then a
# user-config.jam whose gcc entry is the cross compiler, static libraries only.
set -euo pipefail

cd "$(dirname "$0")/.."
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export PATH="$DEVKITPRO/portlibs/switch/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH"

DEPS="${RLVM_DEPS:-$PWD/out-engines/rlvm-deps}"
PREFIX="$DEPS/prefix"
DL="$DEPS/downloads"
WORK="$DEPS/work"
JOBS="$(nproc 2>/dev/null || echo 4)"
mkdir -p "$PREFIX" "$DL" "$WORK"

BOOST_VERSION=1.87.0
BOOST_SHA256=af57be25cb4c4f4b413ed692fe378affb4352ea50fbe294a11ef548f4d527d89
SNDFILE_VERSION=1.2.2
SNDFILE_SHA256=3799ca9924d3125038880367bf1468e53a1b7e3686a934f098b7e1d286cdb80e


fetch() { # url sha256 file
    local url="$1" sum="$2" out="$DL/$3"
    if [ ! -f "$out" ]; then
        echo "== download $url"
        curl -fL --retry 3 -o "$out.part" "$url"
        mv "$out.part" "$out"
    fi
    echo "$sum  $out" | sha256sum -c - >/dev/null || { echo "bad checksum: $out" >&2; exit 1; }
}

host_cxx() {
    for c in g++ c++ clang++; do command -v "$c" >/dev/null 2>&1 && { echo "$c"; return; }; done
    for d in /ucrt64/bin /mingw64/bin /clang64/bin; do
        [ -x "$d/g++.exe" ] && { echo "$d/g++.exe"; return; }
    done
    echo ""
}

build_boost() {
    local have=1 l
    for l in filesystem serialization iostreams program_options date_time; do
        [ -f "$PREFIX/lib/libboost_$l.a" ] || have=0
    done
    if [ $have = 1 ] && [ -f "$PREFIX/include/boost/version.hpp" ]; then
        echo "== boost already in $PREFIX"; return
    fi
    local us="${BOOST_VERSION//./_}"
    fetch "https://archives.boost.io/release/$BOOST_VERSION/source/boost_$us.tar.bz2" "$BOOST_SHA256" "boost_$us.tar.bz2"
    local src="$WORK/boost_$us"
    if [ ! -d "$src" ]; then
        echo "== unpack boost"
        tar -xjf "$DL/boost_$us.tar.bz2" -C "$WORK"
    fi

    # b2 for the host. A MinGW b2 needs its compiler's runtime DLLs on PATH.
    local hcxx; hcxx="$(host_cxx)"
    [ -n "$hcxx" ] || { echo "no host C++ compiler for b2 (MSYS2: pacman -S mingw-w64-ucrt-x86_64-gcc)" >&2; exit 1; }
    case "$hcxx" in /*) export PATH="$PATH:$(dirname "$hcxx")" ;; esac
    if [ ! -x "$src/b2" ] && [ ! -x "$src/b2.exe" ]; then
        echo "== build b2 with $hcxx"
        (cd "$src/tools/build/src/engine" && PATH="$(dirname "$hcxx"):$PATH" CXX="$hcxx" ./build.sh gcc --cxx="$hcxx")
        cp "$src/tools/build/src/engine/b2"* "$src/"
    fi
    local b2="$src/b2"; [ -x "$b2" ] || b2="$src/b2.exe"

    # The cross compiler; b2 on Windows wants Windows paths for programs.
    local cxx ar ranlib
    cxx="$(command -v aarch64-none-elf-g++)"
    ar="$(command -v aarch64-none-elf-gcc-ar)"
    ranlib="$(command -v aarch64-none-elf-gcc-ranlib)"
    if command -v cygpath >/dev/null 2>&1; then
        cxx="$(cygpath -m "$cxx")"; ar="$(cygpath -m "$ar")"; ranlib="$(cygpath -m "$ranlib")"
    fi
    local inc_portlibs="$DEVKITPRO/portlibs/switch/include" lib_portlibs="$DEVKITPRO/portlibs/switch/lib"
    local prefix_b2="$PREFIX"
    if command -v cygpath >/dev/null 2>&1; then
        inc_portlibs="$(cygpath -m "$inc_portlibs")"; lib_portlibs="$(cygpath -m "$lib_portlibs")"
        prefix_b2="$(cygpath -m "$PREFIX")"
    fi
    local libnx_inc="$DEVKITPRO/libnx/include"
    command -v cygpath >/dev/null 2>&1 && libnx_inc="$(cygpath -m "$libnx_inc")"

    cat > "$src/user-config.jam" <<EOF
using gcc : switch : "$cxx" :
    <archiver>"$ar"
    <ranlib>"$ranlib"
    <compileflags>"-march=armv8-a+crc+crypto"
    <compileflags>"-mtune=cortex-a57"
    <compileflags>"-mtp=soft"
    <compileflags>"-fPIE"
    <compileflags>"-ffunction-sections"
    <compileflags>"-fdata-sections"
    <compileflags>"-D__SWITCH__"
    <compileflags>"-I$libnx_inc"
    ;
EOF
    rm -f "$src/project-config.jam"
    # No mmap on the Switch; rlvm uses only the zlib filter of Boost.Iostreams.
    sed -i 's/^local sources = file_descriptor.cpp mapped_file.cpp ;/local sources = file_descriptor.cpp ;/' \
        "$src/libs/iostreams/build/Jamfile.v2"
    # newlib declares fdopendir(), so Boost.Filesystem's compile-only check says
    # yes, but libnx does not implement it: iterate directories with opendir().
    sed -i '/has_fdopendir_nofollow/d' "$src/libs/filesystem/build/Jamfile.v2"

    echo "== b2 (boost $BOOST_VERSION) -> $PREFIX"
    (cd "$src" && "$b2" -j"$JOBS" -q \
        --user-config=user-config.jam \
        --build-dir="$WORK/bb" --hash \
        --prefix="$prefix_b2" \
        --layout=system \
        --with-filesystem --with-serialization --with-iostreams \
        --with-program_options --with-date_time --with-system \
        toolset=gcc-switch target-os=linux threadapi=pthread threading=multi \
        architecture=arm address-model=64 binary-format=elf abi=aapcs \
        variant=release link=static runtime-link=static \
        cxxstd=17 cxxstd-dialect=gnu \
        -sNO_BZIP2=1 -sNO_LZMA=1 -sNO_ZSTD=1 \
        -sZLIB_INCLUDE="$inc_portlibs" -sZLIB_LIBPATH="$lib_portlibs" \
        define=BOOST_FILESYSTEM_NO_CXX20_ATOMIC_REF \
        install)
}

build_sndfile() {
    if [ -f "$PREFIX/lib/libsndfile.a" ]; then echo "== libsndfile already in $PREFIX"; return; fi
    fetch "https://github.com/libsndfile/libsndfile/releases/download/$SNDFILE_VERSION/libsndfile-$SNDFILE_VERSION.tar.xz" \
        "$SNDFILE_SHA256" "libsndfile-$SNDFILE_VERSION.tar.xz"
    local src="$WORK/libsndfile-$SNDFILE_VERSION"
    [ -d "$src" ] || tar -xJf "$DL/libsndfile-$SNDFILE_VERSION.tar.xz" -C "$WORK"
    echo "== libsndfile $SNDFILE_VERSION -> $PREFIX"
    rm -rf "$WORK/sndfile-build"
    aarch64-none-elf-cmake -S "$src" -B "$WORK/sndfile-build" -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF \
        -DBUILD_TESTING=OFF -DENABLE_EXTERNAL_LIBS=OFF -DENABLE_MPEG=OFF \
        -DENABLE_CPACK=OFF -DINSTALL_MANPAGES=OFF -DINSTALL_PKGCONFIG_MODULE=ON \
        -DENABLE_SQLITE=OFF -DENABLE_ALSA=OFF
    make -C "$WORK/sndfile-build" -j"$JOBS" sndfile
    make -C "$WORK/sndfile-build" install
}

case "${1:-all}" in
    all) build_boost; build_sndfile ;;
    boost) build_boost ;;
    sndfile) build_sndfile ;;
    *) sed -n '2,19p' "$0"; exit 1 ;;
esac
echo "== rlvm deps in $PREFIX"
