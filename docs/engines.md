# Інші рушії: NScripter, Ren'Py, RealLive

Крім Kirikiri, лаунчер kk4s розпізнає ігри трьох інших рушіїв. **NScripter вбудований у kk4s** (ONScripter-NX працює в тому самому `kk4s.nro`). **RealLive** теж може бути вбудований (rlvm, `KK4S_RLVM`); без його бібліотек він, як і **Ren'Py**, запускається окремою програмою з `engines/`.

| Рушій | Як kk4s розпізнає гру | Хто запускає | Стан |
|---|---|---|---|
| Kirikiri | як раніше: `startup.tjs`, `*.xp3`, `data/`, XP3 в exe | сам kk4s | працює |
| NScripter | `nscript.dat`, `nscr_sec.dat`, `nscript.___`, `onscript.nt*`, `pscript.dat`, або `0.txt`/`00.txt` разом з `*.nsa`/`*.sar` | ONScripter-NX **всередині kk4s** (`engines/onscripter-nx/`) | на ПК синтетичний тест проходить; на приставці ще не перевірено |
| Ren'Py 7.x | підпапка `game/` з `*.rpy`, `*.rpyc` або `*.rpa` | окремий `renpy.nro`: renpy-switch (Ren'Py 7.3.5) + `engines/renpy/renpy.py` | пакується; на приставці ще не перевірено |
| RealLive | `Gameexe.ini` поруч із `Seen.txt` | rlvm **усередині kk4s** (`KK4S_RLVM`), а без його бібліотек — окремий `rlvm.nro` (`engines/rlvm/`) | збирається; на приставці ще не перевірено. На ПК — окремий `rlvm.exe` |

Kirikiri перевіряється **першим** і за тими самими правилами, що й раніше, тож папка з Kirikiri-грою ніколи не прийме інший рушій. `kk4s --list-games` на ПК показує для кожної гри рушій і те, чим вона запуститься (`NScripter: built in`, шлях до програми або `(engine not installed)`).

## NScripter усередині kk4s

Після лаунчера процес kk4s запускає або Kirikiri, або ONScripter. Вікно лаунчера закривається, ONScripter відкриває своє. Коли гра закінчується (ONScripter виходить через `exit()`), kk4s робить те саме, що й після гри Kirikiri: повертається в лаунчер або виходить, залежно від налаштування.

- **Сейви** (`save*.dat`, `gloval.sav`, `envdata`, `kidoku.dat`) пишуться в `saves/<гра>/`. Читаються спершу звідти, а потім із папки гри, тож старі сейви з папки гри теж видно. Папка гри не змінюється.
- **Шрифт**: `default.ttf` гри, а якщо його немає — перший системний шрифт kk4s (на Switch це кешований системний шрифт Nintendo з `fonts/.cache/`, на ПК — MS Gothic та інші).
- **Керування** — своє в ONScripter-NX (A — далі, B — назад, X — меню, + — вихід, тач). Меню kk4s (L3+R3), розкладка kk4s і режими екрана kk4s у ньому поки не працюють.

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
    ├── rlvm/rlvm.nro
    └── onscripter/onscripter.nro  (лише для kk4s, зібраного без ONScripter)
