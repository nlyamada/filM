#include "shell_actions.h"
#include "i18n.h"
#include "settings.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <utility>

namespace {

std::wstring ToLower(const std::wstring& s) {
    std::wstring r = s;
    for (wchar_t& c : r) c = static_cast<wchar_t>(towlower(c));
    return r;
}

// コマンドラインの引数を引用符で囲む。末尾の "\" は閉じ引用符を打ち消してしまうので二重にする (C:\ など)。
std::wstring Quote(const std::wstring& s) {
    size_t trailing = 0;
    while (trailing < s.size() && s[s.size() - 1 - trailing] == L'\\') ++trailing;
    return L"\"" + s + std::wstring(trailing, L'\\') + L"\"";
}

// System32 など OS のフォルダにあるプログラムのフルパス (検索パスやカレントフォルダの偽物を起動しないため)。
std::wstring SystemProgram(const wchar_t* relative) {
    wchar_t dir[MAX_PATH];
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return relative;
    return std::wstring(dir) + L"\\" + relative;
}

bool Succeeded(HINSTANCE r) {
    return reinterpret_cast<INT_PTR>(r) > 32;
}

std::wstring ParentOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L'\\');
    if (pos == std::wstring::npos) return L"";
    if (pos == 2 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, pos);
}

// Windows 標準の tar.exe のフルパス (無ければ空)。
std::wstring FindTarExe() {
    wchar_t sysDir[MAX_PATH];
    UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring path = std::wstring(sysDir) + L"\\tar.exe";
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ? path : L"";
}

