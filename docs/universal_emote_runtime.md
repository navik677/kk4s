# Universal E-mote runtime (`compat/emote`)

Status (2026-11-05): **core, cipher profiles, value model, data model,
animation runtime and renderer contract implemented and tested. Nothing is
drawn yet**, and `Motion.*` (`compat/kk4s_motion.cpp`) does not use the module
yet.

The goal is an E-mote runtime for kk4s that can take more games and E-mote
builds by adding **profiles and adapters**, without changing the core.
NekoPara Vol.1 is the first compatibility target, not the target design. Where
universality is not proven, this document says so.

Evidence used: `docs/emote_psb_research.md`, `docs/emote_psb_validation.md`,
`docs/emote_crossgame_compatibility.md`, and a schema survey of the 157
decrypted NekoPara PSB done for this work (section 6).

---

## 1. Architecture audit: what existed, what is reused

| Existing piece | Role | Used how |
|---|---|---|
| `engine/.../XP3Archive.cpp`, `compat/cxdec/tinyfilter.cpp`, `compat/kk4s_xp3crypt.cpp` | XP3 index, segments, archive filters (TinyFilter, CX, .tpm) | **Archive layer, unchanged.** The E-mote core takes bytes after it and does not know where they came from. |
| `compat/psb/psb.{h,cpp}` (`psb::Reader`) | bounds-checked PSB container walker, used by `psbfile.dll` | **Reused** as the container parser of the E-mote core; no second parser was written. |
| `tools/psb_ref.py`, `tests/psb/` | reference reader and its tests | unchanged; the synthetic writer in `tests/emote/psb_writer.h` follows the same encoding |
| `research/emote-psb/emote_psb.py` | independent Python decoder (35 tests) | used read-only as the **cross-check** in `tests/emote/run_real.py` |
| `compat/kk4s_motion.cpp` | TJS `Motion.*` shim (resource cache, playback state, no drawing) | unchanged; it is the future integration point (section 10) |
| `engine/source/core/sdl2/stubs.cpp`, `tvpgl` | software layer compositing, final frame uploaded through SDL2 | determines the renderer backend choice (section 9) |

Nothing had to be refactored. The module is plain C++17 with zlib, no engine
headers, so it builds for desktop and Switch and runs in tests without the engine.

## 2. Layers

```
archive layer (XP3, TinyFilter, ...)          -- existing kk4s code, unchanged
        | bytes of one PSB file
        v
PSB core           psb_document.h   header, version policy, mdf, cipher selection
  cipher profiles  cipher_profile.h  interface, xorshift128 implementation, registry
                   profiles/         build-specific parameters (only place with keys)
  container walk   compat/psb        psb::Reader (reused)
        | Value tree
        v
PSB value model    psb_value.h      normalized tree, no bytes, no game
        |
        v
schema adapters    model_adapter.h  claim a document by in-data markers
                   adapters/        emote_v3.cpp (E-mote content 3.03)
        | CharacterModel
        v
data model         emote_model.h    canvas, variables, timelines, objects/motions,
                                    layers, sources, controllers
        |                         \
        v                          v
animation runtime                scene evaluation          assets
animation_runtime.h, easing.h    scene.h                   emote_assets.h
variables, tweens, timelines     active frame per layer    texture bytes for a backend
        \__________________________|_________________________/
                                   v
renderer contract  render_contract.h  TextureDesc / DrawCommand / RenderBackend
```

`emote.h` is the one-call entry (`LoadCharacter`). Dependencies only point down:
the runtime does not include PSB headers, the renderer contract includes
nothing of E-mote, the core includes no model or adapter.

## 3. PSB core

`Document::Load(bytes, LoadOptions)`:

1. `mdf\0` (zlib) is inflated (512 MiB limit).
2. Header parsed (`PsbHeader`, all eight offsets).
3. Version policy (`ClassifyPsbVersion`):
   * **v2: verified** (157 real files);
   * **v3, v4: parsed, unverified** — accepted with the same layout, only
     synthetic tests; `allow_unverified_versions = false` refuses them;
   * anything else: `unsupported-version`.
4. A nonzero header `encrypt` field is `unsupported-layout` (no known scheme uses it).
5. Cipher selection (section 4), then `psb::Reader` builds the Value tree;
   the root must be an object.

Errors are `Status` values (`emote_status.h`), never exceptions or half-filled
documents: after a failed `Load` the document is empty.

## 4. Cipher profiles

A `CipherProfile` is a complete description of one scheme:

