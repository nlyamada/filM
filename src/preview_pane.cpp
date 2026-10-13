#include "preview_pane.h"

#ifndef _RICHEDIT_VER
#define _RICHEDIT_VER 0x0500
#endif
#include <commctrl.h>
#include <richedit.h>
#include <uxtheme.h>

#include <algorithm>

#include "i18n.h"
#include "image_view.h"
#include "markdown_view.h"
#include "settings.h"
#include "shell_actions.h"
#include "text_codec.h"
#include "theme.h"

namespace {

constexpr wchar_t kPaneClass[] = L"FilMPreviewPane";
constexpr int kHeaderHeight = 24;                    // 96dpi 時の px
constexpr size_t kMaxTextBytes = 10u * 1024 * 1024;  // これを超えるテキストは開かない
constexpr UINT_PTR kRichSubclassId = 7;

enum class Content { Message, Image, Markdown, Text };

struct Pane {
    HWND hwnd = nullptr;
    HWND image = nullptr;
    HWND rich = nullptr;
    HWND md = nullptr;
    HFONT headerFont = nullptr;

    Content content = Content::Message;
    std::wstring message;  // Content::Message のときの案内 (日本語のキー。表示時に Tr する)
    std::wstring path;     // いま表示しているファイル
    TextDocument doc;      // 表示中のテキストの文字コード・改行の情報
    bool wrap = true;
    bool mdFallback = false;  // Markdown を整形表示できず、文字のまま出している

