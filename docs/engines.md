# Інші рушії: NScripter, Ren'Py, RealLive

Крім Kirikiri, лаунчер kk4s розпізнає ігри трьох інших рушіїв і запускає їх **окремою програмою**: kk4s не вбудовує ці рушії в себе. Власні ліцензії рушіїв (GPL, MIT) лишаються при них, а від Kirikiri вони нічого спільного не мають, тож на ігри Kirikiri не впливають.

| Рушій | Як kk4s розпізнає гру | Програма | Стан |
|---|---|---|---|
| Kirikiri | як раніше: `startup.tjs`, `*.xp3`, `data/`, XP3 в exe | сам kk4s | працює |
| NScripter | `nscript.dat`, `nscr_sec.dat`, `nscript.___`, `onscript.nt*`, `pscript.dat`, або `0.txt`/`00.txt` разом з `*.nsa`/`*.sar` | ONScripter-NX (`engines/onscripter-nx/`) | збирається; на приставці ще не перевірено |
| Ren'Py 7.x | підпапка `game/` з `*.rpy`, `*.rpyc` або `*.rpa` | renpy-switch (Ren'Py 7.3.5) + `engines/renpy/renpy.py` | пакується; на приставці ще не перевірено |
| RealLive | `Gameexe.ini` поруч із `Seen.txt` | rlvm (`engines/rlvm/`) | збирається (`rlvm.nro`); на приставці ще не перевірено. На ПК — якщо покласти `rlvm.exe` |

Kirikiri перевіряється **першим** і за тими самими правилами, що й раніше, тож папка з Kirikiri-грою ніколи не прийме інший рушій. `kk4s --list-games` на ПК показує для кожної гри, який рушій і яка програма її запустить.

## Де лежать програми

```
sdmc:/switch/kk4s/
├── kk4s.nro
└── engines/
    ├── onscripter/onscripter.nro
    ├── renpy/renpy.nro            (+ kk4s_game.txt, його пише kk4s)
    └── rlvm/rlvm.nro
```

На ПК: `<папка kk4s>/engines/<id>/<id>.exe` (Windows) або `<id>` (Linux; також шукаються `/usr/bin`, `/usr/games`). Для Ren'Py на ПК, якщо `engines/renpy/` немає, kk4s запускає власний лаунчер гри (`Game.exe` / `Game.sh` поруч із `Game.py`).

Якщо програми немає, у лаунчері з'являється підказка «Потрібен рушій: kk4s/engines/…», і гра не стартує.

## Як це працює

**Switch.** kk4s ставить програму рушія як наступний NRO (`envSetNextLoad`) і завершується, а hbloader запускає рушій. Рушій одразу ставить `kk4s.nro` своїм наступним NRO, тож коли гра закривається (будь-яким способом), повертається лаунчер kk4s.

- ONScripter: `onscripter.nro --root "<гра>" [--font "<шрифт>"] --kk4s-return "<kk4s.nro>"`. Якщо в грі немає `default.ttf`, підставляється системний шрифт Switch (кеш у `fonts/.cache/`).
- RealLive: `rlvm.nro --kk4s-return "<kk4s.nro>" "<гра>"`. Сейви — у `saves/<гра>/rlvm/` (`global.sav.gz`, `save*.sav.gz`). Шрифт — `msgothic.ttc` з папки гри, а якщо його немає — японський системний шрифт Switch (кеш у `fonts/.cache/nintendo_standard.ttf`). Журнал — `logs/rlvm.log`, помилки показує системне вікно помилки.
- Ren'Py: renpy-switch не приймає аргументів, тому kk4s пише `engines/renpy/kk4s_game.txt` (`game=`, `saves=`, `return=`), а `renpy.py` kk4s його читає. Сейви — у `saves/<гра>/renpy/`.

**ПК.** kk4s ховає своє вікно, запускає програму рушія, чекає, поки вона завершиться, і знову показує лаунчер.

## Збірка

```bash
tools/build-engines.sh onscripter
tools/build-engines.sh renpy <renpy-switch.elf> <Quickstart/romfs/Contents>
tools/build-engines.sh rlvm
```

Результат — у `out-engines/switch/`, його треба скопіювати в корінь SD-карти.