// コマンドを非表示で実行し、終了コードと標準出力+標準エラーを返す。
bool RunHidden(const std::wstring& commandLine, const std::wstring& workDir, DWORD& exitCode,
               std::wstring& output) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) return false;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0,
                             nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = commandLine;
    BOOL created = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                  workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);
    CloseHandle(writeEnd);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!created) {
        CloseHandle(readEnd);
        return false;
    }

    std::string raw;
    char buf[1024];
    DWORD got = 0;
    while (ReadFile(readEnd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        if (raw.size() < 8192) raw.append(buf, got);
    }
    CloseHandle(readEnd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // tar は ANSI コードページ (日本語環境では CP932) で出力する。
    int len = MultiByteToWideChar(CP_ACP, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    output.assign(len, L'\0');
    if (len > 0) {
        MultiByteToWideChar(CP_ACP, 0, raw.data(), static_cast<int>(raw.size()), output.data(), len);
    }
    return true;
}

DWORD WINAPI ArchiveThread(LPVOID param) {
    std::unique_ptr<std::pair<HWND, std::pair<UINT, ArchiveRequest>>> job(
        static_cast<std::pair<HWND, std::pair<UINT, ArchiveRequest>>*>(param));
    const ArchiveRequest& req = job->second.second;

    auto* result = new ArchiveResult();
    result->extract = req.extract;
    result->intoFolder = req.intoFolder;
    result->output = req.extract ? req.destDir : req.zipPath;

    std::wstring tar = FindTarExe();
    if (tar.empty()) {
        result->message = Tr(L"tar.exe が見つかりません (Windows 10 1803 以降が必要です)。");
    } else {
        std::wstring cmd;
        std::wstring workDir;
        if (req.extract) {
            cmd = Quote(tar) + L" -x -f " + Quote(req.zipPath) + L" -C " + Quote(req.destDir);
            workDir = req.destDir;
        } else {
            cmd = Quote(tar) + L" -a -c -f " + Quote(req.zipPath) + L" -C " + Quote(req.workDir) + L" --";  // "--" 以降は名前 ("-" で始まる名前も安全)
            for (const std::wstring& n : req.names) {
                // "@" で始まる名前は tar が「別のアーカイブの中身を取り込む指定」と解釈してしまうので、".\" を付けて普通の名前にする。
                cmd += L" " + Quote(!n.empty() && n[0] == L'@' ? L".\\" + n : n);
            }
            workDir = req.workDir;
        }
        DWORD code = 1;
        std::wstring out;
        if (!RunHidden(cmd, workDir, code, out)) {
            result->message = Tr(L"tar.exe を起動できませんでした。");
        } else if (code != 0) {
            result->message = out.empty() ? Tr(L"tar.exe がエラーで終了しました。") : out;
        } else {
            result->ok = true;
        }
    }
    PostMessageW(job->first, job->second.first, 0, reinterpret_cast<LPARAM>(result));
    return 0;
}

// paths (同じフォルダ内) に対してコンテキストメニューの動詞を実行する。
bool InvokeShellVerb(HWND hwnd, const std::vector<std::wstring>& paths, const char* verb) {
    if (paths.empty()) return false;
    std::vector<PIDLIST_ABSOLUTE> pidls;
    std::vector<PCUITEMID_CHILD> children;
    IShellFolder* parent = nullptr;
    for (const std::wstring& p : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) continue;
        pidls.push_back(pidl);
        if (!parent) {
            PCUITEMID_CHILD child = nullptr;
            if (FAILED(SHBindToParent(pidl, IID_PPV_ARGS(&parent), &child))) {
                parent = nullptr;
                continue;
            }
            children.push_back(child);
        } else {
            children.push_back(ILFindLastID(pidl));
        }
    }

    bool ok = false;
    if (parent && !children.empty()) {
        IContextMenu* menu = nullptr;
        if (SUCCEEDED(parent->GetUIObjectOf(hwnd, static_cast<UINT>(children.size()), children.data(),
                                            IID_IContextMenu, nullptr, reinterpret_cast<void**>(&menu))) &&
            menu) {
            HMENU hMenu = CreatePopupMenu();
            menu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL);
            CMINVOKECOMMANDINFO info{};
            info.cbSize = sizeof(info);
            info.hwnd = hwnd;
            info.lpVerb = verb;
            info.nShow = SW_SHOWNORMAL;
            ok = SUCCEEDED(menu->InvokeCommand(&info));
            DestroyMenu(hMenu);
            menu->Release();
        }
    }
    if (parent) parent->Release();
    for (PIDLIST_ABSOLUTE pidl : pidls) CoTaskMemFree(pidl);
    return ok;
}

}  // namespace

bool OpenWithAssociation(HWND hwnd, const std::wstring& path, const std::wstring& workDir) {
    return Succeeded(ShellExecuteW(hwnd, L"open", path.c_str(), nullptr,
                                   workDir.empty() ? nullptr : workDir.c_str(), SW_SHOWNORMAL));
}

bool OpenWithChooser(HWND hwnd, const std::wstring& path) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_DEFAULT;
    sei.hwnd = hwnd;
    sei.lpVerb = L"openas";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

bool ShowProperties(HWND hwnd, const std::vector<std::wstring>& paths) {
    return InvokeShellVerb(hwnd, paths, "properties");
}

bool LaunchTerminal(const std::wstring& dir, bool powershell) {
    // フルパスで起動する (カレントフォルダ・exe のフォルダに置かれた同名の偽物を実行しないため)。
    std::wstring cmd = powershell ? Quote(SystemProgram(L"WindowsPowerShell\\v1.0\\powershell.exe")) + L" -NoLogo"
                                  : Quote(SystemProgram(L"cmd.exe"));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // 新しいコンソールで起動する (Windows 11 の既定のターミナル設定に従って表示される)。
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, dir.c_str(),
                        &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool IsBinaryFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned char buf[8192];
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf), &got, nullptr);
    CloseHandle(h);
    if (!ok || got == 0) return false;
    if (got >= 2 && ((buf[0] == 0xFF && buf[1] == 0xFE) || (buf[0] == 0xFE && buf[1] == 0xFF))) {
        return false;  // UTF-16 は NUL を含むがテキスト
    }
    return std::find(buf, buf + got, static_cast<unsigned char>(0)) != buf + got;
}

