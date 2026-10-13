#include "input_dialog.h"
#include "i18n.h"
#include "theme.h"

#include <commctrl.h>

#include <cstring>
#include <memory>

#include "completion.h"
#include "favorites.h"
#include "paths.h"

namespace {

constexpr wchar_t kAppTitle[] = L"filM";
constexpr int kIdFirstField = 2000;
constexpr int kIdHint = 1999;
constexpr int kLabelWidth = 62;     // ダイアログ単位
constexpr int kEditWidth = 200;
constexpr int kEditWidthWide = 280;  // パス入力欄がある場合
constexpr int kRowHeight = 18;
constexpr int kMargin = 8;
constexpr UINT_PTR kCompletionSubclassId = 77;

// DLGTEMPLATE を組み立てるための小さなバッファ。
class TemplateBuffer {
public:
    void Align4() {
        while (data_.size() % 4 != 0) data_.push_back(0);
    }
    void Word(WORD v) {
        data_.push_back(static_cast<BYTE>(v & 0xFF));
        data_.push_back(static_cast<BYTE>(v >> 8));
    }
    void Dword(DWORD v) {
        Word(static_cast<WORD>(v & 0xFFFF));
        Word(static_cast<WORD>(v >> 16));
    }
    void String(const std::wstring& s) {
        for (wchar_t c : s) Word(c);
        Word(0);
    }
    void Item(DWORD style, short x, short y, short cx, short cy, WORD id, WORD classAtom,
              const std::wstring& text) {
        Align4();
        Dword(style);
        Dword(0);  // 拡張スタイル
        Word(static_cast<WORD>(x));
        Word(static_cast<WORD>(y));
        Word(static_cast<WORD>(cx));
        Word(static_cast<WORD>(cy));
        Word(id);
        Word(0xFFFF);
        Word(classAtom);
        String(text);
        Word(0);  // 作成データなし
    }
    std::vector<BYTE>& Data() { return data_; }
    // 先頭の DLGTEMPLATE の項目数 (cdit) を後から書き込む。
    void SetItemCount(WORD count) { std::memcpy(&data_[8], &count, sizeof(count)); }

private:
    std::vector<BYTE> data_;
};

struct DialogContext {
    std::vector<DialogField>* fields;
    const DialogOptions* options;
};

// TAB 補完の状態 (入力欄ごと)。同じ入力に対して TAB を繰り返すと候補を順に巡回する。
struct CompletionState {
    std::wstring baseDir;
    bool active = false;
    std::wstring textAfter;   // 直前の補完適用後の入力 (これと一致する間だけ巡回を続ける)
    std::wstring textBefore;  // 補完適用前の入力
    CompletionResult result;
    size_t index = 0;
};

// 補完を1回進める。候補が無ければ false。
bool ApplyCompletion(HWND hEdit, CompletionState& st, bool reverse) {
    int len = GetWindowTextLengthW(hEdit);
    std::wstring text(len, L'\0');
    GetWindowTextW(hEdit, text.data(), len + 1);

    if (st.active && text == st.textAfter) {
        size_t n = st.result.candidates.size();
        st.index = reverse ? (st.index + n - 1) % n : (st.index + 1) % n;
    } else {
        DWORD selStart = 0, selEnd = 0;
        SendMessageW(hEdit, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
        st.result = ComputeFolderCompletions(text, selEnd, st.baseDir);
        st.active = false;
        if (st.result.candidates.empty()) {
            return false;
        }
        st.textBefore = text;
        st.index = reverse ? st.result.candidates.size() - 1 : 0;
        st.active = true;
    }

    const CompletionResult& r = st.result;
    std::wstring completed = st.textBefore.substr(0, r.replaceStart) + r.candidates[st.index] +
                             st.textBefore.substr(r.replaceEnd);
    SetWindowTextW(hEdit, completed.c_str());
    size_t caret = r.replaceStart + r.candidates[st.index].size();
    SendMessageW(hEdit, EM_SETSEL, caret, caret);

    st.textAfter = completed;
    if (r.candidates.size() == 1) {
        st.active = false;  // 候補が1つなら完了 (フォルダなら続けて次の TAB で中身を補完できる)
    }
    return true;
}

// パス入力欄のサブクラス: TAB で補完する。候補が無ければ通常の TAB (次の欄へ) にする。
LRESULT CALLBACK CompletionEditProc(HWND hEdit, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                    DWORD_PTR refData) {
    auto* st = reinterpret_cast<CompletionState*>(refData);
    switch (msg) {
        case WM_GETDLGCODE:
            return DefSubclassProc(hEdit, msg, wParam, lParam) | DLGC_WANTTAB;
        case WM_CHAR:
            if (wParam == L'\t') return 0;
            break;
        case WM_KEYDOWN:
            if (wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)) {
                bool reverse = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (!ApplyCompletion(hEdit, *st, reverse)) {
                    SendMessageW(GetParent(hEdit), WM_NEXTDLGCTL, reverse ? 1 : 0, 0);
                }
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hEdit, CompletionEditProc, kCompletionSubclassId);
            delete st;
            break;
        default:
            break;
    }
    return DefSubclassProc(hEdit, msg, wParam, lParam);
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* ctx = reinterpret_cast<DialogContext*>(lParam);
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
            int firstText = -1;
            for (size_t i = 0; i < ctx->fields->size(); ++i) {
                const DialogField& f = (*ctx->fields)[i];
                int id = kIdFirstField + static_cast<int>(i);
                if (f.kind == FieldKind::Check) {
                    CheckDlgButton(hDlg, id, f.value == L"1" ? BST_CHECKED : BST_UNCHECKED);
                    continue;
                }
                if (firstText < 0) firstText = id;
                SetDlgItemTextW(hDlg, id, f.value.c_str());
                if (f.maxLength > 0) {
                    SendDlgItemMessageW(hDlg, id, EM_LIMITTEXT, f.maxLength, 0);
                }
                if (f.pathCompletion) {
                    auto* st = new CompletionState();
                    st->baseDir = ctx->options->baseDir;
                    SetWindowSubclass(GetDlgItem(hDlg, id), CompletionEditProc, kCompletionSubclassId,
                                      reinterpret_cast<DWORD_PTR>(st));
                }
            }
            if (!ctx->options->hint.empty()) {
                SetDlgItemTextW(hDlg, kIdHint, ctx->options->hint.c_str());
            }
            ThemeApplyToWindow(hDlg);
            if (firstText >= 0) {
                HWND first = GetDlgItem(hDlg, firstText);
                SetFocus(first);
                SendMessageW(first, EM_SETSEL, 0, -1);
                return FALSE;  // フォーカスは自分で設定した
            }
            return TRUE;
        }
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HBRUSH brush;
            if (ThemeHandleCtlColor(msg, reinterpret_cast<HDC>(wParam), brush)) return reinterpret_cast<INT_PTR>(brush);
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == IDOK) {
                auto* ctx = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
                for (size_t i = 0; i < ctx->fields->size(); ++i) {
                    int id = kIdFirstField + static_cast<int>(i);
                    if ((*ctx->fields)[i].kind == FieldKind::Check) {
                        (*ctx->fields)[i].value = IsDlgButtonChecked(hDlg, id) == BST_CHECKED ? L"1" : L"0";
                        continue;
                    }
                    int len = GetWindowTextLengthW(GetDlgItem(hDlg, id));
                    std::wstring text(len, L'\0');
                    GetDlgItemTextW(hDlg, id, text.data(), len + 1);
                    (*ctx->fields)[i].value = text;
                }
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        default:
            break;
    }
    return FALSE;
}

}  // namespace

