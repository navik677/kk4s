# Tests

Synthetic fixtures only — no game content. Two kinds:

- **Engine tests** boot a generated "game" with the desktop `kk4s` binary and
  assert on what the engine logs. They exercise the real storage layer, script
  APIs and decoders.
- **Unit tests** compare a C++ decoder in `compat/` with its independent Python
  reference. They do not need the engine.

## Running

```bash
# everything, against a desktop build (default out-desktop/kk4s)
bash tools/build-desktop.sh
bash tools/run-tests.sh

# one group
bash tools/run-tests.sh --group xp3
bash tools/run-tests.sh --group io
bash tools/run-tests.sh --group decoders

# a specific binary
bash tools/run-tests.sh build-desktop/kk4s
```

Engine tests run headlessly (`SDL_VIDEODRIVER=dummy`) and write per-test logs to
`build-tests/` (`KK4S_TEST_WORK` changes the directory). A failing test prints
the tail of its own log.

## What each test protects

| Test | Group | Checks | Needs |
|---|---|---|---|
| `xp3/precedence_smoke.py` | xp3 | project dir is `<game>/data.xp3>` when `startup.tjs` is inside the archive; the current dir wins over auto paths, including overlays mounted by absolute path (`System.exePath + "patch.xp3>"`, which is how FSN mounts); the **last mounted** archive wins for a shared name; `removeAutoPath` + re-add flips the order; an overlay does not hide base-only files | engine |
| `xp3/layout_smoke.py` | xp3 | game-folder layouts besides `startup.tjs`/`data.xp3`: `data/` folder, XP3 appended to `Game.exe` (synthetic PE), `data.exe` beside a bigger exe without archive, an archive 6 MiB past the image (full scan); `--list-games` lists exactly the game folders | engine |
| `xp3/crypt_smoke.py` | xp3 | keyless XP3 encryption discovery: 18 synthetic archives, one per family / fitted XOR formula (`compat/kk4s_xp3crypt.cpp`), each encrypted by the inverse of its decryptor with the plain-data adlr, must run their startup.tjs; a plain archive must get no scheme; an unknown scheme must be reported and not bound | engine |
| `xp3/elif_smoke.py` | xp3 | XP3v2 `eliF` filename mapping | engine |
| `xp3/empty_entry_smoke.py` | xp3 | a zero-byte entry with an empty `segm` chunk opens, reads, seeks and rejects a seek past the end | engine |
| `xp3/malformed_index_smoke.py` | xp3 | a corrupt index is rejected instead of looping or reading out of bounds | engine |
| `xp3/index_cycle_smoke.py` | xp3 | a self-referential index chain is rejected instead of looping forever | engine |
| `textstream/smoke.py` | io | TextStream encoding detection (ASCII, Shift_JIS, UTF-8/BOM, GBK, malformed, truncated) and mixed sequential reads | engine |
| `alphamovie/smoke.py` | io | AlphaMovie AJPM zlib-alpha decode | engine |
| `layerex/smoke.py` | io | every layerExBtoA / layerExImage / layerExRaster method on synthetic pixels (values from the wamsoft sources), clip rects, `ltAddAlpha`, province helpers, and the engine colour round-trip (`fillRect` / `setMainPixel` / `getMainPixel`) | engine |
| `layersave/smoke.py` | io | layerExSave: PNG / TLG5 (with tags) / TLG6 / BMP round trips keep the colour order (the PNG is also decoded in Python), PNG octet, crop / diff / ooze / clearAlpha / copyBlueToAlpha / isBlank / getAverageColor, `Window.startSaveLayerImage` events with a copy of the layer | engine |
| `kag3/smoke.py` | io | the **original Kirikiri 2 KAG3 system scripts** (`tests/kag3/template/`, unmodified, Shift_JIS) play a Shift_JIS scenario with generated assets, driven by pad presses: text/`[l]`/`[p]`, `[image]`, crossfade/universal/scroll transitions, `[move]`, `[quake]`, fonts/ruby/style, macros, `[if]`, `[call]`/`[return]`, SE and BGM, `[emb]`, `saveBookMark`, `[link]` chosen with DOWN + A, and `askYesNo` (modal second window). Every KAG3-based game depends on these paths | engine |
| `natives/smoke.py` | io | without linking any plugin: the Storages / System / Window members Kirikiri Z has natively (fstat and windowEx contracts), Kirikiri 2's `System.inputString` (fed by `$KK4S_INPUT_STRING`), `Debug.visible`, language properties | engine |
| `windowex/smoke.py` | io | windowEx: monitor info / display list (with intersection) / system metrics, `%ENV%` expansion with APPDATA = save folder, cursor, Window rect/IME/hit-test members on a window created after the link, MenuItem constants | engine |
| `json/smoke.py` | io | json.dll reader (lenient syntax, escapes, errors), writer layout (CRLF/LF), saveJSON / evalJSONStorage round trip; scriptsEx additions (`equalStructNumericLoose`, MD5 of an octet, `propSet`/`propGet`, `pf*`) | engine |
| `fstat/smoke.py` | io | fstat.dll contract: `dirlist`/`dirlistEx`/`dirtree`, `fstat`/`getTime`/`setTime`/FILETIME, attributes, `truncateFile`, MD5, `TemporaryFiles` (deletes its entries, never inside the game folder); the game folder must stay unchanged | engine |
| `pbd/loaddatapack_smoke.py` | io | `Scripts.loadDataPack` returns real TJS Array/Dictionary trees and rejects an invalid pack | engine |
| `pbd/run_tests.py` | decoders | `compat/pbd` against a Python encoder: all value types, all six crypt modes, dictionary LZ4, truncation/corruption | `pbd_dump` |
| `psb/run_tests.py` | decoders | `compat/psb` against `tools/psb_ref.py`: every value type, the name trie, resources, `mdf` wrapping, corruption | `psb_dump` |
| `cxdec/cxdec_test.cpp` | decoders | `compat/cxdec` against 384 vectors from `tests/cxdec/gen_vectors.py`, plus chunked-vs-whole decryption | nothing |
| `cxdec/tinyfilter_test.cpp` | decoders | `compat/cxdec/tinyfilter` against vectors from `tests/cxdec/gen_tiny_vectors.py`: adlr-derived keystream, the `k == 0` case, chunked-vs-whole decryption | nothing |

