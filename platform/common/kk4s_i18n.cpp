// kk4s launcher translations (see kk4s_i18n.h).
#include "kk4s_i18n.h"
#include "kk4s_platform.h"

#include <cstdio>

namespace kk4s {
namespace i18n {

namespace {

enum Lang { kUk, kEn, kRu, kJa, kLangs };

struct Entry { const char* text[kLangs]; };

// Order must match enum Str.
const Entry kTable[kCount] = {
    {{"Kirikiri для Switch", "Kirikiri for Switch", "Kirikiri для Switch", "Switch用 Kirikiri"}},
    {{"Грати", "Play", "Играть", "プレイ"}},
    {{"Обкладинка", "Cover", "Обложка", "カバー"}},
    {{"Налаштування", "Settings", "Настройки", "設定"}},
    {{"Вихід", "Quit", "Выход", "終了"}},
    {{"Назад", "Back", "Назад", "戻る"}},
    {{"Вибрати", "Select", "Выбрать", "決定"}},
    {{"Вибір", "Choose", "Выбор", "選択"}},
    {{"Змінити запит", "Edit search", "Изменить запрос", "検索語を編集"}},
    {{"Шукати", "Search", "Искать", "検索"}},
    {{"Змінити", "Change", "Изменить", "変更"}},
    {{"Відкрити", "Open", "Открыть", "開く"}},
    {{"Ігор не знайдено", "No games found", "Игры не найдены", "ゲームが見つかりません"}},
    {{"Скопіюй папку гри Kirikiri (з data.xp3 або startup.tjs) у",
      "Copy a Kirikiri game folder (with data.xp3 or startup.tjs) to",
      "Скопируй папку игры Kirikiri (с data.xp3 или startup.tjs) в",
      "Kirikiri のゲームフォルダ（data.xp3 か startup.tjs を含む）をコピー："}},
    {{"Востаннє: %s", "Last played: %s", "Последний запуск: %s", "最終プレイ: %s"}},
    {{"Ще не запускалась", "Not played yet", "Ещё не запускалась", "未プレイ"}},
    {{"сьогодні", "today", "сегодня", "今日"}},
    {{"вчора", "yesterday", "вчера", "昨日"}},
    {{"Обкладинка з інтернету", "Cover from the internet", "Обложка из интернета", "ネットからカバーを取得"}},
    {{"Пошук: «%s»", "Search: “%s”", "Поиск: «%s»", "検索: 「%s」"}},
    {{"Шукаю", "Searching", "Ищу", "検索中"}},
    {{"Нічого не знайдено. Спробуй змінити запит.", "Nothing found. Try another search.",
      "Ничего не найдено. Попробуй изменить запрос.", "見つかりませんでした。検索語を変えてください。"}},
    {{"Немає з'єднання з VNDB: %s", "Cannot reach VNDB: %s", "Нет соединения с VNDB: %s", "VNDB に接続できません: %s"}},
    {{"Не вдалося завантажити: %s", "Download failed: %s", "Не удалось загрузить: %s", "ダウンロード失敗: %s"}},
    {{"Завантажую", "Downloading", "Загружаю", "ダウンロード中"}},
    {{"Обкладинку збережено", "Cover saved", "Обложка сохранена", "カバーを保存しました"}},
    {{"не можу записати на SD", "cannot write to the SD card", "не могу записать на SD", "SD に書き込めません"}},
    {{"Назва гри для пошуку на VNDB", "Game title to search on VNDB", "Название игры для поиска на VNDB",
      "VNDB で検索するタイトル"}},
    {{"Мова", "Language", "Язык", "言語"}},
    {{"Авто", "Auto", "Авто", "自動"}},
    {{"Вигляд", "Layout", "Вид", "表示"}},
    {{"Карусель", "Carousel", "Карусель", "カルーセル"}},
    {{"Сітка", "Grid", "Сетка", "グリッド"}},
    {{"Сортування", "Sort by", "Сортировка", "並び順"}},
    {{"За назвою", "Title", "По названию", "タイトル"}},
    {{"Нещодавні", "Recently played", "Недавние", "最近プレイ"}},
    {{"Тло з обкладинки", "Cover background", "Фон из обложки", "カバー背景"}},
    {{"Увімк.", "On", "Вкл.", "オン"}},
    {{"Вимк.", "Off", "Выкл.", "オフ"}},
    {{"Ховати відверті обкладинки", "Hide explicit covers", "Скрывать откровенные обложки", "過激なカバーを隠す"}},
    {{"Завантажити всі обкладинки", "Download all covers", "Скачать все обложки", "すべてのカバーを取得"}},
    {{"Для ігор без обкладинки, з VNDB", "For games without a cover, from VNDB", "Для игр без обложки, с VNDB",
      "カバーのないゲーム用（VNDB）"}},
    {{"Обкладинки: %s", "Covers: %s", "Обложки: %s", "カバー: %s"}},
    {{"Обкладинок завантажено: %s", "Covers downloaded: %s", "Обложек загружено: %s", "取得したカバー: %s"}},
    {{"Про kk4s", "About kk4s", "О kk4s", "kk4s について"}},
    {{"Версія", "Version", "Версия", "バージョン"}},
    {{"Папка даних", "Data folder", "Папка данных", "データフォルダ"}},
    {{"Джерело обкладинок", "Cover source", "Источник обложек", "カバーの取得元"}},
    {{"Запускає ігри Kirikiri без змін файлів гри.", "Runs Kirikiri games without changing the game files.",
      "Запускает игры Kirikiri без изменения файлов игры.", "ゲームファイルを変更せずに Kirikiri のゲームを実行します。"}},
};

Lang g_lang = kEn;
std::string g_code = "en";

Lang FromCode(const std::string& c) {
    if (c == "uk") return kUk;
    if (c == "ru") return kRu;
    if (c == "ja") return kJa;
    return kEn;
}

const char* kCodes[kLangs] = {"uk", "en", "ru", "ja"};

// Plural form index for n: Slavic one / few / many, English one / other.
int Plural(int n) {
    if (g_lang == kUk || g_lang == kRu) {
        int a = n % 10, b = n % 100;
        if (a == 1 && b != 11) return 0;
        if (a >= 2 && a <= 4 && (b < 12 || b > 14)) return 1;
        return 2;
    }
    if (g_lang == kJa) return 0;
    return n == 1 ? 0 : 1;
}

std::string Counted(int n, const char* const forms[kLangs][3]) {
    char buf[96];
    const char* f = forms[g_lang][Plural(n)];
    snprintf(buf, sizeof(buf), f, n);
    return buf;
}

} // namespace

void SetLanguage(const std::string& code) {
    std::string c = code;
    if (c.empty() || c == "auto") c = SystemLanguage();
    g_lang = FromCode(c);
    g_code = kCodes[g_lang];
}

const std::string& Language() { return g_code; }

const char* T(Str s) { return s >= 0 && s < kCount ? kTable[s].text[g_lang] : ""; }

std::string F(Str s, const std::string& arg) {
    std::string t = T(s);
    size_t at = t.find("%s");
    if (at != std::string::npos) t.replace(at, 2, arg);
    return t;
}

std::string Games(int n) {
    static const char* const forms[kLangs][3] = {
        {"%d гра", "%d гри", "%d ігор"}, {"%d game", "%d games", "%d games"},
        {"%d игра", "%d игры", "%d игр"}, {"%d 本", "%d 本", "%d 本"}};
    return Counted(n, forms);
}

std::string Launches(int n) {
    static const char* const forms[kLangs][3] = {
        {"%d запуск", "%d запуски", "%d запусків"}, {"%d launch", "%d launches", "%d launches"},
        {"%d запуск", "%d запуска", "%d запусков"}, {"%d 回起動", "%d 回起動", "%d 回起動"}};
    return Counted(n, forms);
}

const char* LanguageName(const std::string& code) {
    if (code == "uk") return "Українська";
    if (code == "en") return "English";
    if (code == "ru") return "Русский";
    if (code == "ja") return "日本語";
    return "";
}

} // namespace i18n
} // namespace kk4s
