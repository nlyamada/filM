#include "theme.h"

#include <dwmapi.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <vector>

#include "i18n.h"

namespace {

bool g_dark = false;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

BOOL CALLBACK ApplyToChild(HWND h, LPARAM lParam) {
    bool dark = lParam != 0;
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    // ボタン・入力欄・コンボ・一覧・スクロールバー等は Explorer 系のテーマ名でダーク配色になる。
    // グループボックスはテーマ付きだと見出しが黒固定になるので、ダークでは外して WM_CTLCOLORSTATIC の文字色に従わせる。
    if (_wcsicmp(cls, L"Button") == 0 && (GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) {
        SetWindowTheme(h, dark ? L"" : nullptr, dark ? L"" : nullptr);
        return TRUE;
    }
    if (_wcsicmp(cls, L"Button") == 0 || _wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ComboBox") == 0 ||
        _wcsicmp(cls, L"ListBox") == 0 || _wcsicmp(cls, L"SysListView32") == 0 || _wcsicmp(cls, L"msctls_updown32") == 0) {
        const wchar_t* theme = dark ? L"DarkMode_Explorer" : L"Explorer";
        if (dark && (_wcsicmp(cls, L"ComboBox") == 0 || _wcsicmp(cls, L"Edit") == 0)) theme = L"DarkMode_CFD";
        SetWindowTheme(h, theme, nullptr);
    }
    return TRUE;
}

// 列見出し (ヘッダー) の描画。テーマ付きの見出しは文字色を変えられないので、ダークのときは自前で描く。
LRESULT CALLBACK HeaderSubclassProc(HWND hHeader, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                    DWORD_PTR /*refData*/) {
    if (msg == WM_ERASEBKGND && g_dark) return 1;
    if (msg == WM_PAINT && g_dark) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hHeader, &ps);
        RECT client;
        GetClientRect(hHeader, &client);
        HBRUSH bg = CreateSolidBrush(RGB(40, 40, 40));
        FillRect(hdc, &client, bg);
        DeleteObject(bg);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(hHeader, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = SelectObject(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(230, 230, 230));
        int dpi = static_cast<int>(GetDpiForWindow(hHeader));
        int count = Header_GetItemCount(hHeader);
        for (int i = 0; i < count; ++i) {
            RECT rc;
            if (!Header_GetItemRect(hHeader, i, &rc)) continue;
            wchar_t text[128] = L"";
            HDITEMW hi{};
            hi.mask = HDI_TEXT | HDI_FORMAT;
            hi.pszText = text;
            hi.cchTextMax = 128;
            Header_GetItem(hHeader, i, &hi);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            MoveToEx(hdc, rc.right - 1, rc.top + 3, nullptr);
            LineTo(hdc, rc.right - 1, rc.bottom - 3);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            RECT tr = rc;
            tr.left += MulDiv(6, dpi, 96);
            tr.right -= MulDiv(6, dpi, 96);
            UINT align = (hi.fmt & HDF_RIGHT) ? DT_RIGHT : DT_LEFT;
            DrawTextW(hdc, text, -1, &tr, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (hi.fmt & (HDF_SORTUP | HDF_SORTDOWN)) {
                int cx = (rc.left + rc.right) / 2, top = rc.top + MulDiv(3, dpi, 96), s = MulDiv(3, dpi, 96);
                POINT up[3] = {{cx - s, top + s}, {cx + s, top + s}, {cx, top}};
                POINT down[3] = {{cx - s, top}, {cx + s, top}, {cx, top + s}};
                HBRUSH arrow = CreateSolidBrush(RGB(230, 230, 230));
                HGDIOBJ oldBrush = SelectObject(hdc, arrow);
                HPEN none = CreatePen(PS_SOLID, 1, RGB(230, 230, 230));
                HGDIOBJ oldP = SelectObject(hdc, none);
                Polygon(hdc, (hi.fmt & HDF_SORTUP) ? up : down, 3);
                SelectObject(hdc, oldP);
                SelectObject(hdc, oldBrush);
                DeleteObject(none);
                DeleteObject(arrow);
            }
        }
        SelectObject(hdc, oldFont);
        EndPaint(hHeader, &ps);
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hHeader, HeaderSubclassProc, subclassId);
    return DefSubclassProc(hHeader, msg, wParam, lParam);
}

}  // namespace

void ThemeStyleListView(HWND list) {
    SetWindowTheme(list, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    ListView_SetBkColor(list, ThemeWindowBg());
    ListView_SetTextBkColor(list, ThemeWindowBg());
    ListView_SetTextColor(list, ThemeText());
    if (HWND header = ListView_GetHeader(list)) {
        SetWindowTheme(header, g_dark ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
        SetWindowSubclass(header, HeaderSubclassProc, 5, 0);
        InvalidateRect(header, nullptr, TRUE);
    }
    InvalidateRect(list, nullptr, TRUE);
}

bool ThemeIsDark() {
    return g_dark;
}

void ThemeSetDark(bool dark) {
    g_dark = dark;
}

bool ThemeSystemIsDark() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return false;
    }
    return value == 0;
}

COLORREF ThemeWindowBg() { return g_dark ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW); }
COLORREF ThemeText() { return g_dark ? RGB(230, 230, 230) : GetSysColor(COLOR_WINDOWTEXT); }
COLORREF ThemeBarBg() { return g_dark ? RGB(48, 48, 48) : GetSysColor(COLOR_BTNFACE); }
COLORREF ThemeBarText() { return g_dark ? RGB(230, 230, 230) : GetSysColor(COLOR_BTNTEXT); }
COLORREF ThemeBarHot() { return g_dark ? RGB(70, 70, 70) : GetSysColor(COLOR_BTNSHADOW); }
COLORREF ThemeSelectBg() { return g_dark ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW); }

