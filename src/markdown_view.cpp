#include "markdown_view.h"

#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>

#include "WebView2.h"
#include "i18n.h"
#include "paths.h"
#include "text_codec.h"
#include "theme.h"
#include "webview2_iids.h"

namespace {

constexpr wchar_t kHostClass[] = L"FilMMarkdownHost";
constexpr wchar_t kViewerHost[] = L"filmviewer.local";
constexpr wchar_t kDocHost[] = L"filmdoc.local";
constexpr size_t kMaxDocBytes = 16u * 1024 * 1024;

// ---- COM のコールバック (std::function を呼ぶだけの小さな実装) ----

template <class Iface, const IID* Iid, class... Args>
class Handler final : public Iface {
public:
    explicit Handler(std::function<HRESULT(Args...)> f) : fn_(std::move(f)) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (IsEqualIID(riid, *Iid) || IsEqualIID(riid, IID_IUnknown)) {
            *ppv = static_cast<Iface*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_(args...); }

private:
    std::function<HRESULT(Args...)> fn_;
    std::atomic<ULONG> ref_{1};
};

using EnvHandler = Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                           &FilmIID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>;
using ControllerHandler = Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                                  &FilmIID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>;
using NavCompletedHandler = Handler<ICoreWebView2NavigationCompletedEventHandler, &FilmIID_ICoreWebView2NavigationCompletedEventHandler,
                                    ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>;
using NavStartingHandler = Handler<ICoreWebView2NavigationStartingEventHandler, &FilmIID_ICoreWebView2NavigationStartingEventHandler,
                                   ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>;
using NewWindowHandler = Handler<ICoreWebView2NewWindowRequestedEventHandler, &FilmIID_ICoreWebView2NewWindowRequestedEventHandler,
                                 ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>;
using AccelHandler = Handler<ICoreWebView2AcceleratorKeyPressedEventHandler, &FilmIID_ICoreWebView2AcceleratorKeyPressedEventHandler,
                             ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs*>;

using Microsoft::WRL::ComPtr;

enum class Phase { Idle, Starting, Ready, Failed };

struct State {
    HWND host = nullptr;
    HWND parent = nullptr;
    std::wstring path;  // 表示したいファイル (空なら何も表示しない)
    std::wstring mappedRoot;  // filmdoc.local に割り当て済みのフォルダ (文書のあるドライブ / 共有のルート)
    bool remoteImages = false;  // インターネット上の画像も表示する (viewer-remote.html を使う)
    bool dark = false;
    Phase phase = Phase::Idle;
    std::wstring lastError;
    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    bool pageReady = false;  // viewer.html の読み込みが終わった
};

std::shared_ptr<State> g_state;

typedef HRESULT(STDAPICALLTYPE* CreateEnvironmentFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                      ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

std::wstring ExeDirectory() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir(buf, n);
    size_t pos = dir.find_last_of(L'\\');
    return pos == std::wstring::npos ? L"" : dir.substr(0, pos);
}

std::wstring DirectoryOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? L"" : path.substr(0, pos);
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

// パス (\ 区切り) を URL のパス部分にする (UTF-8 でパーセントエンコード。/ 区切りに直す)。
std::wstring UrlEncodePath(const std::wstring& path) {
    std::string utf8;
    int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
    if (n > 0) {
        utf8.assign(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), utf8.data(), n, nullptr, nullptr);
    }
    std::wstring out;
    for (unsigned char c : utf8) {
        if (c == '\\') out += L'/';
        else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') out += static_cast<wchar_t>(c);
        else {
            wchar_t buf[4];
            swprintf(buf, 4, L"%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// JavaScript の文字列リテラルにする (引用符付き)。
std::wstring JsString(const std::wstring& s) {
    std::wstring r = L"\"";
    for (wchar_t c : s) {
        switch (c) {
            case L'\\': r += L"\\\\"; break;
            case L'"': r += L"\\\""; break;
            case L'\n': r += L"\\n"; break;
            case L'\r': r += L"\\r"; break;
            case L'\t': r += L"\\t"; break;
            case 0x2028: r += L"\\u2028"; break;
            case 0x2029: r += L"\\u2029"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", static_cast<unsigned>(c));
                    r += buf;
                } else {
                    r += c;
                }
        }
    }
    return r + L"\"";
}

std::wstring PercentDecode(const std::wstring& in) {
    std::string utf8;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'%' && i + 2 < in.size() + 0 && iswxdigit(in[i + 1]) && iswxdigit(in[i + 2])) {
            utf8 += static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            int n = WideCharToMultiByte(CP_UTF8, 0, &in[i], 1, nullptr, 0, nullptr, nullptr);
            std::string tmp(static_cast<size_t>(n), '\0');
            WideCharToMultiByte(CP_UTF8, 0, &in[i], 1, tmp.data(), n, nullptr, nullptr);
            utf8 += tmp;
        }
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(std::max(len, 0)), L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

std::wstring LowerExtension(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    size_t sep = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) return L"";
    std::wstring ext = path.substr(dot + 1);
    for (wchar_t& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext;
}

// リンクから直接開かない種類 (実行ファイル・スクリプトなど)。Markdown に書かれたリンクで
// 知らないうちにプログラムが動くのを防ぐ。
bool IsRiskyLinkTarget(const std::wstring& path) {
    static const wchar_t* kRisky[] = {L"exe", L"com", L"scr", L"pif", L"bat", L"cmd", L"msi", L"msp", L"mst", L"ps1", L"psm1",
                                      L"ps1xml", L"vbs", L"vbe", L"js", L"jse", L"wsf", L"wsh", L"hta", L"cpl", L"reg", L"lnk",
                                      L"url", L"jar", L"dll", L"sys", L"scf", L"inf", L"chm", L"msc", L"gadget", L"application",
                                      L"appref-ms", L"library-ms", L"search-ms", L"settingcontent-ms", L"docm", L"xlsm", L"pptm"};
    std::wstring ext = LowerExtension(path);
    for (const wchar_t* r : kRisky) {
        if (ext == r) return true;
    }
    return false;
}

// 開いても安全とみなす種類 (画像・テキスト・PDF・音声・動画)。HTML や SVG などスクリプトを含みうるものは含めない。
bool IsPlainDocumentType(const std::wstring& path) {
    static const wchar_t* kSafe[] = {L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"webp", L"tif", L"tiff", L"ico", L"avif", L"heic",
                                     L"txt", L"log", L"csv", L"tsv", L"json", L"yaml", L"yml", L"toml", L"ini", L"pdf",
                                     L"mp3", L"wav", L"flac", L"ogg", L"m4a", L"mp4", L"mkv", L"webm", L"mov", L"avi"};
    std::wstring ext = LowerExtension(path);
    for (const wchar_t* e : kSafe) {
        if (ext == e) return true;
    }
    return false;
}

// 文書のあるドライブ (共有) のルートを仮想ホスト filmdoc.local に割り当てる。文書の画像は
// "../images/a.png" のように文書より上のフォルダにあることが多いので、文書のフォルダだけでなくルートごと割り当てて、
// base を文書のフォルダにする。画像を別オリジンの viewer.html から読み込むので ALLOW が要る。ページ内にスクリプトは
// 無く (CSP)、画像以外は読めない (img-src のみ許可、connect-src 'none') ので、読み出される心配はない。
// 同じ名前の割り当ては上書きされる (Clear してから Set し直すと最初の画像要求が止まることがあるので、Set だけ行う)。
void MapDocFolder(State* st, const std::wstring& dir) {
    if (dir.empty() || dir == st->mappedRoot) return;
    ComPtr<ICoreWebView2_3> wv3;
    if (st->webview && SUCCEEDED(st->webview->QueryInterface(FilmIID_ICoreWebView2_3, reinterpret_cast<void**>(wv3.GetAddressOf())))) {
        if (SUCCEEDED(wv3->SetVirtualHostNameToFolderMapping(kDocHost, dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))) {
            st->mappedRoot = dir;
        }
    }
}

void NotifyParent(State* st, UINT msg) {
    if (st->parent) PostMessageW(st->parent, msg, 0, 0);
}

// path を読み込んで、表示中のページに描画させる。
void RenderFile(State* st, const std::wstring& path, const std::wstring& hash = L"") {
    TextDocument doc;
    TextLoadResult r = LoadTextDocument(path, doc, kMaxDocBytes);
    if (r != TextLoadResult::Ok) {
        doc = TextDocument();
        doc.text = std::wstring(r == TextLoadResult::TooLarge ? Tr(L"ファイルが大きすぎるため表示しません。")
                                : r == TextLoadResult::Binary ? Tr(L"テキストファイルではありません。")
                                                              : Tr(L"ファイルを読み込めません。"));
    }
    st->path = path;
    std::wstring root = GetRootOfPath(path);
    MapDocFolder(st, root);
    // base: 文書のフォルダ (ルートからの相対) を URL にしたもの。画像の相対パスはここを基準に読まれる。
    std::wstring relDir = DirectoryOf(path);
    relDir = relDir.size() > root.size() ? relDir.substr(root.size()) : std::wstring();
    std::wstring base = std::wstring(L"https://") + kDocHost + L"/" + UrlEncodePath(relDir) + (relDir.empty() ? L"" : L"/");
    std::wstring script = L"render(" + JsString(doc.text) + L", {dark:" + (st->dark ? L"true" : L"false") + L", base:" +
                          JsString(base) + L", title:" + JsString(FileNameOf(path)) +
                          L", hash:" + JsString(hash) + L"});";
    st->webview->ExecuteScript(script.c_str(), nullptr);
}

void OpenExternal(HWND hwnd, const std::wstring& target) {
    ShellExecuteW(hwnd, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool StartsWithNoCase(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() && _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

void ResizeWebView(State* st) {
    if (!st->controller) return;
    RECT rc;
    GetClientRect(st->host, &rc);
    st->controller->put_Bounds(rc);
}

// 文書内の (相対) リンクの行き先を、実際のファイルのパスにする。
// relative は filmdoc.local のルート (= ドライブのルート) からのパス。
std::wstring ResolveLinkedFile(const std::wstring& root, const std::wstring& relative) {
    std::wstring file = root + PercentDecode(relative);
    for (wchar_t& c : file) {
        if (c == L'/') c = L'\\';
    }
    wchar_t full[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(file.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
    return (n > 0 && n < std::size(full)) ? std::wstring(full, n) : std::wstring();
}

void HandleDocLink(const std::shared_ptr<State>& s, const std::wstring& uri, const std::wstring& docPrefix) {
    std::wstring rest = uri.substr(docPrefix.size());
    std::wstring hash;
    size_t h = rest.find(L'#');
    if (h != std::wstring::npos) {
        hash = rest.substr(h + 1);
        rest.erase(h);
    }
    size_t q = rest.find(L'?');
    if (q != std::wstring::npos) rest.erase(q);
    std::wstring file = ResolveLinkedFile(GetRootOfPath(s->path), rest);
    // ページ内リンク (#見出し): base が文書のフォルダなので、フォルダ自身か文書自身を指す。
    bool selfLink = rest.empty() || rest.back() == L'/' || _wcsicmp(file.c_str(), s->path.c_str()) == 0;
    if (selfLink && !hash.empty()) {
        std::wstring script = L"(function(){var e=document.getElementById(decodeURIComponent(" + JsString(hash) + L"));if(e)e.scrollIntoView();})();";
        s->webview->ExecuteScript(script.c_str(), nullptr);
        return;
    }
    if (selfLink) return;
    // ドライブ名・代替データストリーム (a.txt:x) になりうる ':' を含むものは開かない。
    if (file.empty() || file.find(L':', 2) != std::wstring::npos || StartsWithNoCase(file, L"\\\\")) {
        ThemedMessageBox(s->host, Tr(L"このリンク先は開けません。"), L"filM", MB_OK | MB_ICONWARNING);
        return;
    }
    if (IsMarkdownFile(file)) {
        if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
            ThemedMessageBox(s->host, Fmt(L"ファイルが見つかりません:\n{0}", {file}).c_str(), L"filM", MB_OK | MB_ICONWARNING);
            return;
        }
        RenderFile(s.get(), file, hash);
        NotifyParent(s.get(), kMsgMarkdownViewInfo);
        return;
    }
    if (IsRiskyLinkTarget(file)) {
        ThemedMessageBox(s->host, Fmt(L"安全のため、この種類のファイルはリンクから開けません:\n{0}", {file}).c_str(), L"filM", MB_OK | MB_ICONWARNING);
        return;
    }
    // 画像・文書・音声や動画など、開いても安全な種類はそのまま開く。それ以外は確認する。
    if (!IsPlainDocumentType(file)) {
        if (ThemedMessageBox(s->host, Fmt(L"このファイルを関連付けられたアプリで開きますか?\n{0}", {file}).c_str(), L"filM", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    }
    OpenExternal(s->host, file);
}

void OnWebViewReady(const std::shared_ptr<State>& st) {
    st->controller->get_CoreWebView2(&st->webview);
    ResizeWebView(st.get());
    st->controller->put_IsVisible(TRUE);

    ComPtr<ICoreWebView2Controller2> c2;
    if (SUCCEEDED(st->controller->QueryInterface(FilmIID_ICoreWebView2Controller2, reinterpret_cast<void**>(c2.GetAddressOf())))) {
        COREWEBVIEW2_COLOR color = st->dark ? COREWEBVIEW2_COLOR{255, 30, 30, 30} : COREWEBVIEW2_COLOR{255, 255, 255, 255};
        c2->put_DefaultBackgroundColor(color);  // 読み込み中に白く光らないように
    }

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(st->webview->get_Settings(&settings))) {
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->put_IsWebMessageEnabled(FALSE);      // ページからアプリへのメッセージは使わない
        settings->put_AreHostObjectsAllowed(FALSE);    // アプリのオブジェクトをページへ公開しない
    }

    ComPtr<ICoreWebView2_3> wv3;
    if (SUCCEEDED(st->webview->QueryInterface(FilmIID_ICoreWebView2_3, reinterpret_cast<void**>(wv3.GetAddressOf())))) {
        std::wstring viewerDir = ExeDirectory() + L"\\viewer";
        // viewer\ は読み取り専用の資源。CORS は拒否 (文書側から fetch できない)。
        // viewer\ は読み取り専用の資源。他のオリジンからの CORS 要求は拒否する。
        wv3->SetVirtualHostNameToFolderMapping(kViewerHost, viewerDir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    }
    MapDocFolder(st.get(), GetRootOfPath(st->path));

    std::weak_ptr<State> weak = st;
    EventRegistrationToken token;

    auto* completed = new NavCompletedHandler([weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
        auto s = weak.lock();
        if (!s) return S_OK;
        BOOL success = FALSE;
        args->get_IsSuccess(&success);
        if (success && !s->pageReady) {
            s->pageReady = true;
            s->phase = Phase::Ready;
            if (!s->path.empty()) RenderFile(s.get(), s->path);
        }
        return S_OK;
    });
    st->webview->add_NavigationCompleted(completed, &token);
    completed->Release();

    auto* starting = new NavStartingHandler([weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
        auto s = weak.lock();
        if (!s) return S_OK;
        LPWSTR raw = nullptr;
        if (FAILED(args->get_Uri(&raw)) || !raw) return S_OK;
        std::wstring uri = raw;
        CoTaskMemFree(raw);

        std::wstring viewerPrefix = std::wstring(L"https://") + kViewerHost + L"/";
        std::wstring docPrefix = std::wstring(L"https://") + kDocHost + L"/";
        if (StartsWithNoCase(uri, viewerPrefix)) return S_OK;  // viewer.html 自身の読み込み
        args->put_Cancel(TRUE);
        // 利用者がクリックしたリンクだけ扱う (文書の自動リダイレクトなどでブラウザやファイルが勝手に開かないように)。
        BOOL userInitiated = FALSE;
        args->get_IsUserInitiated(&userInitiated);
        if (!userInitiated) return S_OK;
        if (StartsWithNoCase(uri, docPrefix)) {
            HandleDocLink(s, uri, docPrefix);
            return S_OK;
        }
        if (StartsWithNoCase(uri, L"http://") || StartsWithNoCase(uri, L"https://") || StartsWithNoCase(uri, L"mailto:")) {
            OpenExternal(s->host, uri);
        }
        return S_OK;
    });
    st->webview->add_NavigationStarting(starting, &token);
    starting->Release();

    auto* newWindow = new NewWindowHandler([weak](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
        auto s = weak.lock();
        args->put_Handled(TRUE);
        BOOL userInitiated = FALSE;
        args->get_IsUserInitiated(&userInitiated);
        LPWSTR raw = nullptr;
        if (s && userInitiated && SUCCEEDED(args->get_Uri(&raw)) && raw) {
            std::wstring uri = raw;
            if (StartsWithNoCase(uri, L"http://") || StartsWithNoCase(uri, L"https://")) OpenExternal(s->host, uri);
        }
        if (raw) CoTaskMemFree(raw);
        return S_OK;
    });
    st->webview->add_NewWindowRequested(newWindow, &token);
    newWindow->Release();

    auto* accel = new AccelHandler([weak](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
        auto s = weak.lock();
        if (!s) return S_OK;
        COREWEBVIEW2_KEY_EVENT_KIND kind;
        UINT vk = 0;
        args->get_KeyEventKind(&kind);
        args->get_VirtualKey(&vk);
        if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN) return S_OK;
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (ctrl || alt) return S_OK;
        if (vk == VK_ESCAPE || vk == VK_TAB || vk == 'E') {  // 一覧へ戻る / ソースを編集 (親に任せる)
            args->put_Handled(TRUE);
            PostMessageW(s->parent, kMsgMarkdownViewKey, vk, 0);
        } else if (vk == VK_F5) {
            args->put_Handled(TRUE);
            if (!s->path.empty()) RenderFile(s.get(), s->path);
        }
        return S_OK;
    });
    st->controller->add_AcceleratorKeyPressed(accel, &token);
    accel->Release();

    st->webview->Navigate((std::wstring(L"https://") + kViewerHost + (st->remoteImages ? L"/viewer-remote.html" : L"/viewer.html") + (st->dark ? L"#dark" : L"")).c_str());
}

void Fail(const std::shared_ptr<State>& s, const std::wstring& message) {
    s->phase = Phase::Failed;
    s->lastError = message;
    NotifyParent(s.get(), kMsgMarkdownViewFailed);
}

// WebView2 の準備を始める (非同期)。すぐに分かる失敗は false と error。
bool StartWebView(const std::shared_ptr<State>& st, std::wstring& error) {
    std::wstring exeDir = ExeDirectory();
    if (GetFileAttributesW((exeDir + L"\\viewer\\viewer.html").c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = Fmt(L"viewer フォルダが見つかりません ({0})。", {exeDir + L"\\viewer"});
        st->phase = Phase::Failed;
        st->lastError = error;
        return false;
    }
    // 読み込み先は exe のフォルダに固定する (カレントディレクトリなど他の場所の DLL を拾わない)。
    HMODULE loader = LoadLibraryExW((exeDir + L"\\WebView2Loader.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!loader) {
        error = Fmt(L"WebView2Loader.dll が見つかりません ({0})。", {exeDir});
        st->phase = Phase::Failed;
        st->lastError = error;
        return false;
    }
    auto createEnv = reinterpret_cast<CreateEnvironmentFn>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")));
    if (!createEnv) {
        error = Tr(L"WebView2Loader.dll が正しくありません。");
        st->phase = Phase::Failed;
        st->lastError = error;
        return false;
    }

    // WebView2 のデータ置き場 (キャッシュ等)。%LOCALAPPDATA%\filM\WebView2
    std::wstring userData;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        userData = std::wstring(local) + L"\\filM";
        CreateDirectoryW(userData.c_str(), nullptr);
        userData += L"\\WebView2";
    }
    CoTaskMemFree(local);

    st->phase = Phase::Starting;
    std::weak_ptr<State> weak = st;
    auto* envHandler = new EnvHandler([weak](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        auto s = weak.lock();
        if (!s) return S_OK;
        if (FAILED(hr) || !env) {
            Fail(s, Tr(L"WebView2 ランタイムを開始できませんでした。\nMicrosoft Edge WebView2 Runtime をインストールしてください。"));
            return S_OK;
        }
        s->env = env;
        auto* ctrlHandler = new ControllerHandler([weak](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT {
            auto s2 = weak.lock();
            if (!s2) return S_OK;
            if (FAILED(hr2) || !controller) {
                Fail(s2, Tr(L"WebView2 を作れませんでした。"));
                return S_OK;
            }
            s2->controller = controller;
            OnWebViewReady(s2);
            return S_OK;
        });
        s->env->CreateCoreWebView2Controller(s->host, ctrlHandler);
        ctrlHandler->Release();
        return S_OK;
    });
    HRESULT hr = createEnv(nullptr, userData.empty() ? nullptr : userData.c_str(), nullptr, envHandler);
    envHandler->Release();
    if (FAILED(hr)) {
        error = Tr(L"WebView2 ランタイムが見つかりません。\nMicrosoft Edge WebView2 Runtime をインストールしてください。");
        st->phase = Phase::Failed;
        st->lastError = error;
        return false;
    }
    return true;
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(ThemeIsDark() ? RGB(30, 30, 30) : RGB(255, 255, 255));
            FillRect(reinterpret_cast<HDC>(wParam), &rc, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_SIZE:
            if (g_state) ResizeWebView(g_state.get());
            return 0;
        case WM_SETFOCUS:
            if (g_state && g_state->controller) g_state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            return 0;
        case WM_DESTROY:
            if (g_state) {
                if (g_state->controller) g_state->controller->Close();
                g_state->webview.Reset();
                g_state->controller.Reset();
                g_state->env.Reset();
                g_state.reset();
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

bool IsMarkdownFile(const std::wstring& path) {
    std::wstring ext = LowerExtension(path);
    return ext == L"md" || ext == L"markdown" || ext == L"mdown" || ext == L"mkd";
}

HWND MarkdownViewCreate(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HostProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kHostClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    HWND host = CreateWindowExW(0, kHostClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!host) return nullptr;
    g_state = std::make_shared<State>();
    g_state->host = host;
    g_state->parent = parent;
    return host;
}

bool MarkdownViewShow(HWND host, const std::wstring& path, bool dark, bool remoteImages, std::wstring& error) {
    auto st = g_state;
    if (!st || st->host != host) return false;
    st->path = path;
    if (st->remoteImages != remoteImages) {  // 画像の許可を切り替えた: 読み込む HTML (CSP) を替えるので、ページごと読み直す
        st->remoteImages = remoteImages;
        if (st->phase == Phase::Ready && st->webview) {
            st->pageReady = false;
            st->webview->Navigate((std::wstring(L"https://") + kViewerHost + (remoteImages ? L"/viewer-remote.html" : L"/viewer.html") + (dark ? L"#dark" : L"")).c_str());
        }
    }
    bool themeChanged = st->dark != dark;
    st->dark = dark;
    switch (st->phase) {
        case Phase::Ready:
            if (themeChanged && st->controller) {
                ComPtr<ICoreWebView2Controller2> c2;
                if (SUCCEEDED(st->controller->QueryInterface(FilmIID_ICoreWebView2Controller2, reinterpret_cast<void**>(c2.GetAddressOf())))) {
                    c2->put_DefaultBackgroundColor(dark ? COREWEBVIEW2_COLOR{255, 30, 30, 30} : COREWEBVIEW2_COLOR{255, 255, 255, 255});
                }
            }
            if (st->pageReady) RenderFile(st.get(), path);  // ページを読み直している間は、読み込み完了時に表示される
            return true;
        case Phase::Starting:
            return true;  // 準備が済み次第 (NavigationCompleted で) 表示される
        case Phase::Failed:
            error = st->lastError;
            return false;
        case Phase::Idle:
            break;
    }
    return StartWebView(st, error);
}

std::wstring MarkdownViewCurrentPath() {
    return g_state ? g_state->path : std::wstring();
}

std::wstring MarkdownViewLastError() {
    return g_state ? g_state->lastError : std::wstring();
}

void MarkdownViewFocus(HWND host) {
    if (g_state && g_state->controller) g_state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    else SetFocus(host);
}

void MarkdownViewScroll(int notches) {
    if (!g_state || !g_state->webview || !g_state->pageReady) return;
    std::wstring script = L"window.scrollBy(0," + std::to_wstring(-notches * 90) + L");";
    g_state->webview->ExecuteScript(script.c_str(), nullptr);
}

void MarkdownViewClear() {
    if (g_state) g_state->path.clear();
}