- **ONScripter-NX** (clamintus, на основі onscripter-20060724-insani, GPL-2) лежить у `engines/onscripter-nx/`, правки kk4s позначені `kk4s:`. Для збірки потрібні пакети devkitPro `switch-sdl2_image switch-sdl2_mixer switch-sdl2_ttf switch-smpeg2 switch-libmodplug switch-mpg123 switch-libvorbisidec`.
- **Ren'Py**: `renpy-switch.elf` (Ren'Py Switch SDK, uyjulian) і `lib.zip` з `renpy/common` (RenpyNXPorts Quickstart) у репозиторій не входять, їх треба взяти окремо. Скрипт підкладає замість `renpy.py` версію kk4s (`engines/renpy/renpy.py`).

- **rlvm** (Elliot Glaysher та ін., GPL-3) лежить у `engines/rlvm/`: `src/` і потрібні бібліотеки з `vendor/` (guichan, zita-resampler, xclannad, pygame, utf8cpp) з дерева rlvm-project, де вже є рендер на OpenGL ES 3.2 з Android-порту. Цей самий рендер працює на Switch через switch-mesa: у GL-файлах поруч із `__ANDROID__` стоїть `__SWITCH__`. Правки kk4s позначені `kk4s:`, оболонка для Switch — `src/platforms/switch/rlvm_switch.cc`, збірка — `engines/rlvm/Makefile`. Потрібні пакети devkitPro `switch-sdl2 switch-sdl2_image switch-sdl2_mixer switch-sdl2_ttf switch-mesa`. Boost 1.87 (filesystem, serialization, iostreams, program_options, date_time, system) і libsndfile 1.2.2 devkitPro не має, тому `tools/build-rlvm-deps.sh` завантажує їх (з перевіркою SHA-256) і збирає в `out-engines/rlvm-deps/prefix`, а не в portlibs devkitPro. На Windows скрипт запускається з bash MSYS2; для b2 один раз потрібен g++ для самого ПК (MSYS2 ucrt64 або mingw64). `tools/build-engines.sh rlvm` викликає скрипт сам.

## Керування в rlvm

Ігри RealLive розраховані на мишу, тому на Switch є вказівник:

| Кнопка | Дія |
|---|---|
| лівий стік | рухає вказівник |
| A | клік лівою кнопкою там, де вказівник: далі по тексту, вибір варіанта |
| B | клік правою кнопкою: меню гри або «назад» |
| хрестовина | стрілки |
| R / ZR (тримати) | Ctrl: пропуск тексту |
| X | пробіл |
| + | вийти (глобальні дані гри зберігаються) і повернутися в kk4s |
| сенсорний екран | як миша |

Власного курсора rlvm не малює, якщо його немає в самій грі; варіанти вибору підсвічуються, коли вказівник над ними.

## Обмеження

- **ONScripter-NX** — старий ONScripter 2006 року. Ігри для ONScripter-EN або новіших версій NScripter можуть не працювати. Сейви він пише в папку гри (`save*.dat`, `gloval.sav`), а не в `saves/`.
- **Ren'Py** — версія 7.3.5 на Python 2.7. Ігри на Ren'Py 8 (Python 3) не запустяться, а ігри новіших 7.x — не завжди. Великі ігри можуть упертися в пам'ять приставки. Автори портів радять розпакувати `.rpa` і перекодувати картинки в WebP, а звук в Opus.
- **RealLive (rlvm)** — `rlvm.nro` збирається, але на приставці ще жодного разу не запускався. Що перевірити першим: чи створюється контекст OpenGL ES 3.2 через SDL2 + switch-mesa (якщо ні — рядок `Failed to create OpenGL context` у `logs/rlvm.log`); чи правильні номери кнопок (взято розкладку джойстика SDL2 у libnx: 0 — A, 1 — B, 10 — +); швидкість великих ігор (`Seen.txt` читається в пам'ять повністю, бо `mmap` на Switch немає); звук (ресемплінг іде через тимчасові WAV-файли libsndfile). Діалоги rlvm, що питають користувача (наприклад, про зіпсовані глобальні дані), на Switch завжди отримують відповідь «ні». Ігри AVG32 і Siglus rlvm не запускає.
- Меню kk4s (L3+R3), кнопки kk4s і режими екрана в цих рушіях не працюють: кожен має своє керування.
