# kk4s — Kirikiri for Switch

Homebrew-рушій для Nintendo Switch, який запускає візуальні новели на Kirikiri2 / Kirikiri Z **без змін файлів гри**: кидаєш папку гри на SD-карту, вибираєш її в меню — і граєш.

> Стан: **M0 — перший білд**. Є лаунчер і запуск гри з папки; сумісність з реальними іграми ще не перевірена. Див. [docs/status.md](docs/status.md) і [docs/roadmap.md](docs/roadmap.md).

## Як користуватись

1. Скачай `kk4s.nro` з [nightly-релізу](https://github.com/navik677/kk4s/releases/tag/nightly).
2. Поклади на SD:
   ```
   sdmc:/switch/kk4s/kk4s.nro
   sdmc:/switch/kk4s/games/<НазваГри>/   ← вміст папки гри як є (data.xp3, patch.xp3, ...)
   ```
3. Запусти з hbmenu **в режимі title takeover** (тримай R при запуску будь-якої гри), не з альбому — в режимі апплета мало пам'яті.
4. Вибери гру (Вгору/Вниз, A). `+` — вихід.

Логи: `sdmc:/switch/kk4s/logs/`. Повна схема папок — [docs/sd-layout.md](docs/sd-layout.md).

Гра розпізнається, якщо в папці є `startup.tjs` або будь-який `*.xp3`. Project dir вибирається як у `krkr.exe`: `startup.tjs` → `data.xp3` → `data/`.

## PC viewer

Той самий рушій працює і на ПК (Linux, Windows — експериментально): `kk4s <папка гри>` або поклади ігри в `games/` поруч з exe. Деталі: [docs/pc-viewer.md](docs/pc-viewer.md).

## Збірка

Локально інструменти не потрібні — кожен push у `main` збирається в GitHub Actions (контейнер `devkitpro/devkita64`), `.nro` + `.elf` з'являються в артефактах і в релізі `nightly`.

Локально через Docker:
```bash
docker build -t kk4s-build .
docker run --rm -v "$PWD":/src kk4s-build bash tools/build-switch.sh
```
Або з встановленим devkitPro: `bash tools/build-switch.sh` → `out/kk4s.nro`, `out/kk4s.elf`.

## Структура

| Папка | Що там |
|---|---|
| `engine/` | Рушій [krkrs](https://github.com/uyjulian/krkrs) (uyjulian), майже без змін; наші правки позначені коментарем `kk4s:` |
| `platform/common/` | Спільне: логер, пошук ігор, вибір project dir |
| `platform/switch/` | Switch (libnx): консольний лаунчер, nxlink, пам'ять |
| `platform/desktop/` | PC viewer (Linux / Windows) |
| `profiles/` | Профілі ігор (JSON) — фікси без зміни файлів гри |
| `docs/` | План, дебаг, чекліст тестів на залізі, схема профілів |
| `tools/` | Скрипт збірки, розбір краш-репортів |
| `tests/` | Синтетичні TJS/KAG-тести (без контенту ігор) |

## Правила проєкту

- Ніколи не змінюємо файли гри. Фікси — тільки код рушія, профіль або overlay-папка.
- У репо немає ассетів ігор, тексту ігор і ключів шифрування конкретних тайтлів.
- Ліцензії: [THIRD_PARTY.md](THIRD_PARTY.md), рушій — `engine/LICENSE` (Kirikiri, BSD-подібна).