EditResult EditFile(HWND hwnd, const std::wstring& path, const std::wstring& workDir, bool forceText) {
    if (!forceText && IsBinaryFile(path)) {
        return EditResult::Binary;
    }
    const wchar_t* dir = workDir.empty() ? nullptr : workDir.c_str();
    std::wstring quoted = Quote(path);
    wchar_t editor[MAX_PATH] = L"";
    // 設定 (settings.json) のエディタ → 環境変数 FILM_EDITOR → 関連付けの edit → メモ帳 の順。
    const std::wstring& configured = GetSettings().editor;
    if (configured == L"notepad") {
        wcscpy_s(editor, SystemProgram(L"notepad.exe").c_str());
    } else if (!configured.empty()) {
        wcsncpy_s(editor, configured.c_str(), MAX_PATH - 1);
    } else {
        GetEnvironmentVariableW(L"FILM_EDITOR", editor, MAX_PATH);
    }
    HINSTANCE r;
    if (editor[0]) {
        r = ShellExecuteW(hwnd, L"open", editor, quoted.c_str(), dir, SW_SHOWNORMAL);
    } else {
        r = ShellExecuteW(hwnd, L"edit", path.c_str(), nullptr, dir, SW_SHOWNORMAL);
        if (!Succeeded(r)) {
            r = ShellExecuteW(hwnd, L"open", SystemProgram(L"notepad.exe").c_str(), quoted.c_str(), dir, SW_SHOWNORMAL);
        }
    }
    return Succeeded(r) ? EditResult::Started : EditResult::Failed;
}

bool IsImageFile(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = ToLower(path.substr(dot + 1));
    static const wchar_t* kExts[] = {L"jpg", L"jpeg", L"jfif", L"png", L"gif", L"bmp", L"tif", L"tiff",
                                     L"webp", L"heic", L"heif", L"avif", L"ico", L"jxr", L"dng"};
    for (const wchar_t* e : kExts) {
        if (ext == e) return true;
    }
    return false;
}

bool CreateShortcutFile(const std::wstring& target, const std::wstring& linkPath) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        return false;
    }
    bool ok = false;
    link->SetPath(target.c_str());
    DWORD attrs = GetFileAttributesW(target.c_str());
    std::wstring workDir = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
                               ? target : ParentOf(target);
    if (!workDir.empty()) link->SetWorkingDirectory(workDir.c_str());
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        ok = SUCCEEDED(file->Save(linkPath.c_str(), TRUE));
        file->Release();
    }
    link->Release();
    return ok;
}

std::wstring MakeUniquePath(const std::wstring& desired, bool isDirectory) {
    if (GetFileAttributesW(desired.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return desired;
    }
    size_t sep = desired.find_last_of(L'\\');
    size_t dot = desired.find_last_of(L'.');
    bool hasExt = !isDirectory && dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep + 1);
    std::wstring stem = hasExt ? desired.substr(0, dot) : desired;
    std::wstring ext = hasExt ? desired.substr(dot) : L"";
    for (int i = 2; i < 10000; ++i) {
        std::wstring candidate = stem + L" (" + std::to_wstring(i) + L")" + ext;
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return candidate;
        }
    }
    return desired;
}

void StartArchiveJob(HWND notify, UINT msg, const ArchiveRequest& request) {
    auto* job = new std::pair<HWND, std::pair<UINT, ArchiveRequest>>(notify, {msg, request});
    HANDLE thread = CreateThread(nullptr, 0, ArchiveThread, job, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        delete job;
        auto* result = new ArchiveResult();
        result->extract = request.extract;
        result->message = Tr(L"処理を開始できませんでした。");
        PostMessageW(notify, msg, 0, reinterpret_cast<LPARAM>(result));
    }
}