| Item | NekoPara Vol.1 profile (`emote.xorshift128.nekopara-vol1`) |
|---|---|
| algorithm | xorshift128 (`Xorshift128RegionProfile`, generic) |
| initial state | seeds `123456789`, `362436069`, `521288629`; fourth word `742877301` (strtol of the ASCII string in `.rdata`) |
| keystream | low byte first, logical `>>= 8`, new word when `cur == 0`, no skip |
| region | `[header_len, off_chunk_offsets)` |
| preconditions | container v2 only; `header_len == off_names`; offsets strictly increasing; chunk data and root inside the file |
| evidence | SHA-256 of `emoteplayer.dll` and `emotedriver.dll`; 157 real files verified |

The key and seeds exist only in `compat/emote/profiles/nekopara_vol1.cpp`.
`tests/emote/check_core_purity.py` fails if they, a game name, a character
variable name or a file-name test appear anywhere else in `compat/emote`.

**Selection** (no key search, only registered profiles):

1. `pinned_profile` set: only that profile; it must pass preconditions and
   validation, else `profile-mismatch` (unregistered id: `unknown-cipher-profile`).
2. Otherwise the plain container is tried first (`PlainContainer`).
3. Then profiles whose evidence hashes match `binary_sha256` (`BinaryEvidence`),
   then, if `allow_structural_detection`, the remaining ones (`StructureOnly`).
4. A candidate counts only if its output passes the full structural walk.
   None: `unknown-cipher-profile` (the message lists what was tried). More
   than one: `ambiguous-cipher-profile`.

A hash never selects a profile by itself; it only raises the reported
confidence. A wrong key produces a broken name trie and is refused (tested).

**Adding a profile for another build:** recover the parameters from the binary
(as in `docs/emote_psb_research.md`), add `profiles/<build>.cpp` with
`ProfileInfo` (id, versions, evidence) and `Xorshift128Params` — or a new
`CipherProfile` subclass for a different algorithm — and register it in
`profiles/builtin_profiles.cpp`. Register it only after a real sample passes
`tests/emote/run_real.py`-style checks: structure, byte-exact round-trip,
archive adler32. Nothing in the core changes.

## 5. Value model

`Value` (`psb_value.h`): undefined, null, bool, int, double, string, resource,
extra, array, object (ordered members). `Find`, `FindPath("a/0/b")`, typed
getters with defaults. Resource bytes stay in the `Document`
(`Document::Resource(index)`). Everything above the core reads this model only.

## 6. E-mote data model and the 3.03 adapter

### 6.1 What the real files are

Survey of all 157 NekoPara PSB after decryption:

| Marker | `emote.xp3` (79) | `emotewin*.xp3` `dx_` / `dxlow_` (78) |
|---|---|---|
| root `version` | 3.03 (float32 `3.0299999713897705`) | 3.03 |
| root `spec` | `krkr` | `win` |
| `metadata.format` / `metadata.version` | `emote` / 1 | `emote` / 1 |
| icons | own pixels, `compress "RL"` | in a texture atlas (`left`/`top`) |
| atlas texture | none | `RGBA8` (73, `dx_`), `DXT5` (75, `dxlow_`), with mipmaps |
| layer references | path form `src/<source>/<icon>`, `motion/<object>/<motion>`, `blank/w:h:ox:oy`, `layout`, `shape/point` | pair form `src` + `icon`; empty `src` where `krkr` files say `layout` |
| root `easing` | absent | empty list |

`root.version = 3.03` is the first E-mote content version found **inside the
data** (the DLLs expose none, see the cross-game report). `spec` is the asset
packing of the export, not the game. The adapter is keyed to
`version 3.03 + format "emote" + metadata.version 1 + spec in {krkr, win}`;
anything else is `unsupported-schema`. Another title exporting the same schema
would load through the same adapter; whether any does is **unknown**.

### 6.2 Model fields

`emote_model.h` marks every field **[C]** confirmed structure (seen in every
checked file), **[H]** hypothesis on meaning, **[R]** raw code with unknown
meaning. Summary:

