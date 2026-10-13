#include "completion.h"

#include <windows.h>

#include <algorithm>

#include "favorites.h"
#include "paths.h"

namespace {

bool StartsWithNoCase(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() &&
           CompareStringOrdinal(s.c_str(), static_cast<int>(prefix.size()),
                                 prefix.c_str(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

bool LessNoCase(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) < 0;
}

bool EqualNoCase(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

}  // namespace

CompletionResult ComputeFolderCompletions(const std::wstring& text, size_t caret, const std::wstring& baseDir) {
    CompletionResult result;
    if (caret > text.size()) caret = text.size();
    result.replaceStart = 0;
    result.replaceEnd = caret;

    std::wstring token;
    for (size_t i = 0; i < caret; ++i) {
        if (text[i] != L'"') token += (text[i] == L'/' ? L'\\' : text[i]);
    }

    size_t sep = token.find_last_of(L'\\');
    std::wstring dirPart = sep == std::wstring::npos ? L"" : token.substr(0, sep + 1);
    std::wstring namePrefix = sep == std::wstring::npos ? token : token.substr(sep + 1);

    std::vector<std::wstring> aliases;
    if (dirPart.empty()) {
        // "C:" のようなドライブ指定は "C:\" に補う。
        if (token.size() == 2 && token[1] == L':') {
            result.candidates.push_back(token + L"\\");
            return result;
        }
        for (const FavoriteEntry& f : GetFavorites()) {
            if (StartsWithNoCase(f.name, namePrefix)) aliases.push_back(f.name + L"\\");
        }
    }

    // 検索するフォルダ: 先頭がエイリアス名ならそのフォルダに読み替える。
    std::wstring searchDir = baseDir;
    if (!dirPart.empty()) {
        searchDir = ResolvePath(baseDir, ExpandAliasPath(dirPart));
    }
    std::wstring pattern = searchDir;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    std::vector<std::wstring> dirs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (!StartsWithNoCase(name, namePrefix)) continue;
            dirs.push_back(dirPart + name + L"\\");
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    std::sort(aliases.begin(), aliases.end(), LessNoCase);
    std::sort(dirs.begin(), dirs.end(), LessNoCase);
    for (const std::wstring& a : aliases) result.candidates.push_back(a);
    for (const std::wstring& d : dirs) {
        bool dup = false;  // エイリアス名と同名のフォルダは1つにまとめる
        for (const std::wstring& a : aliases) dup |= EqualNoCase(a, d);
        if (!dup) result.candidates.push_back(d);
    }
    return result;
}