    // 直前に読み込んだファイル (一覧の更新などで同じものを何度も読み直さないため)
    bool loadedValid = false;
    std::wstring loadedPath;
    FILETIME loadedWrite{};
    ULONGLONG loadedSize = 0;
};

Pane* g_pane = nullptr;

int Scale(int px) {
    return MulDiv(px, static_cast<int>(GetDpiForWindow(g_pane->hwnd)), 96);
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

bool FileStamp(const std::wstring& path, FILETIME& write, ULONGLONG& size) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    write = data.ftLastWriteTime;
    size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

RECT HeaderRect() {
    RECT rc;
    GetClientRect(g_pane->hwnd, &rc);
    rc.bottom = std::min<LONG>(rc.bottom, Scale(kHeaderHeight));
    return rc;
}

void UpdateHeader() {
    RECT rc = HeaderRect();
    InvalidateRect(g_pane->hwnd, &rc, FALSE);
}

// ビューワーを閉じて一覧へ戻る (Esc)。
void RequestClose() {
    PostMessageW(GetParent(g_pane->hwnd), kMsgPreviewClose, 0, 0);
}

// 一覧へフォーカスだけ戻す (Tab)。
void RequestReturn() {
    PostMessageW(GetParent(g_pane->hwnd), kMsgPreviewReturn, 0, 0);
}

// ---- 文字の表示 (RichEdit。読み取り専用) ----

void ApplyRichFormat() {
    if (!g_pane->rich) return;
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
    cf.dwEffects = 0;
    cf.yHeight = 200;  // 10pt (twips)
    cf.crTextColor = ThemeText();
    wcscpy_s(cf.szFaceName, L"Consolas");
    SendMessageW(g_pane->rich, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(g_pane->rich, EM_SETBKGNDCOLOR, 0, ThemeWindowBg());
    SetWindowTheme(g_pane->rich, ThemeIsDark() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

void ApplyWrap() {
    // 0 = ウィンドウの幅で折り返す / 1 = 折り返さない (横スクロール)
    SendMessageW(g_pane->rich, EM_SETTARGETDEVICE, 0, g_pane->wrap ? 0 : 1);
}

void SetRichText(const std::wstring& lfText) {
    std::wstring t = lfText;
    for (wchar_t& c : t) {
        if (c == L'\n') c = L'\r';  // RichEdit の段落区切りは CR
    }
    SETTEXTEX st{};
    st.flags = ST_DEFAULT;
    st.codepage = 1200;  // UTF-16
    SendMessageW(g_pane->rich, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&st), reinterpret_cast<LPARAM>(t.c_str()));
    SendMessageW(g_pane->rich, EM_SETSEL, 0, 0);
    SendMessageW(g_pane->rich, EM_SCROLLCARET, 0, 0);
}

// ---- 表示する内容の切り替え ----

void ShowOnly(Content c) {
    g_pane->content = c;
    ShowWindow(g_pane->image, c == Content::Image ? SW_SHOW : SW_HIDE);
    ShowWindow(g_pane->rich, c == Content::Text ? SW_SHOW : SW_HIDE);
    ShowWindow(g_pane->md, c == Content::Markdown ? SW_SHOW : SW_HIDE);
    RECT rc;
    GetClientRect(g_pane->hwnd, &rc);
    InvalidateRect(g_pane->hwnd, &rc, FALSE);
}

void ShowMessage(const wchar_t* japaneseKey) {
    g_pane->message = japaneseKey;
    g_pane->loadedValid = false;
    ShowOnly(Content::Message);
    if (g_pane->image) ImageViewClear(g_pane->image);
    MarkdownViewClear();
}

void Layout() {
    if (!g_pane || !g_pane->hwnd) return;
    RECT rc;
    GetClientRect(g_pane->hwnd, &rc);
    int top = std::min<int>(Scale(kHeaderHeight), rc.bottom);
    for (HWND h : {g_pane->image, g_pane->rich, g_pane->md}) {
        if (h) MoveWindow(h, 0, top, rc.right, std::max<int>(0, rc.bottom - top), TRUE);
    }
    UpdateHeader();
}

// テキストとして読み込んで RichEdit に出す。読めなければ案内を出す。
void ShowTextFile(const std::wstring& path) {
    TextDocument d;
    TextLoadResult r = LoadTextDocument(path, d, kMaxTextBytes);
    const wchar_t* key = nullptr;
    switch (r) {
        case TextLoadResult::Ok: break;
        case TextLoadResult::Binary: key = L"バイナリファイルのためプレビューできません"; break;
        case TextLoadResult::TooLarge: key = L"ファイルが大きすぎるため開きません (10 MB まで)"; break;
        default: key = L"ファイルを読み込めません"; break;
    }
    g_pane->path = path;
    if (key) {
        ShowMessage(key);
        return;
    }
    g_pane->doc = std::move(d);
    SetRichText(g_pane->doc.text);
    ApplyWrap();
    ShowOnly(Content::Text);
}

void ShowFile(const std::wstring& path) {
    if (!g_pane) return;
    if (path.empty()) {
        g_pane->path.clear();
        ShowMessage(L"プレビューする項目がありません");
        UpdateHeader();
        return;
    }
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        g_pane->path = path;
        ShowMessage(L"ファイルを読み込めません");
        UpdateHeader();
        return;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        g_pane->path = path;
        ShowMessage(L"フォルダです");
        UpdateHeader();
        return;
    }
    FILETIME write{};
    ULONGLONG size = 0;
    FileStamp(path, write, size);
    if (g_pane->loadedValid && g_pane->loadedPath == path && g_pane->loadedSize == size &&
        CompareFileTime(&g_pane->loadedWrite, &write) == 0) {
        return;  // 前回と同じ (一覧の更新など)
    }

    g_pane->path = path;
    g_pane->mdFallback = false;
    if (IsImageFile(path)) {
        MarkdownViewClear();
        ShowOnly(Content::Image);
        ImageViewLoad(g_pane->image, path);
    } else if (IsMarkdownFile(path)) {
        ImageViewClear(g_pane->image);
        std::wstring error;
        if (MarkdownViewShow(g_pane->md, path, ThemeIsDark(), GetSettings().markdownRemoteImages, error)) {
            ShowOnly(Content::Markdown);
        } else {
            g_pane->mdFallback = true;  // WebView2 が使えない: 文字のまま表示する
            ShowTextFile(path);
        }
    } else {
        ImageViewClear(g_pane->image);
        MarkdownViewClear();
        ShowTextFile(path);
    }
    g_pane->loadedValid = g_pane->content != Content::Message;
    g_pane->loadedPath = path;
    g_pane->loadedWrite = write;
    g_pane->loadedSize = size;
    UpdateHeader();
}

LRESULT CALLBACK RichProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
    if (g_pane) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (msg == WM_KEYDOWN && !ctrl && !alt) {
            if (wParam == VK_ESCAPE) {  // ビューワーを閉じて一覧へ
                RequestClose();
                return 0;
            }
            if (wParam == VK_TAB) {  // 一覧へフォーカスを戻す
                RequestReturn();
                return 0;
            }
        }
        if (msg == WM_SYSKEYDOWN && alt && !ctrl && wParam == 'Z') {  // 折り返しの切り替え
            g_pane->wrap = !g_pane->wrap;
            ApplyWrap();
            return 0;
        }
        if (msg == WM_CHAR && wParam == L'\t') return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, RichProc, id);
    return DefSubclassProc(h, msg, wParam, lParam);
}

// ---- 見出し (ファイル名と状態) ----

std::wstring HeaderRightText() {
    switch (g_pane->content) {
        case Content::Text: {
            std::wstring s = std::wstring(TextEncodingName(g_pane->doc.encoding)) + L"  |  " + (g_pane->doc.crlf ? L"CRLF" : L"LF");
            if (g_pane->mdFallback) s += L"  |  " + std::wstring(Tr(L"WebView2 なし: そのまま表示"));
            s += L"  |  " + std::wstring(Tr(L"Tab で操作 / Esc で閉じる"));
            return s;
        }
        case Content::Image: {
            std::wstring info = ImageViewInfo(g_pane->image);
            return info.empty() ? std::wstring() : info + L"  |  " + Tr(L"← → 前後 / + - 拡大縮小");
        }
        case Content::Markdown:
            return std::wstring(L"Markdown  |  ") + Tr(L"Tab で操作 / Esc で閉じる");
        default:
            return L"";
    }
}

void PaintPane(HDC hdc) {
    RECT client;
    GetClientRect(g_pane->hwnd, &client);
    int hh = std::min<int>(Scale(kHeaderHeight), client.bottom);
    RECT header{0, 0, client.right, hh};

    HBRUSH barBrush = CreateSolidBrush(ThemeBarBg());
    FillRect(hdc, &header, barBrush);
    DeleteObject(barBrush);
    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(hdc, g_pane->headerFont ? g_pane->headerFont : GetStockObject(DEFAULT_GUI_FONT));

    // 右端の × (閉じる)
    RECT closeRc{client.right - hh, 0, client.right, hh};
    SetTextColor(hdc, ThemeBarText());
    DrawTextW(hdc, L"✕", 1, &closeRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    int pad = Scale(8);
    std::wstring rightText = HeaderRightText();
    COLORREF muted = ThemeIsDark() ? RGB(160, 160, 160) : GetSysColor(COLOR_GRAYTEXT);
    int rightWidth = 0;
    if (!rightText.empty()) {
        RECT calc{0, 0, 0, hh};
        DrawTextW(hdc, rightText.c_str(), -1, &calc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        rightWidth = calc.right - calc.left;
        // 右の説明は、ファイル名が隠れすぎない範囲で出す。
        int maxRight = std::max<int>(0, (closeRc.left - pad) * 65 / 100);
        if (rightWidth > maxRight) rightWidth = maxRight;
        RECT rr{closeRc.left - pad - rightWidth, 0, closeRc.left - pad, hh};
        SetTextColor(hdc, muted);
        DrawTextW(hdc, rightText.c_str(), -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    std::wstring name = g_pane->path.empty() ? std::wstring() : FileNameOf(g_pane->content == Content::Markdown && !MarkdownViewCurrentPath().empty()
                                                                               ? MarkdownViewCurrentPath()
                                                                               : g_pane->path);
    RECT left{pad, 0, std::max<int>(pad, closeRc.left - pad * 2 - rightWidth), hh};
    SetTextColor(hdc, ThemeBarText());
    DrawTextW(hdc, name.c_str(), -1, &left, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // 何も表示していないときは、内容の領域に案内を出す。
    if (g_pane->content == Content::Message) {
        RECT body{0, hh, client.right, client.bottom};
        FillRect(hdc, &body, ThemeWindowBrush());
        SetTextColor(hdc, muted);
        RECT text = body;
        DrawTextW(hdc, Tr(g_pane->message.c_str()), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(hdc, oldFont);
}

void OnImageKey(WPARAM vk) {
    HWND parent = GetParent(g_pane->hwnd);
    switch (vk) {
        case VK_ESCAPE:
            RequestClose();
            break;
        case VK_TAB:
            RequestReturn();
            break;
        case VK_LEFT: case VK_UP: case VK_PRIOR: case VK_BACK:
            PostMessageW(parent, kMsgPreviewStep, static_cast<WPARAM>(-1), 0);
            break;
        case VK_RIGHT: case VK_DOWN: case VK_NEXT: case VK_SPACE:
            PostMessageW(parent, kMsgPreviewStep, 1, 0);
            break;
        case VK_HOME:
            PostMessageW(parent, kMsgPreviewStep, static_cast<WPARAM>(-1000000), 0);
            break;
        case VK_END:
            PostMessageW(parent, kMsgPreviewStep, 1000000, 0);
            break;
        default:
            break;
    }
}

LRESULT CALLBACK PaneProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_pane) {
                // ちらつかないよう、裏のビットマップに描いてから転送する。
                RECT rc;
                GetClientRect(hwnd, &rc);
                HDC buf = CreateCompatibleDC(hdc);
                HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max<LONG>(rc.right, 1), std::max<LONG>(rc.bottom, 1));
                HGDIOBJ old = SelectObject(buf, bmp);
                PaintPane(buf);
                BitBlt(hdc, 0, 0, rc.right, rc.bottom, buf, 0, 0, SRCCOPY);
                SelectObject(buf, old);
                DeleteObject(bmp);
                DeleteDC(buf);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            Layout();
            return 0;
        case WM_LBUTTONUP: {
            if (!g_pane) break;
            RECT rc;
            GetClientRect(hwnd, &rc);
            int hh = Scale(kHeaderHeight);
            int x = static_cast<short>(LOWORD(lParam)), y = static_cast<short>(HIWORD(lParam));
            if (y >= 0 && y < hh && x >= rc.right - hh) PostMessageW(GetParent(hwnd), kMsgPreviewClose, 0, 0);
            return 0;
        }
        case kMsgImageViewKey:
            if (g_pane) OnImageKey(wParam);
            return 0;
        case kMsgImageViewInfo:
        case kMsgMarkdownViewInfo:
            if (g_pane) UpdateHeader();
            return 0;
        case kMsgMarkdownViewKey:
            if (!g_pane) return 0;
            if (wParam == VK_ESCAPE) RequestClose();
            else RequestReturn();
            return 0;
        case kMsgMarkdownViewFailed:
            // WebView2 を使えなかった: 文字のまま表示する。
            if (g_pane && g_pane->content == Content::Markdown) {
                g_pane->mdFallback = true;
                ShowTextFile(g_pane->path);
                UpdateHeader();
            }
            return 0;
        case WM_DESTROY:
            if (g_pane) {
                if (g_pane->headerFont) DeleteObject(g_pane->headerFont);
                delete g_pane;
                g_pane = nullptr;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

HWND PreviewPaneCreate(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PaneProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kPaneClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    HINSTANCE inst = GetModuleHandleW(nullptr);
    auto* pane = new Pane();
    g_pane = pane;
    pane->hwnd = CreateWindowExW(0, kPaneClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, nullptr, inst, nullptr);
    if (!pane->hwnd) {
        g_pane = nullptr;
        delete pane;
        return nullptr;
    }
    int dpi = static_cast<int>(GetDpiForWindow(pane->hwnd));
    pane->headerFont = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");

    pane->image = ImageViewCreate(pane->hwnd);
    pane->md = MarkdownViewCreate(pane->hwnd);

    // RichEdit (Msftedit.dll) は System32 のものだけを読み込む。
    LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    pane->rich = CreateWindowExW(0, L"RICHEDIT50W", L"", WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | ES_READONLY,
                                 0, 0, 0, 0, pane->hwnd, nullptr, inst, nullptr);
    if (pane->rich) {
        SendMessageW(pane->rich, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
        SendMessageW(pane->rich, EM_SETLIMITTEXT, 0x7FFFFFF0, 0);
        SendMessageW(pane->rich, EM_SETREADONLY, TRUE, 0);
        SetWindowSubclass(pane->rich, RichProc, kRichSubclassId, 0);
        ApplyRichFormat();
        ApplyWrap();
    }
    pane->message = L"プレビューする項目がありません";
    return pane->hwnd;
}

void PreviewPaneShowFile(const std::wstring& path) {
    if (g_pane && g_pane->rich) ShowFile(path);
}

bool PreviewPaneFocusContent() {
    if (!g_pane) return false;
    switch (g_pane->content) {
        case Content::Image: SetFocus(g_pane->image); return true;
        case Content::Markdown: MarkdownViewFocus(g_pane->md); return true;
        case Content::Text: SetFocus(g_pane->rich); return true;
        default: return false;
    }
}

void PreviewPaneWheel(int delta) {
    if (!g_pane) return;
    POINT pt;
    GetCursorPos(&pt);
    LPARAM pos = MAKELPARAM(static_cast<short>(pt.x), static_cast<short>(pt.y));
    switch (g_pane->content) {
        case Content::Image: SendMessageW(g_pane->image, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<short>(delta)), pos); break;
        case Content::Text: SendMessageW(g_pane->rich, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<short>(delta)), pos); break;
        case Content::Markdown: MarkdownViewScroll(delta / WHEEL_DELTA); break;
        default: break;
    }
}

void PreviewPaneApplyTheme() {
    if (!g_pane || !g_pane->rich) return;
    ApplyRichFormat();
    InvalidateRect(g_pane->hwnd, nullptr, TRUE);
    if (g_pane->content == Content::Markdown) {
        std::wstring error;
        std::wstring path = MarkdownViewCurrentPath().empty() ? g_pane->path : MarkdownViewCurrentPath();
        MarkdownViewShow(g_pane->md, path, ThemeIsDark(), GetSettings().markdownRemoteImages, error);
    }
}

void PreviewPaneApplyLanguage() {
    if (!g_pane) return;
    InvalidateRect(g_pane->hwnd, nullptr, TRUE);
}