HBRUSH ThemeWindowBrush() {
    static HBRUSH dark = CreateSolidBrush(RGB(32, 32, 32));
    return g_dark ? dark : GetSysColorBrush(COLOR_WINDOW);
}

HBRUSH ThemeBarBrush() {
    static HBRUSH dark = CreateSolidBrush(RGB(48, 48, 48));
    return g_dark ? dark : GetSysColorBrush(COLOR_BTNFACE);
}

void ThemeApplyAppMode(bool dark) {
    // 非公開 API (uxtheme の序数 135 / 136) でメニューのダーク化を OS に伝える。無ければ何もしない。
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
    if (setMode) setMode(dark ? 2 /*ForceDark*/ : 3 /*ForceLight*/);
    if (flush) flush();
}

void ThemeApplyToWindow(HWND hwnd) {
    BOOL useDark = g_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    EnumChildWindows(hwnd, ApplyToChild, g_dark ? 1 : 0);
}

// ---- ダークモード用のメッセージボックス (OS 標準のものはダークにできないので自前で作る) ----

namespace {

constexpr wchar_t kMsgBoxClass[] = L"FilMMessageBox";

struct MsgBoxState {
    bool done = false;
    int result = IDOK;
    std::vector<int> ids;  // 並んでいるボタンの ID
};

LRESULT CALLBACK MsgBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<MsgBoxState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    auto has = [&](int id) { return st && std::find(st->ids.begin(), st->ids.end(), id) != st->ids.end(); };
    switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (!st) break;
            if (id == IDCANCEL && !has(IDCANCEL)) {
                if (has(IDOK) && st->ids.size() == 1) id = IDOK;  // OK だけのときは Esc で閉じる
                else return 0;                                    // はい/いいえ では Esc を無視する (標準と同じ)
            }
            if (has(id)) {
                st->result = id;
                st->done = true;
            }
            return 0;
        }
        case WM_CLOSE:
            if (st && (has(IDCANCEL) || (has(IDOK) && st->ids.size() == 1))) {
                st->result = has(IDCANCEL) ? IDCANCEL : IDOK;
                st->done = true;
            }
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, ThemeBarBrush());
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, ThemeBarText());
            SetBkColor(hdc, ThemeBarBg());
            return reinterpret_cast<LRESULT>(ThemeBarBrush());
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int ThemedMessageBox(HWND owner, const wchar_t* text, const wchar_t* caption, UINT flags) {
    if (!g_dark) return MessageBoxW(owner, text, caption, flags);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgBoxProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMsgBoxClass;
    RegisterClassExW(&wc);

    UINT type = flags & MB_TYPEMASK;
    MsgBoxState st;
    struct Btn {
        int id;
        const wchar_t* label;
    };
    std::vector<Btn> buttons;
    switch (type) {
        case MB_OKCANCEL: buttons = {{IDOK, Tr(L"OK")}, {IDCANCEL, Tr(L"キャンセル")}}; break;
        case MB_YESNO: buttons = {{IDYES, Tr(L"はい")}, {IDNO, Tr(L"いいえ")}}; break;
        case MB_YESNOCANCEL: buttons = {{IDYES, Tr(L"はい")}, {IDNO, Tr(L"いいえ")}, {IDCANCEL, Tr(L"キャンセル")}}; break;
        default: buttons = {{IDOK, Tr(L"OK")}}; break;
    }
    for (const Btn& b : buttons) st.ids.push_back(b.id);
    size_t defIndex = 0;
    switch (flags & MB_DEFMASK) {
        case MB_DEFBUTTON2: defIndex = 1; break;
        case MB_DEFBUTTON3: defIndex = 2; break;
    }
    if (defIndex >= buttons.size()) defIndex = 0;

    HWND dpiWnd = (owner && IsWindow(owner)) ? owner : GetDesktopWindow();
    int dpi = static_cast<int>(GetDpiForWindow(dpiWnd));
    auto S = [&](int px) { return MulDiv(px, dpi, 96); };
    HFONT font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");

    const wchar_t* iconId = nullptr;
    switch (flags & MB_ICONMASK) {
        case MB_ICONWARNING: iconId = IDI_WARNING; break;
        case MB_ICONERROR: iconId = IDI_ERROR; break;
        case MB_ICONQUESTION: iconId = IDI_QUESTION; break;
        case MB_ICONINFORMATION: iconId = IDI_INFORMATION; break;
    }

    // 本文の大きさ (幅は最大 480px。それ以上は折り返す)。
    HDC dc = GetDC(nullptr);
    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT tr{0, 0, S(iconId ? 420 : 460), 0};
    DrawTextW(dc, text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    ReleaseDC(nullptr, dc);
    int textW = std::max<int>(tr.right, S(200)), textH = std::max<int>(tr.bottom, S(iconId ? 32 : 16));
    int iconW = iconId ? S(32 + 16) : 0;
    int btnW = S(92), btnH = S(28), gap = S(8);
    int btnsW = static_cast<int>(buttons.size()) * btnW + static_cast<int>(buttons.size() - 1) * gap;
    int clientW = std::max(S(16) * 2 + iconW + textW, S(16) * 2 + btnsW);
    int clientH = S(16) + textH + S(20) + btnH + S(14);

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT frame{0, 0, clientW, clientH};
    AdjustWindowRectEx(&frame, style, FALSE, exStyle);
    int w = frame.right - frame.left, h = frame.bottom - frame.top;
    RECT orc;
    if (owner && IsWindow(owner) && IsWindowVisible(owner)) {
        GetWindowRect(owner, &orc);
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    }
    int x = (orc.left + orc.right - w) / 2, y = (orc.top + orc.bottom - h) / 2;
    HWND hwnd = CreateWindowExW(exStyle, kMsgBoxClass, caption ? caption : L"filM", style, x, y, w, h,
                                (owner && IsWindow(owner)) ? owner : nullptr, nullptr, wc.hInstance, &st);
    if (!hwnd) {
        DeleteObject(font);
        return MessageBoxW(owner, text, caption, flags);
    }
    auto child = [&](const wchar_t* cls, const wchar_t* txt, DWORD st2, int cx, int cy, int cw, int ch, int id) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st2, cx, cy, cw, ch, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), wc.hInstance, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return c;
    };
    if (iconId) {
        HWND ic = child(L"STATIC", L"", SS_ICON, S(16), S(16), S(32), S(32), -1);
        SendMessageW(ic, STM_SETICON, reinterpret_cast<WPARAM>(LoadIconW(nullptr, iconId)), 0);
    }
    child(L"STATIC", text, SS_LEFT | SS_NOPREFIX, S(16) + iconW, S(16), textW + S(4), textH, -1);
    int bx = (clientW - btnsW) / 2;
    for (size_t i = 0; i < buttons.size(); ++i) {
        DWORD bs = WS_TABSTOP | (i == defIndex ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
        if (i == 0) bs |= WS_GROUP;
        child(L"BUTTON", buttons[i].label, bs, bx + static_cast<int>(i) * (btnW + gap), clientH - btnH - S(14), btnW, btnH, buttons[i].id);
    }
    ThemeApplyToWindow(hwnd);
    MessageBeep(flags & MB_ICONMASK);

    bool disableOwner = owner && IsWindow(owner) && IsWindowEnabled(owner);
    if (disableOwner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(GetDlgItem(hwnd, buttons[defIndex].id));
    MSG m;
    while (!st.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (disableOwner) EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    if (owner && IsWindow(owner)) SetForegroundWindow(owner);
    DeleteObject(font);
    return st.result;
}

bool ThemeHandleCtlColor(UINT msg, HDC hdc, HBRUSH& brush) {
    if (!g_dark) return false;
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        SetTextColor(hdc, ThemeText());
        SetBkColor(hdc, ThemeWindowBg());
        brush = ThemeWindowBrush();
    } else {  // STATIC / DLG / BTN
        SetTextColor(hdc, ThemeBarText());
        SetBkColor(hdc, ThemeBarBg());
        brush = ThemeBarBrush();
    }
    return true;
}
