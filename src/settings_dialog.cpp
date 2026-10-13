#include "settings_dialog.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <string>

#include "i18n.h"
#include "settings.h"
#include "theme.h"

namespace {

constexpr wchar_t kDialogClass[] = L"FilMSettingsDialog";
constexpr wchar_t kCaptureClass[] = L"FilMKeyCapture";

enum : int {
    kIdLangCombo = 100,
    kIdThemeCombo = 101,
    kIdMdRemote = 108,
    kIdEditorDefault = 110,
    kIdEditorNotepad = 111,
    kIdEditorCustom = 112,
    kIdEditorPath = 113,
    kIdEditorBrowse = 114,
    kIdKeyList = 120,
    kIdKeyChange = 121,
    kIdKeyAdd = 122,
    kIdKeyClear = 123,
    kIdKeyReset = 124,
};

// キー一覧の 1 行。binding が -1 なら「未割り当て」のコマンド。
struct KeyRow {
    std::wstring command;
    int binding = -1;
};

struct DialogState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HFONT font = nullptr;
    int dpi = 96;
    bool done = false;
    bool accepted = false;
    bool keysChanged = false;
    AppSettings settings;
    std::vector<KeyBinding>* bindings = nullptr;
    std::vector<KeyRow> rows;
};

int S(const DialogState* st, int px) {
    return MulDiv(px, st->dpi, 96);
}

HWND MakeControl(DialogState* st, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id,
                 DWORD exStyle = 0) {
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, S(st, x), S(st, y), S(st, w), S(st, h),
                             st->hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    return c;
}

// ---- キーの入力 (「キーを押してください」の小さなウィンドウ) ----

struct CaptureState {
    bool done = false;
    bool captured = false;
    KeyBinding result;
    HFONT font = nullptr;
};

LRESULT CALLBACK CaptureProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<CaptureState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            WORD vk = static_cast<WORD>(wParam);
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) return 0;
            std::wstring name = VirtualKeyToKeyName(vk);
            if (name.empty()) return 0;  // 割り当てできないキーは無視する
            st->result = KeyBinding{};
            st->result.key = name;
            st->result.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            st->result.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            st->result.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            st->captured = true;
            st->done = true;
            return 0;
        }
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_KEYUP:
        case WM_SYSKEYUP:
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL) {
                st->done = true;
                return 0;
            }
            break;
        case WM_CLOSE:
            st->done = true;
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
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool CaptureKey(DialogState* dlg, const std::wstring& command, KeyBinding& out) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CaptureProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCaptureClass;
    RegisterClassExW(&wc);

    CaptureState st;
    st.font = dlg->font;
    int w = S(dlg, 380), h = S(dlg, 150);
    RECT orc;
    GetWindowRect(dlg->hwnd, &orc);
    int x = (orc.left + orc.right - w) / 2, y = (orc.top + orc.bottom - h) / 2;
    HWND wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kCaptureClass, Tr(L"キーの割り当て"), WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y,
                               w, h, dlg->hwnd, nullptr, wc.hInstance, &st);
    if (!wnd) return false;
    RECT crc;
    GetClientRect(wnd, &crc);
    // クライアント領域が狭くならないよう、枠の分を足す。
    RECT frame{0, 0, w, h};
    AdjustWindowRectEx(&frame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    SetWindowPos(wnd, nullptr, x, y, frame.right - frame.left, frame.bottom - frame.top, SWP_NOZORDER);

    std::wstring prompt = Fmt(L"「{0}」に割り当てるキーを押してください。\nCtrl / Alt / Shift との組み合わせも使えます。", {command});
    HWND label = CreateWindowExW(0, L"STATIC", prompt.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, S(dlg, 16), S(dlg, 14), S(dlg, 348),
                                 S(dlg, 60), wnd, nullptr, wc.hInstance, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(dlg->font), TRUE);
    HWND cancel = CreateWindowExW(0, L"BUTTON", Tr(L"キャンセル"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, S(dlg, 264),
                                  S(dlg, 84), S(dlg, 100), S(dlg, 28), wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                                  wc.hInstance, nullptr);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(dlg->font), TRUE);
    SetWindowLongPtrW(cancel, GWL_STYLE, GetWindowLongPtrW(cancel, GWL_STYLE) & ~static_cast<LONG_PTR>(BS_OWNERDRAW));
    ThemeApplyToWindow(wnd);

    EnableWindow(dlg->hwnd, FALSE);
    ShowWindow(wnd, SW_SHOW);
    SetFocus(wnd);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(dlg->hwnd, TRUE);
    DestroyWindow(wnd);
    SetForegroundWindow(dlg->hwnd);
    if (st.captured) out = st.result;
    return st.captured;
}

