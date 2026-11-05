#!/usr/bin/env bash
# Run the synthetic regression suite against a built kk4s binary.
#
#   tools/run-tests.sh                       # out-desktop/kk4s, every group
#   tools/run-tests.sh --group xp3           # one group
#   tools/run-tests.sh build-desktop/kk4s    # explicit binary
#
# Groups:
#   xp3        storage layer: project dir, overlay precedence, archive parsing,
#              game-folder layouts (data/, data.exe, XP3 inside an exe),
#              keyless .tpm encryption discovery, the game's own .tpm filter
#              in the x86 interpreter (needs i686-w64-mingw32-gcc, else skipped)
#   io         engine I/O: TextStream encoding, AlphaMovie, Scripts.loadDataPack,
#              layerEx* / layerExSave / layerExDraw Layer methods, fstat.dll Storages methods,
#              json.dll / scriptsEx.dll / windowEx.dll, mouse / wheel / touch /
#              pad input semantics, and the original KAG3 system scripts
#              playing a scenario and played with the pad (tests/kag3, ~60 s)
#   decoders   PBD / PSB / CXDEC decoders vs their Python references (no engine),
#              the x86 interpreter vs native code and the .tpm loader on
#              synthetic filter plugins (need i686-w64-mingw32-gcc)
#
# Engine groups boot a synthetic game headlessly (SDL_VIDEODRIVER=dummy) and
# write their logs to $KK4S_TEST_WORK (default build-tests/). No game content is
# used. These tests say nothing about Switch hardware behaviour.
set -uo pipefail
cd "$(dirname "$0")/.."

GROUP=all
EXE=out-desktop/kk4s
while [ $# -gt 0 ]; do
    case "$1" in
        --group) GROUP=${2:?--group needs a value}; shift 2 ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) EXE=$1; shift ;;
    esac
done

if [ ! -x "$EXE" ]; then
    echo "no kk4s binary at $EXE" >&2
    exit 2
fi
EXE=$(cd "$(dirname "$EXE")" && pwd)/$(basename "$EXE")

WORK=${KK4S_TEST_WORK:-build-tests}
mkdir -p "$WORK"

export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
# CI builds with ASan; leak reports from third-party code are not test failures.
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}

ran=0
failed=0
run() {
    name=$1
    shift
    ran=$((ran + 1))
    if "$@" >"$WORK/$name.log" 2>&1; then
        echo "PASS $name"
    else
        echo "FAIL $name (log: $WORK/$name.log)"
        tail -40 "$WORK/$name.log"
        failed=$((failed + 1))
    fi
}

group_xp3() {
    echo "== xp3: project dir, overlay precedence, archive parsing, folder layouts =="
    run xp3-precedence      python3 tests/xp3/precedence_smoke.py "$EXE"
    run xp3-elif            python3 tests/xp3/elif_smoke.py "$EXE"
    run xp3-empty-entry     python3 tests/xp3/empty_entry_smoke.py "$EXE"
    run xp3-malformed-index python3 tests/xp3/malformed_index_smoke.py "$EXE"
    run xp3-index-cycle     python3 tests/xp3/index_cycle_smoke.py "$EXE"
    run xp3-layouts         python3 tests/xp3/layout_smoke.py "$EXE"
    run xp3-crypt           python3 tests/xp3/crypt_smoke.py "$EXE"
    run xp3-tpm             python3 tests/tpm/smoke.py "$EXE"
}

group_io() {
    echo "== io: TextStream, AlphaMovie, Scripts.loadDataPack, layerEx, layerExSave, fstat, json, windowEx, input, KAG3 =="
    run textstream   python3 tests/textstream/smoke.py "$EXE"
    run alphamovie   python3 tests/alphamovie/smoke.py "$EXE"
    run loaddatapack python3 tests/pbd/loaddatapack_smoke.py "$EXE"
    run layerex      python3 tests/layerex/smoke.py "$EXE"
    run fstat        python3 tests/fstat/smoke.py "$EXE"
    run layersave    python3 tests/layersave/smoke.py "$EXE"
    run json         python3 tests/json/smoke.py "$EXE"
    run savestruct   python3 tests/savestruct/smoke.py "$EXE"
    run sprintf      python3 tests/sprintf/smoke.py "$EXE"
    run csvvar       python3 tests/csvvar/smoke.py "$EXE"
    run kagparserex  python3 tests/kagparserex/smoke.py "$EXE"
    run psdfile      python3 tests/psdfile/smoke.py "$EXE"
    run layerexdraw  python3 tests/layerexdraw/smoke.py "$EXE"
    run windowex     python3 tests/windowex/smoke.py "$EXE"
    run natives      python3 tests/natives/smoke.py "$EXE"
    run softmode     python3 tests/softmode/smoke.py "$EXE"
    run aspect       python3 tests/aspect/smoke.py "$EXE"
    run extrans      python3 tests/extrans/smoke.py "$EXE"
    run input        python3 tests/input/smoke.py "$EXE"
    run kag3         python3 tests/kag3/smoke.py "$EXE"
    run kag3-interact python3 tests/kag3/interact_smoke.py "$EXE"
}