```

На ПК: `<папка kk4s>/engines/<id>/<id>.exe` (Windows) або `<id>` (Linux; також шукаються `/usr/bin`, `/usr/games`). Для Ren'Py на ПК, якщо `engines/renpy/` немає, kk4s запускає власний лаунчер гри (`Game.exe` / `Game.sh` поруч із `Game.py`).

Якщо програми немає, у лаунчері з'являється підказка «Потрібен рушій: kk4s/engines/…», і гра не стартує.

**Switch.** kk4s ставить програму рушія наступним NRO (`envSetNextLoad`) і завершується, а hbloader її запускає. Рушій одразу ставить `kk4s.nro` своїм наступним NRO, тож коли гра закривається, повертається лаунчер kk4s.

- Ren'Py: renpy-switch не приймає аргументів, тому kk4s пише `engines/renpy/kk4s_game.txt` (`game=`, `saves=`, `return=`), а `renpy.py` kk4s його читає. Сейви — у `saves/<гра>/renpy/`.
- RealLive: `rlvm.nro --kk4s-return "<kk4s.nro>" "<гра>"`. Сейви — у `saves/<гра>/rlvm/` (`global.sav.gz`, `save*.sav.gz`). Шрифт — `msgothic.ttc` з папки гри; якщо його немає — `dat/migmix-1m-regular.ttf` (його кладуть Steam-версії ігор Key, як CLANNAD HD); якщо немає й того — японський системний шрифт Switch (кеш у `fonts/.cache/nintendo_standard.ttf`). Журнал — `logs/rlvm.log`, помилки показує системне вікно помилки.
- Окремий ONScripter: `onscripter.nro --root "<гра>" [--font "<шрифт>"] --kk4s-return "<kk4s.nro>"`.

**Вбудований rlvm.** З `KK4S_RLVM=ON` (або `AUTO`, коли знайдено Boost і libsndfile з `out-engines/rlvm-deps/prefix` та SDL2-порти) rlvm збирається в `kk4s.nro` і працює в тому самому процесі, як ONScripter: лаунчер закриває своє вікно, rlvm відкриває своє, сейви — ті самі `saves/<гра>/rlvm/`; повідомлення rlvm (як і ONScripter) ідуть у stdout, який на приставці нікуди не пишеться. Класи rlvm, чиї назви збігаються з класами Kirikiri (`Memory`, `Resampler`), отримують під час компіляції префікс `rlvm_`, як `ons_` в ONScripter.

**ПК.** kk4s ховає своє вікно, запускає програму рушія, чекає, поки вона завершиться, і знову показує лаунчер.

```bash
tools/build-engines.sh renpy <renpy-switch.elf> <Quickstart/romfs/Contents>
tools/build-engines.sh onscripter
tools/build-engines.sh rlvm
```

Результат — у `out-engines/switch/`, його треба скопіювати в корінь SD-карти. `renpy-switch.elf` (Ren'Py Switch SDK, uyjulian) і `lib.zip` з `renpy/common` (RenpyNXPorts Quickstart) у репозиторій не входять. Скрипт підкладає замість `renpy.py` версію kk4s (`engines/renpy/renpy.py`).

## rlvm (RealLive)

rlvm (Elliot Glaysher та ін., GPL-3) лежить у `engines/rlvm/`: `src/` і потрібні бібліотеки з `vendor/` (guichan, zita-resampler, xclannad, pygame, utf8cpp) з дерева rlvm-project, де вже є рендер на OpenGL ES 3.2 з Android-порту. Цей самий рендер працює на Switch через switch-mesa: у GL-файлах поруч із `__ANDROID__` стоїть `__SWITCH__`. Правки kk4s позначені `kk4s:`, оболонка для Switch — `src/platforms/switch/rlvm_switch.cc`, збірка — `engines/rlvm/Makefile`.

- Пакети devkitPro: `switch-sdl2 switch-sdl2_image switch-sdl2_mixer switch-sdl2_ttf switch-mesa switch-libvorbis switch-libvorbisidec`.
- Boost 1.87 (filesystem, serialization, iostreams, program_options, date_time, system) і libsndfile 1.2.2 devkitPro не має. `tools/build-rlvm-deps.sh` завантажує їх (з перевіркою SHA-256) і збирає в `out-engines/rlvm-deps/prefix`, а не в portlibs devkitPro. На Windows — з bash MSYS2; для b2 один раз потрібен g++ для самого ПК (MSYS2 ucrt64 або mingw64). `tools/build-engines.sh rlvm` викликає скрипт сам.
- Переклад: якщо гра підсовує таблицю підміни однобайтових кодів CP932 (у CLANNAD — `dsound.dll`), текст вважається перекладеним: символи таблиці отримують власну ширину, а рядки переносяться по пробілах, як у звичайному тексті (в оригіналі пробілів немає, тому rlvm рве рядок посеред слова).
- Опкод вибору `0:2:10` (SEEN9801 у CLANNAD, текст варіантів — `###PRINT(...)` зі строкових змінних) розбирається як `SelectElement`, але обробника в модулі `Sel` немає — як і для `0:2:16`; гра цей сценарій не викликає.
- Tremor: SDL_mixer з devkitPro декодує Ogg через Tremor, а голоси rlvm (`vendor/xclannad/koedec_ogg.cc`) — через libvorbisfile. Makefile робить так само, як `cmake/kk4s_tremor_rename.cmake`: копії `libSDL2_mixer.a` і `libvorbisidec.a` з перейменованими символами Tremor.
- **Щоб потім вбудувати rlvm у kk4s.nro**, як ONScripter: з `-DKK4S_EMBEDDED` точка входу — `rlvm_main()` (і rlvm не перенаправляє stdout/stderr); змінних, визначених у заголовках, немає; дубльованих глобальних символів між об'єктами rlvm немає, `--allow-multiple-definition` не потрібен. Що лишиться зробити тоді: перейменувати під час компіляції класи, які збігаються з класами Kirikiri (`-DName=rlvm_Name`, як для ONScripter у `CMakeLists.txt`), і прибрати `SDL_Quit()` з деструктора `SDLSystem`, бо він закрив би SDL самого kk4s.

