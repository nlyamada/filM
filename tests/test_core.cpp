// GUI 非依存のロジックの簡易テスト。 .\build.ps1 test で実行する。
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "completion.h"
#include "favorites.h"
#include "i18n.h"
#include "json.h"
#include "file_list.h"
#include "keyconfig.h"
#include "marks.h"
#include "paths.h"
#include "shell_actions.h"
#include "text_codec.h"

static int g_failed = 0;

static void Check(bool ok, const char* expr, int line) {
    if (!ok) {
        std::printf("FAIL line %d: %s\n", line, expr);
        ++g_failed;
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

static bool Contains(const std::vector<std::wstring>& v, const std::wstring& s) {
    for (const auto& x : v) {
        if (x == s) return true;
    }
    return false;
}

int main() {
    // 実際の設定を汚さないよう、テスト用の設定ディレクトリを使う。
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring cfg = std::wstring(tmp) + L"film_test_cfg";
    SetEnvironmentVariableW(L"FILM_CONFIG_DIR", cfg.c_str());

    // ResolvePath
    CHECK(ResolvePath(L"C:\\a\\b", L"..") == L"C:\\a");
    CHECK(ResolvePath(L"C:\\a\\b", L"..\\..\\..") == L"C:\\");
    CHECK(ResolvePath(L"C:\\a\\b", L"c\\.\\d") == L"C:\\a\\b\\c\\d");
    CHECK(ResolvePath(L"C:\\a\\b", L"D:\\x\\y\\") == L"D:\\x\\y");
    CHECK(ResolvePath(L"C:\\a\\b", L"\\z") == L"C:\\z");
    CHECK(ResolvePath(L"C:\\a\\b", L"D:\\") == L"D:\\");
    CHECK(ResolvePath(L"C:\\a", L"//srv/share/x/../y") == L"\\\\srv\\share\\y");
    CHECK(ResolvePath(L"C:\\", L"foo") == L"C:\\foo");

    // ルートの取得
    CHECK(GetRootOfPath(L"C:\\a\\b") == L"C:\\");
    CHECK(GetRootOfPath(L"\\\\srv\\share\\x\\y") == L"\\\\srv\\share\\");

    // エイリアス (フォルダのお気に入り)
    CHECK(AddFavorite(L"WIN", L"C:\\Windows", L'w') == AddResult::Added);
    CHECK(AddFavorite(L"Other", L"C:\\Windows") == AddResult::DuplicatePath);
    CHECK(AddFavorite(L"win", L"C:\\Temp") == AddResult::DuplicateName);  // 名前は大文字小文字を区別しない
    CHECK(AddFavorite(L"Temp", L"C:\\Temp", L'w') == AddResult::Added);  // 同じキーは後勝ち
    CHECK(GetFavorites()[0].name == L"Temp" && GetFavorites()[1].name == L"WIN");  // 名前順に並ぶ
    CHECK(FindFavoriteByKey(L'W') == 0);
    CHECK(GetFavorites()[1].key == 0);
    CHECK(FindFavoriteByName(L"wIn") != nullptr);

    // エイリアスの読み替え
    bool expanded = false;
    CHECK(ExpandAliasPath(L"win", &expanded) == L"C:\\Windows" && expanded);
    CHECK(ExpandAliasPath(L"WIN\\System32", &expanded) == L"C:\\Windows\\System32" && expanded);
    CHECK(ExpandAliasPath(L"win/Fonts", &expanded) == L"C:\\Windows\\Fonts" && expanded);
    CHECK(ExpandAliasPath(L"windows", &expanded) == L"windows" && !expanded);  // 名前が一致しなければそのまま
    CHECK(ExpandAliasPath(L"C:\\x", &expanded) == L"C:\\x" && !expanded);

    // 補完: エイリアス名・フォルダ名
    CompletionResult r = ComputeFolderCompletions(L"wi", 2, L"C:\\");
    CHECK(Contains(r.candidates, L"WIN\\"));
    CHECK(Contains(r.candidates, L"Windows\\"));
    r = ComputeFolderCompletions(L"Sys", 3, L"C:\\Windows");
    CHECK(Contains(r.candidates, L"System32\\") && r.replaceStart == 0 && r.replaceEnd == 3);
    // エイリアスの続きの補完 (入力の形を保つ)
    r = ComputeFolderCompletions(L"WIN\\Sys", 7, L"D:\\");
    CHECK(Contains(r.candidates, L"WIN\\System32\\"));
    r = ComputeFolderCompletions(L"win\\system32\\driv", 17, L"D:\\");
    CHECK(Contains(r.candidates, L"win\\system32\\drivers\\"));
    r = ComputeFolderCompletions(L"WIN\\", 4, L"D:\\");
    CHECK(Contains(r.candidates, L"WIN\\Fonts\\"));
    // フルパス・ドライブ指定・引用符
    r = ComputeFolderCompletions(L"C:\\Windows\\Sys", 14, L"D:\\");
    CHECK(Contains(r.candidates, L"C:\\Windows\\System32\\"));
    r = ComputeFolderCompletions(L"\"C:\\Program F", 14, L"D:\\");
    CHECK(Contains(r.candidates, L"C:\\Program Files\\"));
    r = ComputeFolderCompletions(L"C:", 2, L"D:\\");
    CHECK(Contains(r.candidates, L"C:\\"));
    // ファイルは候補に出さない (フォルダのみ)
    r = ComputeFolderCompletions(L"C:\\Windows\\notepad", 18, L"D:\\");
    CHECK(r.candidates.empty());

    RemoveFavoriteAt(1);
    RemoveFavoriteAt(0);
    CHECK(GetFavorites().empty());

    // マーク
    MarkSet marks;
    marks.Toggle(L"a.txt");
    marks.Toggle(L"b.txt");
    CHECK(marks.Contains(L"A.TXT"));
    marks.Toggle(L"a.txt");
    CHECK(!marks.Contains(L"a.txt") && marks.Count() == 1);

    // ソート (フォルダは常に先頭)
    std::wstring dir = std::wstring(tmp) + L"film_test_sort";
    CreateDirectoryW(dir.c_str(), nullptr);
    CreateDirectoryW((dir + L"\\zdir").c_str(), nullptr);
    auto writeFile = [&](const wchar_t* name, size_t bytes, char fill = 'x') {
        HANDLE h = CreateFileW((dir + L"\\" + name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        std::string data(bytes, fill);
        DWORD w;
        WriteFile(h, data.data(), static_cast<DWORD>(bytes), &w, nullptr);
        CloseHandle(h);
    };
    writeFile(L"b.zip", 300);
    writeFile(L"a.txt", 100);
    writeFile(L"c.bin", 200, '\0');  // NUL を含む = バイナリ
    auto names = [&](SortKey k, bool desc) {
        std::wstring s;
        for (const FileEntry& e : EnumerateDirectory(dir, k, desc)) s += e.name + L",";
        return s;
    };
    CHECK(names(SortKey::Name, false) == L"zdir,a.txt,b.zip,c.bin,");
    CHECK(names(SortKey::Name, true) == L"zdir,c.bin,b.zip,a.txt,");
    CHECK(names(SortKey::Size, false) == L"zdir,a.txt,c.bin,b.zip,");
    CHECK(names(SortKey::Size, true) == L"zdir,b.zip,c.bin,a.txt,");
    CHECK(names(SortKey::Ext, false) == L"zdir,c.bin,a.txt,b.zip,");

    // バイナリ判定・画像判定・重複しない名前
    CHECK(IsBinaryFile(dir + L"\\c.bin"));
    CHECK(!IsBinaryFile(dir + L"\\a.txt"));
    CHECK(IsImageFile(L"C:\\x\\photo.JPG") && !IsImageFile(L"C:\\x\\a.txt"));
    CHECK(MakeUniquePath(dir + L"\\a.txt", false) == dir + L"\\a (2).txt");
    CHECK(MakeUniquePath(dir + L"\\zdir", true) == dir + L"\\zdir (2)");
    CHECK(MakeUniquePath(dir + L"\\none.txt", false) == dir + L"\\none.txt");

    DeleteFileW((dir + L"\\b.zip").c_str());
    DeleteFileW((dir + L"\\a.txt").c_str());
    DeleteFileW((dir + L"\\c.bin").c_str());
    RemoveDirectoryW((dir + L"\\zdir").c_str());
    RemoveDirectoryW(dir.c_str());

    // 永続化の往復 (テスト用設定ディレクトリ)
    AddFavorite(L"Roundtrip", L"C:\\Windows", L'r');
    CHECK(LoadFavorites());  // 保存内容から読み直す
    CHECK(!GetFavorites().empty() && GetFavorites()[0].name == L"Roundtrip" && GetFavorites()[0].key == L'R');
    CHECK(!ReloadFavoritesIfChanged());  // 自分が書いた直後は「変更あり」にならない
    RemoveFavoriteAt(0);
    CHECK(FindFavoriteByName(L"Roundtrip") == nullptr);

    // キー割り当て: CSV の主なキーが既定で割り当てられていること
    std::vector<KeyBinding> keys = GetDefaultKeyBindings();
    CHECK(FindCommandForKey(keys, L"A", false) == L"Attributes");
    CHECK(FindCommandForKey(keys, L"C", true) == L"CopyToFolder");
    CHECK(FindCommandForKey(keys, L"X", true) == L"OpenCmd");
    CHECK(FindCommandForKey(keys, L"Z", false) == L"Undo");
    CHECK(FindCommandForKey(keys, L"Delete", true) == L"DeletePermanent");
    CHECK(FindCommandForKey(keys, L"Backspace", true) == L"NavigateToRoot");
    CHECK(FindCommandForKey(keys, L",", false) == L"PrevTab");
    CHECK(FindCommandForKey(keys, L".", false) == L"NextTab");
    CHECK(FindCommandForKey(keys, L"Space", false) == L"ToggleMark");
    CHECK(FindCommandForKey(keys, L"Enter", true) == L"OpenWith");
    CHECK(FindCommandForKey(keys, L"L", false) == L"TabSetMenu");
    CHECK(FindCommandForKey(keys, L"L", true) == L"NewTabSet");
    CHECK(FindCommandForKey(keys, L",", true) == L"MoveTabLeft");
    CHECK(FindCommandForKey(keys, L".", true) == L"MoveTabRight");
    CHECK(KeyNameToVirtualKey(L",") == VK_OEM_COMMA && KeyNameToVirtualKey(L".") == VK_OEM_PERIOD);
    // 一覧に出すキーのコマンドには、すべて表示名があること
    for (const PanelKey& pk : GetPanelKeys()) {
        for (bool shift : {false, true}) {
            std::wstring c = FindCommandForKey(keys, pk.key, shift);
            if (!c.empty() && GetCommandLabel(c).empty()) {
                std::wprintf(L"label missing: %ls\n", c.c_str());
                ++g_failed;
            }
        }
    }
    // 同じキー (修飾キーの組み合わせ) に複数のコマンドが割り当たっていないこと
    for (size_t i = 0; i < keys.size(); ++i) {
        for (size_t j = i + 1; j < keys.size(); ++j) {
            bool same = KeyNameToVirtualKey(keys[i].key) == KeyNameToVirtualKey(keys[j].key) &&
                        keys[i].ctrl == keys[j].ctrl && keys[i].alt == keys[j].alt && keys[i].shift == keys[j].shift;
            if (same) {
                std::wprintf(L"key conflict: %ls / %ls (%ls)\n", keys[i].command.c_str(), keys[j].command.c_str(),
                             keys[i].key.c_str());
                ++g_failed;
            }
        }
    }

    // 日英切り替え: 英語のとき、コマンド名の表示に日本語が残らないこと。Fmt が {0} を差し込むこと。
    auto hasCjk = [](const std::wstring& s) {
        for (wchar_t c : s) {
            if (c >= 0x3040) return true;
        }
        return false;
    };
    SetLanguage(Language::English);
    for (const auto& cmd : GetAllCommands()) {
        if (hasCjk(cmd.second)) {
            std::wprintf(L"untranslated command label: %ls\n", cmd.first.c_str());
            ++g_failed;
        }
    }
    CHECK(Fmt(L"{0} 件を複製しました。", {L"3"}) == L"Duplicated 3 item(s).");
    CHECK(Fmt(L"{0} / {1} 件", {L"1", L"2"}) == L"1 of 2");
    SetLanguage(Language::Japanese);
    CHECK(Fmt(L"{0} 件を複製しました。", {L"3"}) == L"3 件を複製しました。");
    CHECK(std::wstring(Tr(L"未登録の文字列")) == L"未登録の文字列");  // 表に無ければそのまま
    CHECK(VirtualKeyToKeyName(VK_OEM_COMMA) == L"," && VirtualKeyToKeyName('T') == L"T" && VirtualKeyToKeyName(VK_F5) == L"F5");
    KeyBinding combo;
    combo.key = L"T";
    combo.ctrl = true;
    combo.shift = true;
    CHECK(FormatKeyCombo(combo) == L"Ctrl+Shift+T");
    CHECK(FindCommandForKey(keys, L"V", true) == L"MaximizePreview");
    CHECK(FindCommandForKey(keys, L"U", true) == L"UnzipToFolder");
    CHECK(FindCommandForKey(keys, L"V", false) == L"TogglePreview");
    CHECK(FindCommandForKey(keys, L"Tab", false) == L"FocusPreview");

    // ---- JSON: 深すぎる入れ子は拒否する (スタックを使い切らない) ----
    {
        JsonValue v;
        CHECK(ParseJson(L"{\"a\":[1,2,{\"b\":null}]}", v));
        CHECK(!ParseJson(std::wstring(100000, L'[') + std::wstring(100000, L']'), v));
        CHECK(ParseJson(std::wstring(60, L'[') + std::wstring(60, L']'), v));
    }

    // ---- テキストの読み書き (text_codec) ----
    {
        wchar_t tmpDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpDir);
        std::wstring dir = std::wstring(tmpDir) + L"film_codec_test";
        CreateDirectoryW(dir.c_str(), nullptr);
        auto writeBytes = [&](const std::wstring& name, const std::string& bytes) {
            std::wstring p = dir + L"\\" + name;
            HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD n = 0;
            WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &n, nullptr);
            CloseHandle(h);
            return p;
        };
        TextDocument d;
        // UTF-8 (BOM なし) / CRLF
        std::wstring p1 = writeBytes(L"u8.txt", "a\r\n\xE3\x81\x82\r\nb");
        CHECK(LoadTextDocument(p1, d, 1 << 20) == TextLoadResult::Ok);
        CHECK(d.encoding == TextEncoding::Utf8 && d.crlf && d.text == L"a\nあ\nb");
        // UTF-8 BOM / LF
        std::wstring p2 = writeBytes(L"u8bom.txt", "\xEF\xBB\xBF" "x\ny\n");
        CHECK(LoadTextDocument(p2, d, 1 << 20) == TextLoadResult::Ok && d.encoding == TextEncoding::Utf8Bom && !d.crlf && d.text == L"x\ny\n");
        // UTF-16 LE (BOM)
        std::wstring p3 = writeBytes(L"u16.txt", std::string("\xFF\xFE" "A\0\x42\x30", 6));
        CHECK(LoadTextDocument(p3, d, 1 << 20) == TextLoadResult::Ok && d.encoding == TextEncoding::Utf16LE && d.text == L"Aあ");
        // UTF-8 として不正 -> ANSI。BOM のない UTF-16 風 (NUL を含む) と、大きすぎるファイル。
        std::wstring p4 = writeBytes(L"ansi.txt", "\x83\x65\x83\x58\x83\x67");
        CHECK(LoadTextDocument(p4, d, 1 << 20) == TextLoadResult::Ok && d.encoding == TextEncoding::Ansi);
        CHECK(LoadTextDocument(writeBytes(L"bin.dat", std::string("ab\0cd", 5)), d, 1 << 20) == TextLoadResult::Binary);
        CHECK(LoadTextDocument(p1, d, 3) == TextLoadResult::TooLarge);
        CHECK(LoadTextDocument(dir + L"\\none.txt", d, 1 << 20) == TextLoadResult::CannotOpen);
        for (const wchar_t* n : {L"u8.txt", L"u8bom.txt", L"u16.txt", L"ansi.txt", L"bin.dat"}) DeleteFileW((dir + L"\\" + n).c_str());
        RemoveDirectoryW(dir.c_str());
    }
    std::printf(g_failed ? "FAILED (%d)\n" : "ALL PASSED\n", g_failed);
    return g_failed ? 1 : 0;
}