| Area | Confirmed | Hypothesis | Raw / unknown |
|---|---|---|---|
| canvas | `screenSize` w/h/origin | | |
| variables | `metadata.variableList`: name, named poses (`label`, `frame`) | range = union of motion parameter ranges | |
| timelines | `metadata.timelineControl`: label, `diff`, `loopBegin/End`, tracks per variable; key = frame type 2 with `{value, easing}`; type 0 without content is always the **last** frame (end marker); keys always sorted (0 violations in 221,024 keys) | `diff` = additive; loop range in frames | `lastTime` (always -1), easing codes (0, 1, -1, -2, -10) |
| motions | `object/<o>/motion/<m>`: `lastTime`, `parameter[]` (`id`, `rangeBegin/End`, `division`), layer tree | parameter maps a variable onto the frame axis (`range -30..30`, `division 60`, `lastTime 61`) | `priority`, `loopTime`, `tag`, `variable` |
| layers | label, frames sorted by time, children; frame type 0 = no content | `coord` = x,y,z position, `angle` degrees, `opa` opacity | layer `type`, `inheritMask`, `transformOrder`, `meshDivision`, `meshTransform`, `parameterize`, frame types 2 vs 3, `mask`, `bm`, `color`, `mesh.bp` (32 numbers), `mesh.cc` |
| sources | icons: size, origin, atlas position or own pixel resource; atlas texture format/size/mipmaps; `RGBA8` resource size = `w*h*4` (73/73) | `RGBA8` byte order R,G,B,A | `RL` codec, `DXT5` not decoded, `clip`, `attr`, `truncated_*` |
| controllers | `*Control` blocks (eye, eyebrow, mouth, hair, bust, parts, clamp, transition, mirror, stereovision, loop): kind, label, enabled, variables they name | blink / lip sync / physics behaviour | all other parameters |

Real-data totals through the adapter: 5,735 variables, 2,546 timelines, 62,069
tracks, 221,024 keys (158 text values), 3,297 motions, 50,757 layers, 190,534
layer frames, 15,123 icons, 148 atlas textures, **0 unresolved references,
0 adapter warnings**.

## 7. Animation runtime

`AnimationState` (`animation_runtime.h`) works on `CharacterModel` /
`TimelineDef` only. No variable name is special; any number of characters
(one state each) and timelines.

* Variables: `SetVariable(name, value, duration_frames, easing_code)`, the
  text form (pose label or number), base vs. evaluated value, optional
  clamping to the range.
* Timelines: play / stop / stop all, start frame, weight, Override or
  Additive, loop over `[loopBegin, loopEnd)` when present.
* Track evaluation: hold the first value before the first key and the last
  after the last; between keys interpolate with the **earlier key's** curve;
  text keys hold (step).
* Time: frames; `AdvanceMs` converts with `frames_per_second` (default 60).
* Easing: kk4s' own curves (`easing.h`); code → curve comes from the
  adapter's `EasingMap`. Unknown codes fall back to linear and are reported
  (`diagnostics()`), or fail with `UnknownEasingPolicy::Error`.

kk4s decisions / hypotheses, **not checked against the original player**:

| Decision | Why it is only a hypothesis |
|---|---|
| easing code 0 = linear | 98.5 % of keys use it; no reference output compared |
| codes 1, -1, -2, -10 | unknown; linear fallback, reported |
| 60 frames per second | not found in data or code |
| `diff` timelines are additive | name only |
| a finished non-looping override timeline leaves its last value as the base | behaviour of the DLL unknown |
| interpolation uses the earlier key's easing | common convention, unverified |
| text values: pose label first, then number; held, not interpolated | 158 text keys seen, e.g. `"20"` |
| `setVariable(name, value, time, easing)` time unit | the TJS API is from the project notes; its unit (ms or frames) is not verified |

The API names used by game scripts (`setVariable`, `playTimeline`, variables
like `face_eye_open`) come from `docs/HANDOFF.md`. In this session the PSB data
confirmed the variable names (e.g. `face_eye_open` is the `eyeControl` label,
`head_LR` / `body_slant` are parameter ids), but the game's script calls could
not be re-read: `research/emote-psb/extract_xp3.py` does not read the
`data.xp3` segment layout.

## 8. Geometry and deformation

**Not researched; nothing is invented.** Known to exist in the data and kept raw:

* `meshDivision` (10/16/20), `meshTransform`, `meshCombine`, `meshSyncChildMask`;
* per-frame `mesh.bp` — 32 numbers in 56,088 frames, `mesh.cc` always null;
* `transformOrder` (4 ints, e.g. `0 3 2 1`), `coord` (3 numbers), `angle`;
* `inheritMask`, `mask`, `stencilType`, `stencilCompositeMaskLayerList`;
* `bm` (blend mode code), `color` (e.g. `0x808080FF`), `opa`;
* `parameterize` on layers (small ints; leading hypothesis: index into the
  motion's `parameter[]`, so a variable drives the layer's frame position —
  `ParameterToFrame` implements that mapping for experiments only).

