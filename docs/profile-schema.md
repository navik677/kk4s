# Game profile schema (draft, implemented in M3)

Profiles live in `sdmc:/switch/kk4s/profiles/<game_id>.json` (user) or the app romfs (shipped, no keys). `game_id` = hash of key files (xp3 headers, startup.tjs), computed by kk4s and printed in the log.

```json
{
  "game_id": "sha1-of-key-files",
  "title": "Human readable name",
  "display": { "width": 1280, "height": 720, "scale": "fit" },
  "fonts": { "MS Gothic": "NotoSansCJKjp-Regular.otf", "MS Mincho": "NotoSerifCJKjp-Regular.otf" },
  "plugins": ["wuvorbis.dll", "krmovie.dll", "layerExDraw.dll"],
  "xp3_filter": { "id": "none", "params": {} },
  "quirks": [],
  "input": { "A": "confirm", "B": "cancel", "X": "menu", "Y": "auto", "R": "skip", "L": "backlog" },
  "video": { "decoder": "ffmpeg" },
  "hooks": []
}
```

Rules: no title-specific decryption keys in the repo — `xp3_filter.params` come from the user's own profile on SD.
