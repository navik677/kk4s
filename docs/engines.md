# Інші рушії: NScripter, Ren'Py, RealLive

Крім Kirikiri, лаунчер kk4s розпізнає ігри трьох інших рушіїв. **NScripter вбудований у kk4s** (ONScripter-NX працює в тому самому `kk4s.nro`). **Ren'Py** і **RealLive** запускаються окремою програмою з `engines/`.

| Рушій | Як kk4s розпізнає гру | Хто запускає | Стан |
|---|---|---|---|
| Kirikiri | як раніше: `startup.tjs`, `*.xp3`, `data/`, XP3 в exe | сам kk4s | працює |
| NScripter | `nscript.dat`, `nscr_sec.dat`, `nscript.___`, `onscript.nt*`, `pscript.dat`, або `0.txt`/`00.txt` разом з `*.nsa`/`*.sar` | ONScripter-NX **всередині kk4s** (`engines/onscripter-nx/`) | на ПК синтетичний тест проходить; на приставці ще не перевірено |
| Ren'Py 7.x | підпапка `game/` з `*.rpy`, `*.rpyc` або `*.rpa` | окремий `renpy.nro`: renpy-switch (Ren'Py 7.3.5) + `engines/renpy/renpy.py` | пакується; на приставці ще не перевірено |
| RealLive | `Gameexe.ini` поруч із `Seen.txt` | окремий `rlvm` | порт на Switch у роботі; на ПК — якщо покласти `rlvm.exe` |

У лаунчері є бічна панель (ZL / ZR або тап), коли ігри на кількох рушіях: усі ігри або ігри одного рушія. Папка в `games/`, яка сама не гра, але містить кілька ігор (`games/ons/<гра>/`), — це збірка: кожна її гра показується окремо, сейви — у `saves/ons/<гра>/`.

Kirikiri перевіряється **першим** і за тими самими правилами, що й раніше, тож папка з Kirikiri-грою ніколи не прийме інший рушій. `kk4s --list-games` на ПК показує для кожної гри рушій і те, чим вона запуститься (`NScripter: built in`, шлях до програми або `(engine not installed)`).

## NScripter усередині kk4s

Після лаунчера процес kk4s запускає або Kirikiri, або ONScripter. Вікно лаунчера закривається, ONScripter відкриває своє. Коли гра закінчується (ONScripter виходить через `exit()`), kk4s робить те саме, що й після гри Kirikiri: повертається в лаунчер або виходить, залежно від налаштування.

- **Сейви** (`save*.dat`, `gloval.sav`, `envdata`, `kidoku.dat`) пишуться в `saves/<гра>/`. Читаються спершу звідти, а потім із папки гри, тож старі сейви з папки гри теж видно. Папка гри не змінюється.
- **Шрифт**: `default.ttf` гри, а якщо його немає — перший системний шрифт kk4s (на Switch це кешований системний шрифт Nintendo з `fonts/.cache/`, на ПК — MS Gothic та інші).
- **Керування** — розкладка kk4s (налаштування → «Керування»): A — далі (Enter), B — меню гри (правий клік = Esc у ONScripter), ZR/X — пропуск, L/R — історія тексту, хрестовина/стік — вибір варіантів, тач — миша. **L3 + R3** (на ПК — F1) відкривають меню kk4s поверх гри: продовжити, керування, вихід у лаунчер, вихід із kk4s. Швидкого збереження, гучності й режиму екрана в цьому меню немає: їх виконує лише Kirikiri-частина.
- **Шрифти**: SDL_ttf від 2.0.18 віддає гліф на всю висоту рядка, а ONScripter 2006 року ставив його на базову лінію ще раз («сходинки», обрізані g/j/p/y). kk4s обрізає гліф до його рамки перед кешем.

**Збірка.** `KK4S_ONSCRIPTER=AUTO` (за замовчуванням): ONScripter вбудовується, якщо знайдено SDL2_image, SDL2_mixer, SDL2_ttf і smpeg2. Без них kk4s збирається як раніше, а NScripter-ігри шукають окрему програму `engines/onscripter/`.