What a renderer needs and is missing: control-point layout of `bp` (likely a
4x4 bezier patch of x,y pairs — unverified), how `transformOrder` composes
translation/rotation/scale, mask and stencil rules, blend codes, the `RL`
codec and DXT5 decoding. The scene evaluator therefore only selects the
**active frame per layer** (last frame with `time <= t`, type 0 = hidden) and
expands nested motions (depth limit 16, cycle cut); on real data 195,710 nodes,
0 unresolved, 2,198 nested references cut — whether those are real cycles or
deep chains was not analysed.

## 9. Renderer

kk4s composes every frame in software (Kirikiri layers, `tvpgl`) and uploads
the final image through SDL2 on desktop and Switch. `Motion.Player.draw(target)`
draws into a Kirikiri layer. So the first backend is meant to be a **CPU
rasterizer into a layer bitmap**; it works the same on Windows, Linux and
Switch (libnx + SDL2). A GPU backend can implement the same interface later.

`render_contract.h` defines the boundary, independent of PSB and games:
`TextureDesc` (RGBA8), `DrawCommand` (textured triangles, blend, opacity),
`RenderBackend` (create/destroy texture, begin/submit/end frame),
`ValidateDrawCommand`, and `RecordingBackend` for tests.
`emote_assets.h` hands out only pixel data whose layout was checked (`RGBA8`,
`w*h*4` bytes); `DXT5` and `RL` are `unsupported-feature`.

Not implemented: scene → draw commands (needs section 8).

## 10. Integration plan (not done)

1. `kk4s_motion.cpp`: load resources through `LoadCharacter` (passing the
   SHA-256 of the game's `emoteplayer.dll` as evidence), expose the model's
   timeline labels and variables to `Motion.EmotePlayer`, drive an
   `AnimationState` from `progress()`.
2. CPU backend into the target layer; first a static pose from the `win`
   atlas (`RGBA8`, no deformation).
3. Research `mesh.bp` / `transformOrder` with real frames, then deformation.

## 11. Tests and results

Kept apart on purpose:

| Kind | What | Result here |
|---|---|---|
| **synthetic** | `tests/emote/emote_tests.cpp` (13 groups, fixtures in the real section order): keystream vector, byte-exact refill, region-only XOR, all selection paths and confidences, unknown / custom / ambiguous profiles, versions, layout preconditions, mdf, truncation and byte flips (no crash), adapter on both reference forms, refused versions/specs, easing, runtime, scene, renderer contract, end-to-end | **68,117 checks, 0 failed** (MSYS2 g++ 16.2, Windows) |
| **core purity** | `tests/emote/check_core_purity.py` | 25 files, 0 findings |
| **real resources** | `tests/emote/run_real.py` on NekoPara Vol.1 (read-only, game outside the repo) | **157/157 PSB load** with `binary-evidence`; 157/157 match the Python decoder (adler32 of the decrypted container); 11/11 non-PSB entries refused as `not-psb`; 0 unresolved references; 73/73 `RGBA8` atlases size-checked, 75 `DXT5` refused as unsupported; 2,546 timelines played to their end, 0 non-finite values; evidence policy on a real file: no hash → `structure-only`, structural detection off → `unknown-cipher-profile`, pinned → `pinned`; archives unchanged |
| PC runtime (engine) | — | **not done**: the module is not wired into `Motion.*` |
| Switch build | all 16 sources compiled with devkitA64 16.1 (`-march=armv8-a+crc+crypto -mtp=soft -fPIE`, libnx headers) | **compiles, 0 warnings**; full NRO not built in this session |
| physical Switch | — | **not tested** |

Run them:

```sh
tools/run-tests.sh --group decoders          # builds and runs emote + emote-purity
python3 tests/emote/run_real.py BUILD/emote_tool --game "PATH/NekoPara Vol.1"
```

`emote_tool` is built from `tests/emote/emote_tool.cpp` (see its header). The
real-resource report goes to `out/emote-real/report.json` (gitignored).

## 12. Known limitations and open assumptions

* Only one cipher profile and one content schema exist, both from one game.
  Portability of either to other games or builds is **not proven**.
* No pixels are decoded except `RGBA8`; `RL` and `DXT5` are not researched.
* No deformation, transform composition, masks or blending (section 8).
* Runtime semantics listed in section 7 are kk4s choices.
* Physics, blink, lip sync and the other controller blocks are recognised,
  not run.
* PSB v3/v4 handling is synthetic only.
* The game scripts' use of the E-mote API was not re-verified in this session.
