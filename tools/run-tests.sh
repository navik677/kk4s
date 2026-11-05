#!/usr/bin/env bash
# Run the synthetic regression suite against a built kk4s binary.
#
#   tools/run-tests.sh                       # out-desktop/kk4s, every group
#   tools/run-tests.sh --group xp3           # one group
#   tools/run-tests.sh build-desktop/kk4s    # explicit binary
#
# Groups:
#   xp3        storage layer: project dir, overlay precedence, archive parsing,
#              game-folder layouts (data/, data.exe, XP3 inside an exe)
#   io         engine I/O: TextStream encoding, AlphaMovie, Scripts.loadDataPack,
#              layerEx* / layerExSave Layer methods, fstat.dll Storages methods,
#              json.dll / scriptsEx.dll
#   decoders   PBD / PSB / CXDEC decoders vs their Python references (no engine)
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
}

group_io() {
    echo "== io: TextStream, AlphaMovie, Scripts.loadDataPack, layerEx, layerExSave, fstat, json =="
    run textstream   python3 tests/textstream/smoke.py "$EXE"
    run alphamovie   python3 tests/alphamovie/smoke.py "$EXE"
    run loaddatapack python3 tests/pbd/loaddatapack_smoke.py "$EXE"
    run layerex      python3 tests/layerex/smoke.py "$EXE"
    run fstat        python3 tests/fstat/smoke.py "$EXE"
    run layersave    python3 tests/layersave/smoke.py "$EXE"
    run json         python3 tests/json/smoke.py "$EXE"
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
