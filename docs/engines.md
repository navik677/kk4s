# Інші рушії: NScripter, Ren'Py, RealLive

Крім Kirikiri, лаунчер kk4s розпізнає ігри трьох інших рушіїв і запускає їх **окремою програмою**: kk4s не вбудовує ці рушії в себе. Власні ліцензії рушіїв (GPL, MIT) лишаються при них, а від Kirikiri вони нічого спільного не мають, тож на ігри Kirikiri не впливають.

| Рушій | Як kk4s розпізнає гру | Програма | Стан |
|---|---|---|---|
| Kirikiri | як раніше: `startup.tjs`, `*.xp3`, `data/`, XP3 в exe | сам kk4s | працює |
| NScripter | `nscript.dat`, `nscr_sec.dat`, `nscript.___`, `onscript.nt*`, `pscript.dat`, або `0.txt`/`00.txt` разом з `*.nsa`/`*.sar` | ONScripter-NX (`engines/onscripter-nx/`) | збирається; на приставці ще не перевірено |
| Ren'Py 7.x | підпапка `game/` з `*.rpy`, `*.rpyc` або `*.rpa` | renpy-switch (Ren'Py 7.3.5) + `engines/renpy/renpy.py` | пакується; на приставці ще не перевірено |
| RealLive | `Gameexe.ini` поруч із `Seen.txt` | rlvm | ще не портовано на Switch; на ПК — якщо покласти `rlvm.exe` |

Kirikiri перевіряється **першим** і за тими самими правилами, що й раніше, тож папка з Kirikiri-грою ніколи не прийме інший рушій. `kk4s --list-games` на ПК показує для кожної гри, який рушій і яка програма її запустить.

## Де лежать програми

```
sdmc:/switch/kk4s/
├── kk4s.nro
└── engines/
    ├── onscripter/onscripter.nro
    ├── renpy/renpy.nro            (+ kk4s_game.txt, його пише kk4s)
    └── rlvm/rlvm.nro              (поки немає)
```

На ПК: `<папка kk4s>/engines/<id>/<id>.exe` (Windows) або `<id>` (Linux; також шукаються `/usr/bin`, `/usr/games`). Для Ren'Py на ПК, якщо `engines/renpy/` немає, kk4s запускає власний лаунчер гри (`Game.exe` / `Game.sh` поруч із `Game.py`).

Якщо програми немає, у лаунчері з'являється підказка «Потрібен рушій: kk4s/engines/…», і гра не стартує.

## Як це працює

**Switch.** kk4s ставить програму рушія як наступний NRO (`envSetNextLoad`) і завершується, а hbloader запускає рушій. Рушій одразу ставить `kk4s.nro` своїм наступним NRO, тож коли гра закривається (будь-яким способом), повертається лаунчер kk4s.

- ONScripter: `onscripter.nro --root "<гра>" [--font "<шрифт>"] --kk4s-return "<kk4s.nro>"`. Якщо в грі немає `default.ttf`, підставляється системний шрифт Switch (кеш у `fonts/.cache/`).
- Ren'Py: renpy-switch не приймає аргументів, тому kk4s пише `engines/renpy/kk4s_game.txt` (`game=`, `saves=`, `return=`), а `renpy.py` kk4s його читає. Сейви — у `saves/<гра>/renpy/`.

**ПК.** kk4s ховає своє вікно, запускає програму рушія, чекає, поки вона завершиться, і знову показує лаунчер.

## Збірка

```bash
tools/build-engines.sh onscripter
tools/build-engines.sh renpy <renpy-switch.elf> <Quickstart/romfs/Contents>
```

Результат — у `out-engines/switch/`, його треба скопіювати в корінь SD-карти.

- **ONScripter-NX** (clamintus, на основі onscripter-20060724-insani, GPL-2) лежить у `engines/onscripter-nx/`, правки kk4s позначені `kk4s:`. Для збірки потрібні пакети devkitPro `switch-sdl2_image switch-sdl2_mixer switch-sdl2_ttf switch-smpeg2 switch-libmodplug switch-mpg123 switch-libvorbisidec`.
- **Ren'Py**: `renpy-switch.elf` (Ren'Py Switch SDK, uyjulian) і `lib.zip` з `renpy/common` (RenpyNXPorts Quickstart) у репозиторій не входять, їх треба взяти окремо. Скрипт підкладає замість `renpy.py` версію kk4s (`engines/renpy/renpy.py`).

## Обмеження

- **ONScripter-NX** — старий ONScripter 2006 року. Ігри для ONScripter-EN або новіших версій NScripter можуть не працювати. Сейви він пише в папку гри (`save*.dat`, `gloval.sav`), а не в `saves/`.
- **Ren'Py** — версія 7.3.5 на Python 2.7. Ігри на Ren'Py 8 (Python 3) не запустяться, а ігри новіших 7.x — не завжди. Великі ігри можуть упертися в пам'ять приставки. Автори портів радять розпакувати `.rpa` і перекодувати картинки в WebP, а звук в Opus.
- **RealLive (rlvm)** — порт на Switch потребує Boost (filesystem, serialization, iostreams), OpenGL ES (Mesa) і Guichan, тож це окрема велика робота. Поки kk4s лише розпізнає такі ігри й показує, що рушія немає.
- Меню kk4s (L3+R3), кнопки kk4s і режими екрана в цих рушіях не працюють: кожен має своє керування.