// ---- キー一覧 ----

std::wstring LabelOf(const std::wstring& command) {
    std::wstring label = GetCommandLabel(command);
    return label.empty() ? command : label;
}

void RebuildRows(DialogState* st) {
    st->rows.clear();
    for (const auto& cmd : GetAllCommands()) {
        bool any = false;
        for (size_t i = 0; i < st->bindings->size(); ++i) {
            if ((*st->bindings)[i].command == cmd.first) {
                st->rows.push_back({cmd.first, static_cast<int>(i)});
                any = true;
            }
        }
        if (!any) st->rows.push_back({cmd.first, -1});
    }
    // 一覧にないコマンド名 (手で書いたもの) も落とさず表示する。
    for (size_t i = 0; i < st->bindings->size(); ++i) {
        bool known = false;
        for (const KeyRow& r : st->rows) {
            if (r.binding == static_cast<int>(i)) known = true;
        }
        if (!known) st->rows.push_back({(*st->bindings)[i].command, static_cast<int>(i)});
    }
}

void FillKeyList(DialogState* st, int select = -1) {
    HWND list = GetDlgItem(st->hwnd, kIdKeyList);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < st->rows.size(); ++i) {
        const KeyRow& r = st->rows[i];
        std::wstring label = LabelOf(r.command);
        std::wstring combo = r.binding >= 0 ? FormatKeyCombo((*st->bindings)[r.binding]) : std::wstring(Tr(L"(未割り当て)"));
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(label.c_str());
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, static_cast<int>(i), 1, const_cast<LPWSTR>(combo.c_str()));
        ListView_SetItemText(list, static_cast<int>(i), 2, const_cast<LPWSTR>(r.command.c_str()));
    }
    if (select >= 0 && select < static_cast<int>(st->rows.size())) {
        ListView_SetItemState(list, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, select, FALSE);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

int SelectedRow(DialogState* st) {
    return ListView_GetNextItem(GetDlgItem(st->hwnd, kIdKeyList), -1, LVNI_SELECTED);
}

// nk が他のコマンドに割り当て済みなら、置き換えてよいか確かめて取り除く (取り除いた添字を erased へ)。
// 置き換えを断られたら false。
bool ResolveConflict(DialogState* st, const KeyBinding& nk, int ownIndex, int& erased) {
    erased = -1;
    WORD vk = KeyNameToVirtualKey(nk.key);
    for (size_t i = 0; i < st->bindings->size(); ++i) {
        const KeyBinding& b = (*st->bindings)[i];
        if (static_cast<int>(i) == ownIndex) continue;
        if (KeyNameToVirtualKey(b.key) == vk && b.ctrl == nk.ctrl && b.alt == nk.alt && b.shift == nk.shift) {
            std::wstring msg = Fmt(L"{0} は「{1}」に割り当てられています。\n置き換えますか?", {FormatKeyCombo(nk), LabelOf(b.command)});
            if (ThemedMessageBox(st->hwnd, msg.c_str(), Tr(L"キーの割り当て"), MB_YESNO | MB_ICONQUESTION) != IDYES) return false;
            st->bindings->erase(st->bindings->begin() + i);
            erased = static_cast<int>(i);
            return true;
        }
    }
    return true;
}

void AssignKey(DialogState* st, bool addNew) {
    int row = SelectedRow(st);
    if (row < 0 || row >= static_cast<int>(st->rows.size())) {
        ThemedMessageBox(st->hwnd, Tr(L"一覧からコマンドを選んでください。"), Tr(L"設定"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    KeyRow r = st->rows[row];
    KeyBinding nk;
    if (!CaptureKey(st, LabelOf(r.command), nk)) return;
    nk.command = r.command;

    int own = addNew ? -1 : r.binding;
    int erased = -1;
    if (!ResolveConflict(st, nk, own, erased)) return;
    if (own >= 0 && erased >= 0 && erased < own) --own;  // 取り除いた要素が前にあれば、自分の添字は 1 つ前へ
    int index;
    if (own >= 0) {
        (*st->bindings)[own] = nk;
        index = own;
    } else {
        st->bindings->push_back(nk);
        index = static_cast<int>(st->bindings->size()) - 1;
    }
    st->keysChanged = true;
    RebuildRows(st);
    int sel = 0;
    for (size_t i = 0; i < st->rows.size(); ++i) {
        if (st->rows[i].binding == index) sel = static_cast<int>(i);
    }
    FillKeyList(st, sel);
}

void ClearKey(DialogState* st) {
    int row = SelectedRow(st);
    if (row < 0 || row >= static_cast<int>(st->rows.size()) || st->rows[row].binding < 0) return;
    st->bindings->erase(st->bindings->begin() + st->rows[row].binding);
    st->keysChanged = true;
    RebuildRows(st);
    FillKeyList(st, std::min(row, static_cast<int>(st->rows.size()) - 1));
}

void ResetKeys(DialogState* st) {
    if (ThemedMessageBox(st->hwnd, Tr(L"キー割り当てをすべて既定値に戻します。\nよろしいですか?"), Tr(L"設定"),
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    *st->bindings = GetDefaultKeyBindings();
    st->keysChanged = true;
    RebuildRows(st);
    FillKeyList(st, 0);
}

// ---- エディタ ----

void UpdateEditorControls(DialogState* st) {
    bool custom = SendDlgItemMessageW(st->hwnd, kIdEditorCustom, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(GetDlgItem(st->hwnd, kIdEditorPath), custom);
    EnableWindow(GetDlgItem(st->hwnd, kIdEditorBrowse), custom);
}

void BrowseEditor(DialogState* st) {
    wchar_t file[MAX_PATH] = L"";
    GetDlgItemTextW(st->hwnd, kIdEditorPath, file, MAX_PATH);
    std::wstring filter = std::wstring(Tr(L"プログラム (*.exe)")) + L'\0' + L"*.exe" + L'\0' + Tr(L"すべてのファイル") + L'\0' + L"*.*" + L'\0';
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = st->hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = Tr(L"テキストエディタを選択");
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) SetDlgItemTextW(st->hwnd, kIdEditorPath, file);
}

bool CommitAndClose(DialogState* st) {
    // エディタ
    if (SendDlgItemMessageW(st->hwnd, kIdEditorNotepad, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        st->settings.editor = L"notepad";
    } else if (SendDlgItemMessageW(st->hwnd, kIdEditorCustom, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        wchar_t path[MAX_PATH] = L"";
        GetDlgItemTextW(st->hwnd, kIdEditorPath, path, MAX_PATH);
        std::wstring p = path;
        while (!p.empty() && (p.front() == L' ' || p.front() == L'"')) p.erase(p.begin());
        while (!p.empty() && (p.back() == L' ' || p.back() == L'"')) p.pop_back();
        if (p.empty() || GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
            ThemedMessageBox(st->hwnd, Fmt(L"エディタが見つかりません:\n{0}", {p}).c_str(), Tr(L"設定"), MB_OK | MB_ICONWARNING);
            return false;
        }
        st->settings.editor = p;
    } else {
        st->settings.editor.clear();
    }
    st->settings.markdownRemoteImages = SendDlgItemMessageW(st->hwnd, kIdMdRemote, BM_GETCHECK, 0, 0) == BST_CHECKED;
    st->settings.language = SendDlgItemMessageW(st->hwnd, kIdLangCombo, CB_GETCURSEL, 0, 0) == 1 ? Language::English : Language::Japanese;
    int theme = static_cast<int>(SendDlgItemMessageW(st->hwnd, kIdThemeCombo, CB_GETCURSEL, 0, 0));
    st->settings.theme = theme == 1 ? ThemeMode::Light : theme == 2 ? ThemeMode::Dark : ThemeMode::System;
    st->accepted = true;
    st->done = true;
    return true;
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (id == IDOK) {
                CommitAndClose(st);
                return 0;
            }
            if (id == IDCANCEL) {
                st->done = true;
                return 0;
            }
            if (code == BN_CLICKED) {
                if (id == kIdEditorDefault || id == kIdEditorNotepad || id == kIdEditorCustom) UpdateEditorControls(st);
                else if (id == kIdEditorBrowse) BrowseEditor(st);
                else if (id == kIdKeyChange) AssignKey(st, false);
                else if (id == kIdKeyAdd) AssignKey(st, true);
                else if (id == kIdKeyClear) ClearKey(st);
                else if (id == kIdKeyReset) ResetKeys(st);
            }
            return 0;
        }
        case WM_NOTIFY: {
            const NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr->idFrom == static_cast<UINT_PTR>(kIdKeyList) && hdr->code == NM_DBLCLK) {
                AssignKey(st, false);
            }
            break;
        }
        case WM_CLOSE:
            st->done = true;
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, ThemeBarBrush());
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORDLG: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, ThemeBarText());
            SetBkColor(hdc, ThemeBarBg());
            return reinterpret_cast<LRESULT>(ThemeBarBrush());
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HBRUSH brush;
            if (ThemeHandleCtlColor(msg, reinterpret_cast<HDC>(wParam), brush)) return reinterpret_cast<LRESULT>(brush);
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

SettingsDialogResult ShowSettingsDialog(HWND owner, std::vector<KeyBinding>& bindings) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DialogProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kDialogClass;
    RegisterClassExW(&wc);

    DialogState st;
    st.owner = owner;
    st.settings = GetSettings();
    st.bindings = &bindings;
    st.dpi = static_cast<int>(GetDpiForWindow(owner));
    st.font = CreateFontW(-MulDiv(9, st.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT frame{0, 0, S(&st, 640), S(&st, 604)};
    AdjustWindowRectEx(&frame, style, FALSE, exStyle);
    int w = frame.right - frame.left, h = frame.bottom - frame.top;
    RECT orc;
    GetWindowRect(owner, &orc);
    int x = std::max<int>(0, (orc.left + orc.right - w) / 2);
    int y = std::max<int>(0, (orc.top + orc.bottom - h) / 2);
    HWND hwnd = CreateWindowExW(exStyle, kDialogClass, Tr(L"設定"), style, x, y, w, h, owner, nullptr, wc.hInstance, &st);
    st.hwnd = hwnd;
    if (!hwnd) {
        DeleteObject(st.font);
        return {};
    }

    // 表示
    MakeControl(&st, L"BUTTON", Tr(L"表示"), BS_GROUPBOX, 12, 8, 616, 86, -1);
    MakeControl(&st, L"STATIC", Tr(L"言語"), SS_LEFT, 28, 38, 60, 20, -1);
    HWND lang = MakeControl(&st, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 90, 34, 160, 120, kIdLangCombo);
    SendMessageW(lang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"日本語"));
    SendMessageW(lang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
    SendMessageW(lang, CB_SETCURSEL, st.settings.language == Language::English ? 1 : 0, 0);
    MakeControl(&st, L"STATIC", Tr(L"テーマ"), SS_LEFT, 290, 38, 60, 20, -1);
    HWND theme = MakeControl(&st, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 352, 34, 200, 120, kIdThemeCombo);
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"システムに合わせる")));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"ライト")));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"ダーク")));
    SendMessageW(theme, CB_SETCURSEL,
                 st.settings.theme == ThemeMode::Light ? 1 : st.settings.theme == ThemeMode::Dark ? 2 : 0, 0);

    MakeControl(&st, L"BUTTON", Tr(L"Markdown でインターネット上の画像も表示する (画像の取得で外部へ接続します)"), BS_AUTOCHECKBOX | WS_TABSTOP, 28, 66, 580, 20, kIdMdRemote);
    SendDlgItemMessageW(hwnd, kIdMdRemote, BM_SETCHECK, st.settings.markdownRemoteImages ? BST_CHECKED : BST_UNCHECKED, 0);

    // テキストエディタ
    MakeControl(&st, L"BUTTON", Tr(L"テキストエディタ (E キー・設定ファイルの編集)"), BS_GROUPBOX, 12, 102, 616, 100, -1);
    MakeControl(&st, L"BUTTON", Tr(L"Windows の既定 (関連付け。無ければメモ帳)"), BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 28, 122, 400, 20,
                kIdEditorDefault);
    MakeControl(&st, L"BUTTON", Tr(L"メモ帳 (notepad.exe)"), BS_AUTORADIOBUTTON, 28, 144, 400, 20, kIdEditorNotepad);
    MakeControl(&st, L"BUTTON", Tr(L"プログラムを指定:"), BS_AUTORADIOBUTTON, 28, 168, 130, 20, kIdEditorCustom);
    bool custom = !st.settings.editor.empty() && st.settings.editor != L"notepad";
    MakeControl(&st, L"EDIT", custom ? st.settings.editor.c_str() : L"", ES_AUTOHSCROLL | WS_TABSTOP, 160, 166, 350, 24, kIdEditorPath,
                WS_EX_CLIENTEDGE);
    MakeControl(&st, L"BUTTON", Tr(L"参照..."), BS_PUSHBUTTON | WS_TABSTOP, 518, 165, 96, 26, kIdEditorBrowse);
    int radio = custom ? kIdEditorCustom : st.settings.editor == L"notepad" ? kIdEditorNotepad : kIdEditorDefault;
    SendDlgItemMessageW(hwnd, radio, BM_SETCHECK, BST_CHECKED, 0);
    UpdateEditorControls(&st);

    // キー割り当て
    MakeControl(&st, L"BUTTON", Tr(L"キー割り当て"), BS_GROUPBOX, 12, 210, 616, 336, -1);
    HWND list = MakeControl(&st, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, 24, 232, 592, 262,
                            kIdKeyList, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<LPWSTR>(Tr(L"コマンド"));
    col.cx = S(&st, 250);
    ListView_InsertColumn(list, 0, &col);
    col.pszText = const_cast<LPWSTR>(Tr(L"キー"));
    col.cx = S(&st, 170);
    ListView_InsertColumn(list, 1, &col);
    col.pszText = const_cast<LPWSTR>(Tr(L"コマンド名"));
    col.cx = S(&st, 140);
    ListView_InsertColumn(list, 2, &col);
    MakeControl(&st, L"BUTTON", Tr(L"変更..."), BS_PUSHBUTTON | WS_TABSTOP, 24, 502, 96, 28, kIdKeyChange);
    MakeControl(&st, L"BUTTON", Tr(L"追加..."), BS_PUSHBUTTON | WS_TABSTOP, 126, 502, 96, 28, kIdKeyAdd);
    MakeControl(&st, L"BUTTON", Tr(L"解除"), BS_PUSHBUTTON | WS_TABSTOP, 228, 502, 96, 28, kIdKeyClear);
    MakeControl(&st, L"BUTTON", Tr(L"既定値に戻す..."), BS_PUSHBUTTON | WS_TABSTOP, 484, 502, 132, 28, kIdKeyReset);

    MakeControl(&st, L"BUTTON", Tr(L"OK"), BS_DEFPUSHBUTTON | WS_TABSTOP, 428, 558, 96, 28, IDOK);
    MakeControl(&st, L"BUTTON", Tr(L"キャンセル"), BS_PUSHBUTTON | WS_TABSTOP, 532, 558, 96, 28, IDCANCEL);

    RebuildRows(&st);
    FillKeyList(&st, 0);
    ThemeApplyToWindow(hwnd);
    ThemeStyleListView(list);

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(GetDlgItem(hwnd, kIdLangCombo));
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    SetForegroundWindow(owner);
    DeleteObject(st.font);

    SettingsDialogResult result;
    result.accepted = st.accepted;
    result.keysChanged = st.accepted && st.keysChanged;
    if (st.accepted) GetSettings() = st.settings;
    return result;
}