bool ShowFieldsDialog(HWND hwndOwner, const std::wstring& title, std::vector<DialogField>& fields,
                      const DialogOptions& options) {
    if (fields.empty()) {
        return false;
    }

    bool wide = false;
    for (const DialogField& f : fields) wide |= f.pathCompletion;
    const int editWidth = wide ? kEditWidthWide : kEditWidth;

    const int rows = static_cast<int>(fields.size());
    const int width = kMargin * 2 + kLabelWidth + editWidth;
    const int hintHeight = options.hint.empty() ? 0 : 12;
    const int height = kMargin * 2 + rows * kRowHeight + hintHeight + 22;

    TemplateBuffer buf;
    // DLGTEMPLATE ヘッダ
    buf.Dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER);
    buf.Dword(0);
    buf.Word(0);  // cdit (後で設定)
    buf.Word(0);
    buf.Word(0);
    buf.Word(static_cast<WORD>(width));
    buf.Word(static_cast<WORD>(height));
    buf.Word(0);  // メニューなし
    buf.Word(0);  // 既定のダイアログクラス
    buf.String(title);
    buf.Word(9);  // フォントサイズ (DS_SETFONT)
    buf.String(L"Yu Gothic UI");

    WORD itemCount = 0;
    for (int i = 0; i < rows; ++i) {
        int y = kMargin + i * kRowHeight;
        const DialogField& f = fields[i];
        if (f.kind == FieldKind::Check) {
            buf.Item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, static_cast<short>(kMargin),
                     static_cast<short>(y), static_cast<short>(kLabelWidth + editWidth), 12,
                     static_cast<WORD>(kIdFirstField + i), 0x0080, f.label);
            itemCount += 1;
            continue;
        }
        buf.Item(WS_CHILD | WS_VISIBLE | SS_LEFT, static_cast<short>(kMargin), static_cast<short>(y + 2),
                 static_cast<short>(kLabelWidth), 10, static_cast<WORD>(-1), 0x0082, f.label);
        buf.Item(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                 static_cast<short>(kMargin + kLabelWidth), static_cast<short>(y),
                 static_cast<short>(editWidth), 13, static_cast<WORD>(kIdFirstField + i), 0x0081, L"");
        itemCount += 2;
    }
    int bottomY = kMargin + rows * kRowHeight;
    if (!options.hint.empty()) {
        buf.Item(WS_CHILD | WS_VISIBLE | SS_LEFT, static_cast<short>(kMargin), static_cast<short>(bottomY),
                 static_cast<short>(kLabelWidth + editWidth), 10, static_cast<WORD>(kIdHint), 0x0082, L"");
        itemCount += 1;
        bottomY += hintHeight;
    }
    int buttonY = bottomY + 2;
    buf.Item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, static_cast<short>(width - kMargin - 108),
             static_cast<short>(buttonY), 50, 14, IDOK, 0x0080, L"OK");
    buf.Item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, static_cast<short>(width - kMargin - 54),
             static_cast<short>(buttonY), 50, 14, IDCANCEL, 0x0080, Tr(L"キャンセル"));
    itemCount += 2;
    buf.SetItemCount(itemCount);

    DialogContext ctx{&fields, &options};
    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(buf.Data().data()),
        hwndOwner, DialogProc, reinterpret_cast<LPARAM>(&ctx));
    return result == IDOK;
}

