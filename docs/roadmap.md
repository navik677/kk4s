# Roadmap

Full requirements: [original-prompt.md](original-prompt.md). Risk = chance of a big time sink.

| # | Milestone | Status | Risk |
|---|---|---|---|
| M0 | Repo structure, CI build of `kk4s.nro`, launcher, log | **done** | low |
| M1 | Runs real games on hardware; PC viewer (Linux + Windows); aspect-correct scaling | **mostly done** — scaling missing | medium |
| M2 | Storage: case-insensitive lookup, save redirect, overlay dir over xp3, memory-bounded caches | **partly** — lookup + saves done, overlay dir todo | medium |
| M3 | Game detection by hash + `profiles/<game_id>.json`; diagnostic report for unknown games | todo | low |
| M4 | Plugin shim: wuvorbis, krmovie, layerEx*, extrans, stubs for the rest | **partly** — see [plugins.md](plugins.md) | **high** |
| M5 | Decryption: Kirikiroid2 `xp3filter.tjs` (done), `.tpm` decryptors via a built-in x86 interpreter (todo) | **partly** | medium |
| M6 | Core compat (System/Window/Layer/...), TJS conformance tests, profile hooks, CJK/Cyrillic fonts | **partly** — fonts done | high |
| M7 | Video (FFmpeg, alpha movies), audio semantics | todo (audio works, video untested) | high |
| M8 | Input mapping, touch, graphical launcher with covers, in-game overlay menu | **done** | low |
