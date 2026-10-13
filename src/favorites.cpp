#include "favorites.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

#include "json.h"
#include "paths.h"
#include "text_file_io.h"

namespace {

std::vector<FavoriteEntry> g_favorites;
FILETIME g_knownWriteTime{};  // 自分が最後に読み書きしたときのファイル更新時刻

bool EqualNoCase(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()),
                                TRUE) == CSTR_EQUAL;
}

bool GetWriteTime(const std::wstring& path, FILETIME& out) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    out = data.ftLastWriteTime;
    return true;
}

void SortFavorites();

void SaveFavorites() {
    std::wstring path = GetFavoritesFilePath();
    if (path.empty()) {
        return;
    }
    SortFavorites();
    JsonValue root = JsonValue::MakeArray();
    for (const FavoriteEntry& f : g_favorites) {
        JsonValue item = JsonValue::MakeObject();
        item.Set(L"name", JsonValue::MakeString(f.name));
        item.Set(L"path", JsonValue::MakeString(f.path));
        item.Set(L"key", JsonValue::MakeString(f.key ? std::wstring(1, f.key) : L""));
        root.arrayValue.push_back(std::move(item));
    }
    if (WriteUtf16FileAtomic(path, DumpJson(root))) {
        GetWriteTime(path, g_knownWriteTime);
    }
}

wchar_t NormalizeKey(wchar_t key) {
    return static_cast<wchar_t>(towupper(key));
}

// 一覧は常に名前順 (大文字小文字を区別しない) に並べる。画面の並びと添字が一致する。
void SortFavorites() {
    std::stable_sort(g_favorites.begin(), g_favorites.end(), [](const FavoriteEntry& a, const FavoriteEntry& b) {
        return CompareStringOrdinal(a.name.c_str(), static_cast<int>(a.name.size()), b.name.c_str(),
                                    static_cast<int>(b.name.size()), TRUE) == CSTR_LESS_THAN;
    });
}

bool NameInUse(const std::wstring& name, size_t exceptIndex) {
    for (size_t i = 0; i < g_favorites.size(); ++i) {
        if (i != exceptIndex && EqualNoCase(g_favorites[i].name, name)) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::wstring GetFavoritesFilePath() {
    return GetConfigFilePath(L"favorites.json");
}

AddResult AddFavorite(const std::wstring& name, const std::wstring& path, wchar_t key) {
    for (const FavoriteEntry& f : g_favorites) {
        if (EqualNoCase(f.path, path)) {
            return AddResult::DuplicatePath;
        }
    }
    if (NameInUse(name, static_cast<size_t>(-1))) {
        return AddResult::DuplicateName;
    }
    g_favorites.push_back(FavoriteEntry{name, path, 0});
    SetFavoriteKey(g_favorites.size() - 1, key);  // 内部で保存する
    return AddResult::Added;
}

void RemoveFavoriteAt(size_t index) {
    if (index < g_favorites.size()) {
        g_favorites.erase(g_favorites.begin() + index);
        SaveFavorites();
    }
}

void SetFavoriteKey(size_t index, wchar_t key) {
    if (index >= g_favorites.size()) {
        return;
    }
    key = key ? NormalizeKey(key) : 0;
    if (key) {
        for (FavoriteEntry& f : g_favorites) {
            if (f.key == key) {
                f.key = 0;
            }
        }
    }
    g_favorites[index].key = key;
    SaveFavorites();
}

bool UpdateFavorite(size_t index, const std::wstring& name, wchar_t key, const std::wstring& path) {
    if (index >= g_favorites.size() || name.empty() || path.empty() || NameInUse(name, index)) {
        return false;
    }
    g_favorites[index].name = name;
    g_favorites[index].path = path;
    SetFavoriteKey(index, key);  // 内部で保存する
    return true;
}

const std::vector<FavoriteEntry>& GetFavorites() {
    return g_favorites;
}

int FindFavoriteByKey(wchar_t key) {
    key = NormalizeKey(key);
    for (size_t i = 0; i < g_favorites.size(); ++i) {
        if (g_favorites[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const FavoriteEntry* FindFavoriteByName(const std::wstring& name) {
    for (const FavoriteEntry& f : g_favorites) {
        if (EqualNoCase(f.name, name)) {
            return &f;
        }
    }
    return nullptr;
}

std::wstring ExpandAliasPath(const std::wstring& input, bool* expanded) {
    if (expanded) *expanded = false;
    size_t sep = input.find_first_of(L"\\/");
    std::wstring head = sep == std::wstring::npos ? input : input.substr(0, sep);
    if (head.empty() || head.find(L':') != std::wstring::npos) {
        return input;  // ドライブ指定・UNC・絶対パスはエイリアスとして扱わない
    }
    const FavoriteEntry* f = FindFavoriteByName(head);
    if (!f) {
        return input;
    }
    if (expanded) *expanded = true;
    std::wstring base = f->path;
    if (sep == std::wstring::npos) {
        return base;
    }
    if (!base.empty() && base.back() == L'\\') {
        base.pop_back();
    }
    return base + L"\\" + input.substr(sep + 1);
}

bool LoadFavorites() {
    std::wstring path = GetFavoritesFilePath();
    std::wstring text;
    if (path.empty() || !ReadUtf16File(path, text)) {
        return false;
    }
    JsonValue root;
    if (!ParseJson(text, root) || root.type != JsonType::Array) {
        return false;
    }
    std::vector<FavoriteEntry> loaded;
    for (const JsonValue& item : root.arrayValue) {
        FavoriteEntry f;
        if (const JsonValue* v = item.Get(L"name")) f.name = v->AsString();
        if (const JsonValue* v = item.Get(L"path")) f.path = v->AsString();
        if (const JsonValue* v = item.Get(L"key")) {
            std::wstring k = v->AsString();
            f.key = k.empty() ? 0 : NormalizeKey(k[0]);
        }
        if (f.name.empty() || f.path.empty()) {
            continue;
        }
        bool duplicate = false;  // 名前の重複 (手編集のミス) は後の項目を読み飛ばす
        for (const FavoriteEntry& e : loaded) {
            duplicate |= EqualNoCase(e.name, f.name);
        }
        if (!duplicate) {
            loaded.push_back(std::move(f));
        }
    }
    g_favorites = std::move(loaded);
    SortFavorites();
    GetWriteTime(path, g_knownWriteTime);
    return true;
}

bool ReloadFavoritesIfChanged() {
    std::wstring path = GetFavoritesFilePath();
    FILETIME now{};
    if (path.empty() || !GetWriteTime(path, now) || CompareFileTime(&now, &g_knownWriteTime) == 0) {
        return false;
    }
    return LoadFavorites();
}