**Керування.** Ігри RealLive розраховані на мишу, тому на Switch є вказівник:

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

Ren'Py навмисно лишається окремою програмою: це цілий Python 2.7 з pygame_sdl2 (бінарник ~130 МБ). Усередині kk4s.nro він займав би пам'ять і під час ігор Kirikiri.

## Обмеження

- **ONScripter-NX** — старий ONScripter 2006 року. Ігри для ONScripter-EN або новіших версій NScripter можуть не працювати.
- **Ren'Py** — версія 7.3.5 на Python 2.7. Ігри на Ren'Py 8 (Python 3) не запустяться, а ігри новіших 7.x — не завжди. Великі ігри можуть упертися в пам'ять приставки. Автори портів радять розпакувати `.rpa` і перекодувати картинки в WebP, а звук в Opus.
- **RealLive (rlvm)** — `rlvm.nro` збирається, а вбудована збірка (`KK4S_RLVM`) ще не компілювалася: у сесії, де її додано, не було devkitPro (перевірено тільки синтаксис CMake-блоку і фронтенду на заглушці `switch.h`). На приставці ще не запускався. Перевірено в Linux-контейнері з тими самими шляхами OpenGL ES на CLANNAD Russian HD (Steam, `KEY\CLANNAD_ENHD`): розбираються всі 235 сценаріїв, пролог іде російською, текст гортається кліком, меню на правий клік відкривається, перенос рядків — по пробілах, а не посеред слова. Не підтверджено збереження в слот: у меню гри клік по пункту на стенді нічого не робить (подія доходить до rlvm), тому екран «Сохранить» і завантаження ще треба перевірити на приставці. Що перевірити першим: чи створюється контекст OpenGL ES 3.2 через SDL2 + switch-mesa (якщо ні — рядок `Failed to create OpenGL context` у `logs/rlvm.log`); чи правильні номери кнопок (взято розкладку джойстика SDL2 у libnx: 0 — A, 1 — B, 10 — +); швидкість великих ігор (`Seen.txt` читається в пам'ять повністю, бо `mmap` на Switch немає); звук і голоси (ресемплінг іде через тимчасові WAV-файли libsndfile). Діалоги rlvm, що питають користувача (наприклад, про зіпсовані глобальні дані), на Switch завжди отримують відповідь «ні». Ігри AVG32 і Siglus rlvm не запускає.