bool PromptText(HWND hwndOwner, const std::wstring& title, const std::wstring& label, std::wstring& value) {
    std::vector<DialogField> fields = {{label, value, 0}};
    if (!ShowFieldsDialog(hwndOwner, title, fields)) {
        return false;
    }
    value = fields[0].value;
    return true;
}

bool PromptFolder(HWND hwndOwner, const std::wstring& title, const std::wstring& label,
                  const std::wstring& baseDir, const std::wstring& initial, std::wstring& result) {
    DialogOptions options;
    options.baseDir = baseDir;
    options.hint = Tr(L"TAB でフォルダ名・エイリアス名を補完 (HOME\\lib のようにエイリアスから続けられます)");

    std::wstring text = initial;
    for (;;) {
        std::vector<DialogField> fields = {{label, text, 0, FieldKind::Text, true}};
        if (!ShowFieldsDialog(hwndOwner, title, fields, options)) {
            return false;
        }
        text = fields[0].value;
        // 前後の空白と引用符を取り除く。
        size_t b = text.find_first_not_of(L" \t\"");
        size_t e = text.find_last_not_of(L" \t\"");
        std::wstring trimmed = b == std::wstring::npos ? L"" : text.substr(b, e - b + 1);
        if (trimmed.empty()) {
            return false;
        }
        std::wstring full = ResolvePath(baseDir, ExpandAliasPath(trimmed));
        if (full.size() == 2 && full[1] == L':') full += L'\\';
        DWORD attrs = GetFileAttributesW(full.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            result = full;
            return true;
        }
        std::wstring message = Fmt(L"フォルダが見つかりません:\n{0}", {full});
        ThemedMessageBox(hwndOwner, message.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
    }
}
