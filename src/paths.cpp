#include "paths.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <vector>

namespace {

// %APPDATA% 配下の設定置き場。
std::wstring GetAppDataDirectory(const wchar_t* name) {
    PWSTR appDataPath = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath))) {
        dir = appDataPath;
        dir += L"\\";
        dir += name;
    }
    CoTaskMemFree(appDataPath);
    return dir;
}

// exe と同じフォルダ。書き込めない場所 (Program Files 等) にあるときは空文字列。
std::wstring GetExeDirectoryIfWritable() {
    wchar_t exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring dir = exePath;
    size_t pos = dir.find_last_of(L'\\');
    if (pos == std::wstring::npos) return L"";
    dir.resize(pos);

    std::wstring probe = dir + L"\\.film_write_test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    CloseHandle(h);
    return dir;
}

}  // namespace

std::wstring GetConfigDirectory() {
    // 環境変数 FILM_CONFIG_DIR があればそちらを使う (ポータブル運用・テスト用)。
    wchar_t overrideDir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"FILM_CONFIG_DIR", overrideDir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        return overrideDir;
    }

    // 既定は exe と同じフォルダ。書き込めないときだけ %APPDATA%\filM にする。
    static const std::wstring dir = [] {
        std::wstring exeDir = GetExeDirectoryIfWritable();
        return exeDir.empty() ? GetAppDataDirectory(L"filM") : exeDir;
    }();
    return dir;
}

std::wstring GetConfigFilePath(const wchar_t* fileName) {
    std::wstring dir = GetConfigDirectory();
    if (dir.empty()) {
        return L"";
    }
    CreateDirectoryW(dir.c_str(), nullptr);  // 既に存在していてもエラーにしない
    std::wstring path = dir + L"\\" + fileName;

    // 旧版 (%APPDATA%) に設定があり、新しい置き場にまだ無ければ一度だけ引き継ぐ。
    bool overridden = GetEnvironmentVariableW(L"FILM_CONFIG_DIR", nullptr, 0) > 0;
    if (!overridden && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // 旧名 (SpringMClone) と %APPDATA%\filM の順に探す。
        for (const wchar_t* old : {L"SpringMClone", L"filM"}) {
            std::wstring legacy = GetAppDataDirectory(old);
            if (!legacy.empty() && _wcsicmp(legacy.c_str(), dir.c_str()) != 0 &&
                CopyFileW((legacy + L"\\" + fileName).c_str(), path.c_str(), TRUE)) {
                break;
            }
        }
    }
    return path;
}

std::wstring ResolvePath(const std::wstring& base, const std::wstring& pathIn) {
    std::wstring path = pathIn;
    if (!path.empty() && path[0] == L'~' && (path.size() == 1 || path[1] == L'\\' || path[1] == L'/')) {
        wchar_t profile[MAX_PATH];
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) {
            path = std::wstring(profile) + path.substr(1);
        }
    }
    std::replace(path.begin(), path.end(), L'/', L'\\');

    std::wstring full;
    bool isUnc = path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
    bool hasDrive = path.size() >= 2 && path[1] == L':';
    if (isUnc || (hasDrive && path.size() >= 3 && path[2] == L'\\')) {
        full = path;
    } else if (hasDrive) {
        full = path.substr(0, 2) + L"\\" + path.substr(2);  // "D:foo" をルート基準として扱う
    } else if (!path.empty() && path[0] == L'\\') {
        full = (base.size() >= 2 && base[1] == L':' ? base.substr(0, 2) : std::wstring()) + path;
    } else {
        full = base;
        if (!full.empty() && full.back() != L'\\') full += L'\\';
        full += path;
    }

    // "." ".." を取り除いて正規化する。
    std::wstring prefix;
    size_t pos = 0;
    if (full.size() >= 2 && full[0] == L'\\' && full[1] == L'\\') {
        prefix = L"\\\\";
        pos = 2;
    } else if (full.size() >= 2 && full[1] == L':') {
        prefix = full.substr(0, 2);
        pos = 2;
    }
    std::vector<std::wstring> parts;
    while (pos <= full.size()) {
        size_t next = full.find(L'\\', pos);
        if (next == std::wstring::npos) next = full.size();
        std::wstring comp = full.substr(pos, next - pos);
        if (comp == L"..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!comp.empty() && comp != L".") {
            parts.push_back(comp);
        }
        pos = next + 1;
    }
    std::wstring result = prefix;
    if (prefix != L"\\\\") result += L"\\";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) result += L"\\";
        result += parts[i];
    }
    return result;
}

std::wstring GetRootOfPath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') {
        return path.substr(0, 2) + L"\\";
    }
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        // UNC: サーバー名と共有名までをルートとする。
        size_t server = path.find(L'\\', 2);
        if (server == std::wstring::npos) return path;
        size_t share = path.find(L'\\', server + 1);
        return share == std::wstring::npos ? path + L"\\" : path.substr(0, share + 1);
    }
    return path;
}