- Switch: пакети devkitPro `switch-sdl2_image switch-sdl2_mixer switch-sdl2_ttf switch-smpeg2 switch-libmodplug switch-mpg123 switch-libvorbisidec` (`tools/build-switch.sh` ставить їх сам). SDL_mixer з devkitPro декодує Ogg через Tremor, у якого ті самі назви функцій, що й у libvorbis kk4s і FFmpeg, але інші аргументи. Тому CMake робить у папці збірки копії `libSDL2_mixer.a` і `libvorbisidec.a` з перейменованими символами Tremor (`cmake/kk4s_tremor_rename.cmake`).
- ПК (MSYS2 UCRT64): `mingw-w64-ucrt-x86_64-SDL2_image`, `-SDL2_mixer`, `-SDL2_ttf`, `-smpeg2`. Їхні DLL треба покласти поруч із `kk4s.exe`.
- Класи ONScripter (`FontInfo`, `DirtyRect`, …) мають ті самі назви, що й класи рушія Kirikiri, тому під час компіляції їм додається префікс `ons_` (`CMakeLists.txt`). Код ONScripter для цього не змінюється.

**Ліцензія.** ONScripter-NX — GPL-2 або новіша. Коли він вбудований, бінарник kk4s поширюється під GPL (BSD-ліцензія Kirikiri з цим сумісна).

## Ren'Py і RealLive: окремі програми

```
sdmc:/switch/kk4s/
├── kk4s.nro
└── engines/
    ├── renpy/renpy.nro            (+ kk4s_game.txt, його пише kk4s)
    ├── rlvm/rlvm.nro              (порт у роботі)
    └── onscripter/onscripter.nro  (лише для kk4s, зібраного без ONScripter)
```

На ПК: `<папка kk4s>/engines/<id>/<id>.exe` (Windows) або `<id>` (Linux; також шукаються `/usr/bin`, `/usr/games`). Для Ren'Py на ПК, якщо `engines/renpy/` немає, kk4s запускає власний лаунчер гри (`Game.exe` / `Game.sh` поруч із `Game.py`).

Якщо програми немає, у лаунчері з'являється підказка «Потрібен рушій: kk4s/engines/…», і гра не стартує.

**Switch.** kk4s ставить програму рушія наступним NRO (`envSetNextLoad`) і завершується, а hbloader її запускає. Рушій одразу ставить `kk4s.nro` своїм наступним NRO, тож коли гра закривається, повертається лаунчер kk4s.

- Ren'Py: renpy-switch не приймає аргументів, тому kk4s пише `engines/renpy/kk4s_game.txt` (`game=`, `saves=`, `return=`), а `renpy.py` kk4s його читає. Сейви — у `saves/<гра>/renpy/`.
- Окремий ONScripter: `onscripter.nro --root "<гра>" [--font "<шрифт>"] --kk4s-return "<kk4s.nro>"`.

**ПК.** kk4s ховає своє вікно, запускає програму рушія, чекає, поки вона завершиться, і знову показує лаунчер.

```bash
tools/build-engines.sh renpy <renpy-switch.elf> <Quickstart/romfs/Contents>
tools/build-engines.sh onscripter
```

Результат — у `out-engines/switch/`, його треба скопіювати в корінь SD-карти. `renpy-switch.elf` (Ren'Py Switch SDK, uyjulian) і `lib.zip` з `renpy/common` (RenpyNXPorts Quickstart) у репозиторій не входять. Скрипт підкладає замість `renpy.py` версію kk4s (`engines/renpy/renpy.py`).

Ren'Py навмисно лишається окремою програмою: це цілий Python 2.7 з pygame_sdl2 (бінарник ~130 МБ). Усередині kk4s.nro він займав би пам'ять і під час ігор Kirikiri.

## Обмеження

- **ONScripter-NX** — старий ONScripter 2006 року. Ігри для ONScripter-EN або новіших версій NScripter можуть не працювати.
- **Ren'Py** — версія 7.3.5 на Python 2.7. Ігри на Ren'Py 8 (Python 3) не запустяться, а ігри новіших 7.x — не завжди. Великі ігри можуть упертися в пам'ять приставки. Автори портів радять розпакувати `.rpa` і перекодувати картинки в WebP, а звук в Opus.
- **RealLive (rlvm)** — порт на Switch потребує Boost, OpenGL ES (Mesa) і Guichan; іде окремою роботою.
