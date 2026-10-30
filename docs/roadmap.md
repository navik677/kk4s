# Roadmap

Full requirements: [original-prompt.md](original-prompt.md). Risk = chance of a big time sink.

| # | Milestone | Status | Risk |
|---|---|---|---|
| M0 | Repo structure, CI build of `kk4s.nro`, console launcher, crash-safe log | **in progress** | low |
| M1 | Boots and renders a plain unencrypted test game on hardware; letterbox scaling; `linux-sdl2` target | todo | medium (old code vs new toolchain) |
| M2 | Storage: overlay dir over xp3, SJIS/UTF-16 names, case-insensitive lookup, save redirect to `saves/<game_id>/`, memory-bounded caches | todo | medium |
| M3 | Game detection by hash + `profiles/<game_id>.json`; diagnostic report for unknown games | todo | low |
| M4 | Plugin shim: wuvorbis, krmovie, layerExDraw/Image/Alpha, extrans, stubs for the rest | todo | **high** (most games depend on these) |
| M5 | Pluggable xp3 filters (decrypt), params from user profiles on SD | todo | medium |
| M6 | Core compat (System/Window/Layer/...), TJS conformance tests, profile hooks, CJK fallback font | todo | high |
| M7 | Video (FFmpeg, alpha movies), audio semantics | todo | high |
| M8 | Input mapping, touch, graphical launcher with covers, in-game overlay menu | todo | low |

Dev-loop items (from the prompt) that go along with M1: `linux-sdl2` + ASan target, `tools/run-linux.sh`, large-stack interpreter thread, recursion guard, memory budget counters.