group_decoders() {
    echo "== decoders: PBD, PSB, CXDEC =="
    local cxx=${CXX:-g++}
    local ok=1
    "$cxx" -std=c++17 -O2 -I compat tests/pbd/pbd_dump.cpp compat/pbd/pbd.cpp \
        -o "$WORK/pbd_dump" || ok=0
    "$cxx" -std=c++17 -O2 -I compat tests/psb/psb_dump.cpp compat/psb/psb.cpp -lz \
        -o "$WORK/psb_dump" || ok=0
    "$cxx" -std=c++17 -O2 -I compat tests/cxdec/cxdec_test.cpp compat/cxdec/cxdec.cpp -lz \
        -o "$WORK/cxdec_test" || ok=0
    "$cxx" -std=c++17 -O2 -I compat tests/cxdec/tinyfilter_test.cpp compat/cxdec/tinyfilter.cpp -lz \
        -o "$WORK/tinyfilter_test" || ok=0
    if [ "$ok" -ne 1 ]; then
        echo "FAIL decoder helpers did not build"
        failed=$((failed + 1))
        return
    fi
    run pbd   python3 tests/pbd/run_tests.py "$WORK/pbd_dump"
    # x86 interpreter (compat/x86) against native code: needs a 32-bit Windows
    # cross compiler to build the test DLLs from tests/tpm/src/ops.c
    if command -v i686-w64-mingw32-gcc >/dev/null && \
        "$cxx" -std=c++17 -O2 -I compat tests/tpm/x86_test.cpp compat/x86/x86emu.cpp compat/x86/pe32.cpp \
            -o "$WORK/x86_test"; then
        local dlls=()
        for o in O0 O2 Os; do
            i686-w64-mingw32-gcc -$o -march=i686 -mno-mmx -mno-sse -shared -nostdlib \
                -fno-asynchronous-unwind-tables -o "$WORK/ops-$o.dll" tests/tpm/src/ops.c \
                -Wl,--entry,_DllMain@12 -lgcc && dlls+=("$WORK/ops-$o.dll")
        done
        i686-w64-mingw32-gcc -O2 -march=i686 -mno-mmx -mno-sse -shared -nostdlib \
            -fno-asynchronous-unwind-tables -o "$WORK/ops-reloc.dll" tests/tpm/src/ops.c \
            -Wl,--entry,_DllMain@12 -Wl,--image-base,0x40000000 -Wl,--dynamicbase -lgcc \
            && dlls+=("$WORK/ops-reloc.dll")
        run x86 "$WORK/x86_test" "${dlls[@]}"
        local F=(-O2 -march=i686 -mno-mmx -mno-sse -shared -nostdlib -fno-asynchronous-unwind-tables
                 -Wl,--entry,_DllMain@12)
        if "$cxx" -std=c++17 -O2 -I compat tests/tpm/tpm_test.cpp compat/x86/x86emu.cpp compat/x86/pe32.cpp \
                compat/x86/tpm_plugin.cpp -o "$WORK/tpm_test" &&
            i686-w64-mingw32-gcc "${F[@]}" -Wl,--kill-at -o "$WORK/filter_xor.tpm" tests/tpm/src/filter_xor.c -lgcc &&
            i686-w64-mingw32-gcc "${F[@]}" -Wl,--kill-at -DKK4S_TPM_CDECL -o "$WORK/filter_table.tpm" \
                tests/tpm/src/filter_table.c -lkernel32 -lgcc &&
            i686-w64-mingw32-gcc "${F[@]}" -o "$WORK/filter_add.tpm" tests/tpm/src/filter_add.c -lgcc &&
            i686-w64-mingw32-gcc "${F[@]}" -Wl,--kill-at -o "$WORK/plugin_nofilter.tpm" \
                tests/tpm/src/plugin_nofilter.c -lgcc; then
            run tpm "$WORK/tpm_test" "$WORK"
        else
            echo "FAIL tpm (test plugins did not build)"
            failed=$((failed + 1))
        fi
    else
        echo "SKIP x86 (no i686-w64-mingw32-gcc, or the test did not build)"
    fi
    run psb   python3 tests/psb/run_tests.py "$WORK/psb_dump"
    run cxdec "$WORK/cxdec_test" tests/cxdec/vectors.txt
    run tinyfilter "$WORK/tinyfilter_test" tests/cxdec/tiny_vectors.txt
}

case "$GROUP" in
    xp3) group_xp3 ;;
    io) group_io ;;
    decoders) group_decoders ;;
    all)
        group_xp3
        group_io
        group_decoders
        ;;
    *)
        echo "unknown group: $GROUP (xp3|io|decoders|all)" >&2
        exit 2
        ;;
esac

echo "tests: $ran run, $failed failed"
[ "$failed" -eq 0 ]