`tools/run-tests.sh` builds the three decoder helpers itself.

`pbd/run_tests.py` and `psb/run_tests.py` take an optional second argument: a
directory of real files (`.pbd`, `.pimg`/`.scn`/`.psb`). That is a manual check
for a specific game; such files never enter the repository.

## The FSN invariant

`xp3/precedence_smoke.py` exists because Fate/stay night's Russian patch
(`rufix.xp3` + `rupatch.xp3` next to `data.xp3`) broke twice when the project
dir or the global mount order was "fixed" for another game. The test pins the
behaviour the engine has today, including the parts that look odd:

- when the project dir is `data.xp3>`, a *relative* sibling name is normalized
  into the archive (`<game>/data.xp3>patch.xp3>`);
- a name that the project archive contains is served from the project archive
  even when an overlay mounted by absolute path also has it, so overlays can
  only supply files the project archive does not contain.

FSN's real mount order is visible in its own log: `data.xp3` as the project
dir, then `video`, `sound`, `bgm`, `fgimage`, `bgimage`, `image`, `patch`..
`patch7`, `RealtaNua`, `rupatch`, `rufix`, all by absolute path. Do not change
the test to match new behaviour unless FSN has been re-checked by hand and the
change is intended.

## What CI runs

`.github/workflows/desktop.yml`:

| Job | Steps | Blocking |
|---|---|---|
| `linux` | ASan+UBSan build, boot smoke test (`tjs/hello`, 15 s under xvfb), `--group decoders`, `--group xp3`, `--group io` | yes |
| `windows` | MinGW build, `--group decoders` | yes |

The Windows job compiles the viewer but does **not** run it: a green Windows run
is a compile check plus portable decoder tests, not Windows runtime verification.

## Not covered

- Switch hardware and devkitPro builds (no Switch in CI; hardware runs are manual).
- Real games. FSN is the only real game available on this machine and is checked
  by hand; CI never sees game files.
- Rendering correctness, frame rate, aspect scaling, audio output.
- `tjs/hello/` and `tjs/input/` are manual smoke fixtures, not assertions except
  for the boot check above.
