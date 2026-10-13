// filM - キーボード操作中心のファイラー - エントリポイント (ウィンドウ・メッセージループ・UI の結線)
//
// 構成: キー割り当てとコマンド一覧は keyconfig.cpp、フォルダ入力の補完は completion.cpp、
// 入力ダイアログは input_dialog.cpp、エイリアス (フォルダのお気に入り) は favorites.cpp、
// ファイル操作は file_ops.cpp、「元に戻す」は undo.cpp、zip・ターミナル・プロパティ等は
// shell_actions.cpp、D&D・クリップボードは drag_drop.cpp、セッションは session.cpp に分けてあり、
// このファイルは UI (レイアウト・タブ・一覧・パネル) とコマンドの実行を担当する。
//
// 画面: 左にエイリアス一覧、中央にタブとファイル一覧、右にキー一覧 (Shift を押している間は
// Shift+キーの内容に切り替わる。クリックでもコマンドを実行できる)、下端に状態表示。
// 以前の下部のシェル部 (コマンド入力欄) は廃止し、フォルダの指定が要る操作はダイアログで行う
// (TAB でフォルダ名・エイリアス名を補完)。
//
// キー割り当ては exe と同じフォルダ\keymap.json から読み込む (初回起動時に既定値を書き出す)。
// 修飾キー無しの1キー操作はファイル一覧がフォーカスを持つ時だけ LVN_KEYDOWN で処理する
// (名前変更中の入力欄などに誤爆しないようにするため)。Ctrl 付きなどの一部のコマンドだけが
// どこでも効くアクセラレータになる。既定値は keyconfig.cpp の GetDefaultKeyBindings を参照。
//
// 永続化 (すべて exe と同じフォルダ、環境変数 FILM_CONFIG_DIR で変更可):
//   session.json   タブ (ディレクトリ・カーソル・ソート・表示モード)・ウィンドウ位置。変更のたびに
//                  デバウンス付きで保存し、起動時に無条件で復元する。複数起動した場合は最初の
//                  インスタンスだけが読み書きする (session.h の AcquirePrimaryInstance)。
//   keymap.json / favorites.json (エイリアス)
//
// セッション保存の副策: RegisterApplicationRestart、WM_QUERYENDSESSION での同期保存、
// RegisterApplicationRecoveryCallback (ハング時。UI へメッセージを送らずメモリ上の状態から保存)。
//
// 複数ファイルのマーク: Space/Insert で切替 (行の背景色 + アイコンに重ねたチェック印)。
// コピー・移動・削除・パスのコピー・ドラッグ・クリップボードなどは、マークがあればマーク全件、
// 無ければカーソル項目が対象。
//
// 未実装: テキストビューア・別ウィンドウ用のキーテーブル (機能自体が未実装)、
// 背景メニュー (フォルダの空白部分の右クリック)。

#include <windows.h>
#include <commctrl.h>
#include <imm.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <objbase.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "drag_drop.h"
#include "i18n.h"
#include "favorites.h"
#include "file_list.h"
#include "file_ops.h"
#include "input_dialog.h"
#include "keyconfig.h"
#include "marks.h"
#include "paths.h"
#include "preview_pane.h"
#include "session.h"
#include "settings.h"
#include "settings_dialog.h"
#include "shell_actions.h"
#include "theme.h"
#include "text_file_io.h"
#include "undo.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"FilMMainWindow";
constexpr wchar_t kWindowTitle[] = L"filM";
constexpr int kIdFileListView = 1001;
constexpr int kIdTabControl = 1003;
constexpr int kIdFavoritesList = 1004;
constexpr int kIdKeyPanel = 1005;
constexpr int kIdStatus = 1006;
constexpr int kIdFavBar = 1010;      // エイリアス欄の見出しバー (ラベル)
constexpr int kIdKeyBar = 1011;      // キー一覧の見出しバー (ラベル)
constexpr int kIdFavGear = 1012;     // 各バー右端の設定ボタン
constexpr int kIdTabGear = 1013;
constexpr int kIdKeyGear = 1014;
constexpr int kIdLangBtn = 1015;     // 右下の言語切り替え (JA | EN)
constexpr int kIdPreview = 1016;     // ファイル一覧の下のプレビュー欄
constexpr UINT_PTR kFavoritesListSubclassId = 2;

// 見出しバー (タブストリップ) の高さ / 設定ボタンの幅 / 状態表示の高さ / 欄の境界の幅 (96dpi 時の px)
constexpr int kBarHeight = 28;
constexpr int kStatusHeight = 24;
constexpr int kLangButtonWidth = 76;
constexpr int kSplitterWidth = 5;
constexpr int kDefaultFavoritesWidth = 180;
constexpr int kDefaultKeyPanelWidth = 280;
constexpr int kMinPaneWidth = 80;
constexpr int kMinCenterWidth = 240;
constexpr int kKeyColumnWidth = 84;

// セッション状態の保存は、変化のたびに即書き込むのではなく短いデバウンスを挟む。
constexpr UINT_PTR kSessionSaveTimerId = 9001;
constexpr UINT kSessionSaveDebounceMs = 500;
constexpr UINT_PTR kStatusClearTimerId = 9002;
constexpr UINT kStatusClearMs = 8000;
constexpr UINT_PTR kPreviewTimerId = 9003;  // カーソル移動のあと、少し待ってからプレビューを更新する
constexpr UINT kPreviewDelayMs = 120;
constexpr int kMinListHeight = 80;
constexpr int kMinPreviewHeight = 80;

constexpr UINT kMsgPanelExec = WM_APP + 1;     // キー一覧のクリック (wParam = 行)
constexpr UINT kMsgArchiveDone = WM_APP + 2;   // zip の圧縮・展開の完了 (lParam = ArchiveResult*)

// アクセラレータ (どこにフォーカスがあっても効くキー) のコマンドID。ID - kAccelBase が
// g_accelCommands の添字になる。
constexpr int kAccelBase = 3000;
std::vector<std::wstring> g_accelCommands;

// リストビューの列インデックス
enum Column { kColName = 0, kColSize, kColType, kColDate };

// 「サイズ」「項目の種類」「更新日時」列の固定幅。「名前」列は残り幅を使う。
constexpr int kSizeColWidth = 90;
constexpr int kTypeColWidth = 160;
constexpr int kDateColWidth = 130;
constexpr int kMinNameColWidth = 150;

enum class ViewMode { Detail = 0, List = 1 };

// タブごとの状態 (カレントディレクトリ・カーソル位置・ソート順・表示モード・マーク)。
// マークはセッションには保存しない (ファイル名の集合で、一時的な選択のため)。
struct TabState {
    std::wstring directory;
    std::wstring cursorItemName;  // タブ復帰時に選択を復元するためのファイル名
    SortKey sortKey = SortKey::Name;
    bool sortDescending = false;
    ViewMode viewMode = ViewMode::Detail;
    MarkSet marks;
};

// タブのセット。g_tabs / g_activeTabIndex は「いま表示しているセット」の作業用コピーで、
// セットを切り替えるとき・保存するときに g_tabSets へ書き戻す。
struct TabSet {
    std::wstring name;
    std::vector<TabState> tabs;
    int activeIndex = 0;
};

std::vector<TabState> g_tabs;
int g_activeTabIndex = 0;
std::vector<TabSet> g_tabSets;
int g_activeSet = 0;

// 欄の幅 (px。0 は未設定で、起動時に DPI に合わせた既定値にする) と、境界のドラッグ状態。
int g_favWidth = 0;
int g_keyWidth = 0;
int g_dragSplitter = 0;  // 0: ドラッグしていない / 1: エイリアス欄との境界 / 2: キー一覧との境界 / 3: プレビュー欄との境界

// プレビュー欄 (ファイル一覧の下)。V で表示/非表示、Shift+V で一覧を隠して広げる。
HWND g_preview = nullptr;
bool g_showPreview = false;
bool g_previewMax = false;
int g_previewHeight = 0;  // px。0 は未設定 (表示するときに一覧との比で決める)

// 全体で共通の UI フォント (エイリアス・タブ・一覧・状態表示)。設定ボタン用の記号フォント。
HFONT g_uiFont = nullptr;
HFONT g_gearFont = nullptr;
HACCEL g_hAccel = nullptr;

// ダークモード (配色の共通部分は theme.cpp)。
bool g_darkMode = false;
HBRUSH g_darkBrush = nullptr;  // ダーク時のコントロール背景
constexpr COLORREF kDarkBackground = RGB(32, 32, 32);
constexpr COLORREF kDarkText = RGB(230, 230, 230);

TabState& ActiveTab() {
    return g_tabs[g_activeTabIndex];
}

// 常にアクティブタブのディレクトリを指す共有の「カレントディレクトリ」。
std::wstring g_currentDir;

// ドロップターゲットのコールバックは自由関数 (関数ポインタ) である必要があるため、
// メインウィンドウ・リストビューのハンドルをグローバルに控えておく。
HWND g_mainHwnd = nullptr;
HWND g_dropTargetList = nullptr;

// 右クリックメニュー表示中だけ有効な IContextMenu2/3。シェル拡張が要求する
// WM_INITMENUPOPUP/WM_DRAWITEM/WM_MEASUREITEM/WM_MENUCHAR をメインウィンドウの
// WindowProc から転送するために、表示中だけここに控えておく。
IContextMenu2* g_activeContextMenu2 = nullptr;
IContextMenu3* g_activeContextMenu3 = nullptr;

// ファイル操作部・エイリアス一覧のうち直近でフォーカスを持っていたもの。
// メインウィンドウが (モーダルダイアログや他アプリへの切替から) 再アクティブ化された際に
// この控えを使ってフォーカスを復元する。
HWND g_lastFocusedControl = nullptr;

// エイリアス一覧 / キー一覧の表示状態。
bool g_showFavoritesPanel = true;
bool g_showKeyPanel = true;

// キー一覧を Shift+キー の内容で表示しているか (Shift を押している間 true)。
bool g_keyPanelShift = false;
// キー一覧の各行に対応するコマンド名 (現在の表示モードのもの。空なら割り当て無し)。
std::vector<std::wstring> g_panelCommands;

// 状態表示に一時的に出すメッセージ (空なら項目数・マーク数を出す)。
std::wstring g_statusMessage;

// keymap.json から読み込んだキー割り当て (起動時に一度だけ読み込む)。
std::vector<KeyBinding> g_keyBindings;

// 1キー操作のコマンドを最後に実行した時刻 (GetTickCount64)。
ULONGLONG g_commandKeyTick = 0;

// vk/修飾キーの組み合わせに一致するキー割り当てを探す。
const KeyBinding* FindKeyBindingByKey(WORD vk, bool ctrl, bool alt, bool shift) {
    for (const KeyBinding& b : g_keyBindings) {
        if (KeyNameToVirtualKey(b.key) == vk && b.ctrl == ctrl && b.alt == alt && b.shift == shift) {
            return &b;
        }
    }
    return nullptr;
}

bool EqualNoCase(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()),
                                TRUE) == CSTR_EQUAL;
}

std::wstring BuildFullPath(const std::wstring& directory, const std::wstring& name) {
    std::wstring fullPath = directory;
    if (!fullPath.empty() && fullPath.back() != L'\\') {
        fullPath += L'\\';
    }
    fullPath += name;
    return fullPath;
}

// パス末尾の区切り文字を取り除いたコピーを返す (ドライブルート "C:\" はそのまま維持)。
std::wstring TrimTrailingSlash(const std::wstring& path) {
    if (path.size() > 3 && path.back() == L'\\') {
        return path.substr(0, path.size() - 1);
    }
    return path;
}

// パスの最後のコンポーネント (名前) を取り出す。
std::wstring NameOfPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

// タブ見出し用に、パスの最後のコンポーネント (フォルダ名) を取り出す。
// ドライブルートの場合は "C:\" のような表記のまま返す。
std::wstring GetLastPathComponent(const std::wstring& path) {
    std::wstring trimmed = TrimTrailingSlash(path);
    size_t pos = trimmed.find_last_of(L'\\');
    if (pos == std::wstring::npos || pos + 1 >= trimmed.size()) {
        return path;  // ドライブルート等
    }
    return trimmed.substr(pos + 1);
}

// 親ディレクトリのパスを返す。既にドライブルートの場合は同じパスを返す。
std::wstring GetParentDirectory(const std::wstring& path) {
    std::wstring trimmed = TrimTrailingSlash(path);
    size_t pos = trimmed.find_last_of(L'\\');
    if (pos == std::wstring::npos) {
        return path;
    }
    if (pos == 0 || (pos == 2 && trimmed[1] == L':')) {
        return trimmed.substr(0, pos + 1);  // "C:\" を残す
    }
    return trimmed.substr(0, pos);
}

bool IsDirectoryPath(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// ファイル名として使えるか (パス区切り・予約文字・空・末尾の空白/ピリオドを除く)。
bool IsValidFileName(const std::wstring& name) {
    if (name.empty() || name == L"." || name == L"..") return false;
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return false;
    return name.back() != L' ' && name.back() != L'.';
}

void ShowMessage(HWND hwnd, const std::wstring& text, UINT icon = MB_ICONINFORMATION) {
    ThemedMessageBox(hwnd, text.c_str(), kWindowTitle, MB_OK | icon);
}

// 状態表示の更新・プレビューの更新予約 (定義は後述)。
void UpdateStatusBar(HWND hwnd);
void SchedulePreviewUpdate(HWND hwnd);

void UpdateWindowTitle(HWND hwnd) {
    std::wstring title = std::wstring(kWindowTitle) + L" - " + g_currentDir;
    if (g_tabSets.size() > 1 && g_activeSet >= 0 && g_activeSet < static_cast<int>(g_tabSets.size())) {
        title += L"  <" + g_tabSets[g_activeSet].name + L">";
    }
    if (!g_tabs.empty() && ActiveTab().marks.Count() > 0) {
        title += L"  [" + Fmt(L"{0} 件マーク", {Num(ActiveTab().marks.Count())}) + L"]";
    }
    SetWindowTextW(hwnd, title.c_str());
    UpdateStatusBar(hwnd);
}

// 状態表示に一時的なメッセージを出す (しばらくすると項目数の表示に戻る)。
void SetStatusMessage(HWND hwnd, const std::wstring& text) {
    g_statusMessage = text;
    UpdateStatusBar(hwnd);
    SetTimer(hwnd, kStatusClearTimerId, kStatusClearMs, nullptr);
}

void UpdateStatusBar(HWND hwnd) {
    HWND hStatus = GetDlgItem(hwnd, kIdStatus);
    if (!hStatus) return;
    std::wstring text = g_statusMessage;
    if (text.empty()) {
        HWND hList = GetDlgItem(hwnd, kIdFileListView);
        int count = hList ? ListView_GetItemCount(hList) : 0;
        text = Fmt(L"{0} 項目", {Num(count)});
        if (g_tabSets.size() > 1 && g_activeSet >= 0 && g_activeSet < static_cast<int>(g_tabSets.size())) {
            text = L"[" + g_tabSets[g_activeSet].name + L"]  " + text;
        }
        if (!g_tabs.empty() && ActiveTab().marks.Count() > 0) {
            text += L"  /  " + Fmt(L"{0} 件マーク", {Num(ActiveTab().marks.Count())});
        }
    }
    SetWindowTextW(hStatus, (L" " + text).c_str());
}

// hList への WM_DROPFILES (DragAcceptFiles/外部からのファイルドロップ) を処理する
// サブクラスプロシージャ。定義は後述 (OnFilesDropped の直後)。
LRESULT CALLBACK FileListSubclassProc(HWND hList, UINT msg, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR /*subclassId*/, DWORD_PTR /*refData*/);
constexpr UINT_PTR kFileListSubclassId = 3;

// 一覧の列見出しを現在の言語で付け直す。
void SetFileListColumnTexts(HWND hList) {
    const wchar_t* texts[] = {Tr(L"名前"), Tr(L"サイズ"), Tr(L"項目の種類"), Tr(L"更新日時")};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT;
        col.pszText = const_cast<LPWSTR>(texts[i]);
        ListView_SetColumn(hList, i, &col);
    }
}

HWND CreateFileListView(HWND hwndParent, HINSTANCE hInstance) {
    HWND hList = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_EDITLABELS,
        0, 0, 0, 0,
        hwndParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFileListView)),
        hInstance, nullptr);

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);

    // 1キー操作を IME に横取りされないよう、一覧では IME を無効にする
    // (名前変更の入力欄は別のウィンドウなので、そちらでは日本語入力できる)。
    ImmAssociateContext(hList, nullptr);

    // Explorer 等からのファイルドロップを受け付ける (WM_DROPFILES)。
    DragAcceptFiles(hList, TRUE);
    SetWindowSubclass(hList, FileListSubclassProc, kFileListSubclassId, 0);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

    col.fmt = LVCFMT_LEFT;
    col.pszText = const_cast<LPWSTR>(Tr(L"名前"));
    col.cx = kMinNameColWidth;
    ListView_InsertColumn(hList, kColName, &col);

    col.fmt = LVCFMT_RIGHT;
    col.pszText = const_cast<LPWSTR>(Tr(L"サイズ"));
    col.cx = kSizeColWidth;
    ListView_InsertColumn(hList, kColSize, &col);

    col.fmt = LVCFMT_LEFT;
    col.pszText = const_cast<LPWSTR>(Tr(L"項目の種類"));
    col.cx = kTypeColWidth;
    ListView_InsertColumn(hList, kColType, &col);

    col.pszText = const_cast<LPWSTR>(Tr(L"更新日時"));
    col.cx = kDateColWidth;
    ListView_InsertColumn(hList, kColDate, &col);

    return hList;
}

void ApplyViewMode(HWND hList);
void UpdateSortIndicator(HWND hList);

void PopulateFileListView(HWND hList, const std::wstring& directory) {
    ApplyViewMode(hList);
    ListView_DeleteAllItems(hList);

    HIMAGELIST hSysImageList = nullptr;
    SHFILEINFOW shfi{};
    hSysImageList = reinterpret_cast<HIMAGELIST>(
        SHGetFileInfoW(L"", 0, &shfi, sizeof(shfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
    if (hSysImageList) {
        ListView_SetImageList(hList, hSysImageList, LVSIL_SMALL);
    }

    std::vector<FileEntry> entries;
    if (g_tabs.empty()) {
        entries = EnumerateDirectory(directory);
    } else {
        const TabState& tab = ActiveTab();
        entries = EnumerateDirectory(directory, tab.sortKey, tab.sortDescending);
    }

    int index = 0;
    for (const FileEntry& entry : entries) {
        std::wstring fullPath = BuildFullPath(directory, entry.name);

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = index;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(entry.name.c_str());
        item.iImage = GetFileIconIndex(fullPath, entry.isDirectory);
        ListView_InsertItem(hList, &item);

        std::wstring sizeText = FormatFileSize(entry.size, entry.isDirectory);
        ListView_SetItemText(hList, index, kColSize, const_cast<LPWSTR>(sizeText.c_str()));

        std::wstring typeText = GetFileTypeName(fullPath, entry.isDirectory);
        if (entry.isCloudPlaceholder) {
            typeText += Tr(L" (クラウド)");  // OneDrive 等: 開く際にダウンロードが走る
        }
        ListView_SetItemText(hList, index, kColType, const_cast<LPWSTR>(typeText.c_str()));

        std::wstring dateText = FormatFileTime(entry.lastWriteTime);
        ListView_SetItemText(hList, index, kColDate, const_cast<LPWSTR>(dateText.c_str()));

        ++index;
    }
    UpdateSortIndicator(hList);
    SchedulePreviewUpdate(GetParent(hList));  // 空のフォルダでも、プレビューを更新する
}

// 現在のアクティブタブのソート状態を列見出しの矢印 (▲/▼) に反映する。
void UpdateSortIndicator(HWND hList) {
    HWND hHeader = ListView_GetHeader(hList);
    if (!hHeader || g_tabs.empty()) {
        return;
    }
    const TabState& tab = ActiveTab();
    int sortedColumn = kColName;
    switch (tab.sortKey) {
        case SortKey::Name: sortedColumn = kColName; break;
        case SortKey::Size: sortedColumn = kColSize; break;
        case SortKey::Ext: sortedColumn = kColType; break;
        case SortKey::Date: sortedColumn = kColDate; break;
    }
    for (int i = 0; i < 4; ++i) {
        HDITEMW hdi{};
        hdi.mask = HDI_FORMAT;
        Header_GetItem(hHeader, i, &hdi);
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == sortedColumn) {
            hdi.fmt |= tab.sortDescending ? HDF_SORTDOWN : HDF_SORTUP;
        }
        Header_SetItem(hHeader, i, &hdi);
    }
}

// アクティブタブの表示モード (詳細 / 一覧) をリストビューに反映する。
void ApplyViewMode(HWND hList) {
    if (g_tabs.empty()) {
        return;
    }
    LONG_PTR style = GetWindowLongPtrW(hList, GWL_STYLE);
    LONG_PTR wanted = ActiveTab().viewMode == ViewMode::List ? LVS_LIST : LVS_REPORT;
    if ((style & LVS_TYPEMASK) != wanted) {
        SetWindowLongPtrW(hList, GWL_STYLE, (style & ~static_cast<LONG_PTR>(LVS_TYPEMASK)) | wanted);
    }
}

// ディレクトリ移動 (定義は後述)。
void NavigateToDirectory(HWND hwnd, HWND hList, const std::wstring& newDir,
                          const std::wstring& selectItemName);

// セッション状態 (タブ・ウィンドウ位置) の保存をデバウンス付きで予約する (定義は後述)。
void MarkSessionDirty(HWND hwnd);

// Explorer標準の右クリックメニューを表示する (定義は後述)。
void ShowFileContextMenu(HWND hwndOwner, HWND hList, int screenX, int screenY);

// コマンド名で指定されたコマンドを実行する (定義は後述)。
void ExecuteCommand(HWND hwnd, const std::wstring& command);

// タブは全タブ共通のクリックで切替わるだけの単純なストリップ。
// TCS_FOCUSNEVER でクリックしてもキーボードフォーカスを奪わないようにする。
LRESULT CALLBACK TabStripSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

HWND CreateTabStrip(HWND hwndParent, HINSTANCE hInstance) {
    HWND h = CreateWindowExW(
        0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | TCS_FOCUSNEVER | TCS_TOOLTIPS,
        0, 0, 0, 0,
        hwndParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTabControl)),
        hInstance, nullptr);
    SetWindowSubclass(h, TabStripSubclassProc, 4, 0);
    return h;
}

// タブストリップの描画 (ライト/ダーク共通の見た目にするため自前で描く)。
LRESULT CALLBACK TabStripSubclassProc(HWND hTab, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                      DWORD_PTR /*refData*/) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hTab, &ps);
            RECT rc;
            GetClientRect(hTab, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            HGDIOBJ oldFont = SelectObject(mem, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));

            HBRUSH barBrush = CreateSolidBrush(ThemeBarBg());
            FillRect(mem, &rc, barBrush);
            DeleteObject(barBrush);

            int dpi = static_cast<int>(GetDpiForWindow(hTab));
            int count = TabCtrl_GetItemCount(hTab);
            int sel = TabCtrl_GetCurSel(hTab);
            SetBkMode(mem, TRANSPARENT);
            for (int i = 0; i < count; ++i) {
                RECT r;
                if (!TabCtrl_GetItemRect(hTab, i, &r)) continue;
                r.top = 0;
                r.bottom = rc.bottom;
                bool selected = i == sel;
                HBRUSH fill = CreateSolidBrush(selected ? ThemeWindowBg() : ThemeBarBg());
                FillRect(mem, &r, fill);
                DeleteObject(fill);
                if (selected) {  // 選択中のタブの上端にアクセントの線
                    RECT accent{r.left, 0, r.right, std::max(2, MulDiv(2, dpi, 96))};
                    HBRUSH ab = CreateSolidBrush(RGB(0, 120, 215));
                    FillRect(mem, &accent, ab);
                    DeleteObject(ab);
                } else {  // 区切り線
                    RECT sep{r.right - 1, r.top + 5, r.right, r.bottom - 5};
                    HBRUSH sb = CreateSolidBrush(ThemeBarHot());
                    FillRect(mem, &sep, sb);
                    DeleteObject(sb);
                }
                wchar_t text[260] = L"";
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = text;
                item.cchTextMax = 260;
                TabCtrl_GetItem(hTab, i, &item);
                SetTextColor(mem, selected ? ThemeText() : ThemeBarText());
                RECT tr = r;
                tr.left += MulDiv(8, dpi, 96);
                tr.right -= MulDiv(8, dpi, 96);
                DrawTextW(mem, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldFont);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hTab, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            // 描画でタブを上下に広げているので、クリックも横位置だけで判定する。
            int x = GET_X_LPARAM(lParam);
            int count = TabCtrl_GetItemCount(hTab);
            for (int i = 0; i < count; ++i) {
                RECT r;
                if (TabCtrl_GetItemRect(hTab, i, &r) && x >= r.left && x < r.right) {
                    if (i != TabCtrl_GetCurSel(hTab)) {
                        TabCtrl_SetCurSel(hTab, i);
                        NMHDR nm{hTab, static_cast<UINT_PTR>(GetDlgCtrlID(hTab)), TCN_SELCHANGE};
                        SendMessageW(GetParent(hTab), WM_NOTIFY, nm.idFrom, reinterpret_cast<LPARAM>(&nm));
                    }
                    return 0;
                }
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hTab, TabStripSubclassProc, subclassId);
            break;
    }
    return DefSubclassProc(hTab, msg, wParam, lParam);
}

// エイリアス一覧の中身を、現在の登録内容で作り直す。
void RefreshFavoritesListBox(HWND hFav) {
    int topIndex = static_cast<int>(SendMessageW(hFav, LB_GETTOPINDEX, 0, 0));
    int cursor = static_cast<int>(SendMessageW(hFav, LB_GETCURSEL, 0, 0));
    SendMessageW(hFav, LB_RESETCONTENT, 0, 0);
    for (const FavoriteEntry& fav : GetFavorites()) {
        // 呼び出しキーは Shift を押している間だけ "[W] Windows" のように先頭に表示する。
        std::wstring label = (g_keyPanelShift && fav.key) ? L"[" + std::wstring(1, fav.key) + L"] " + fav.name : fav.name;
        SendMessageW(hFav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(hFav, LB_SETTOPINDEX, topIndex, 0);
    int count = static_cast<int>(SendMessageW(hFav, LB_GETCOUNT, 0, 0));
    if (cursor >= 0 && count > 0) {
        SendMessageW(hFav, LB_SETCURSEL, std::min(cursor, count - 1), 0);
    }
}

LRESULT CALLBACK FavoritesListSubclassProc(HWND hFav, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR /*subclassId*/, DWORD_PTR /*refData*/);

HWND CreateFavoritesListBox(HWND hwndParent, HINSTANCE hInstance) {
    HWND hFav = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | (g_showFavoritesPanel ? WS_VISIBLE : 0) | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS |
            LBS_NOINTEGRALHEIGHT,
        0, 0, 0, 0,
        hwndParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFavoritesList)),
        hInstance, nullptr);

    SetWindowSubclass(hFav, FavoritesListSubclassProc, kFavoritesListSubclassId, 0);
    return hFav;
}

// ---- 右側のキー一覧 ----

HWND CreateKeyPanel(HWND hwndParent, HINSTANCE hInstance) {
    HWND h = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | (g_showKeyPanel ? WS_VISIBLE : 0) | WS_BORDER | LVS_REPORT | LVS_SINGLESEL |
            LVS_NOSORTHEADER,
        0, 0, 0, 0,
        hwndParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdKeyPanel)),
        hInstance, nullptr);
    ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_ONECLICKACTIVATE);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    col.pszText = const_cast<LPWSTR>(Tr(L"キー"));
    col.cx = MulDiv(kKeyColumnWidth, static_cast<int>(GetDpiForWindow(hwndParent)), 96);
    ListView_InsertColumn(h, 0, &col);
    col.pszText = const_cast<LPWSTR>(Tr(L"機能"));
    col.cx = 160;  // 実際の幅は ResizeLayout で欄の幅に合わせる
    ListView_InsertColumn(h, 1, &col);

    const std::vector<PanelKey>& keys = GetPanelKeys();
    for (size_t i = 0; i < keys.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(L"");
        ListView_InsertItem(h, &item);
    }
    return h;
}

// キー一覧の内容を、現在の表示モード (通常 / Shift) で書き直す。
void RefreshKeyPanel(HWND hwnd) {
    HWND hKeys = GetDlgItem(hwnd, kIdKeyPanel);
    if (!hKeys) return;
    const std::vector<PanelKey>& keys = GetPanelKeys();
    g_panelCommands.assign(keys.size(), L"");

    SendMessageW(hKeys, WM_SETREDRAW, FALSE, 0);
    for (size_t i = 0; i < keys.size(); ++i) {
        std::wstring command = FindCommandForKey(g_keyBindings, keys[i].key, g_keyPanelShift);
        g_panelCommands[i] = command;
        std::wstring keyText = (g_keyPanelShift ? L"Shift+" : L"") + std::wstring(keys[i].display);
        std::wstring label = L"---";
        if (!command.empty()) {
            label = GetCommandLabel(command);
            if (label.empty()) label = command;
        }
        ListView_SetItemText(hKeys, static_cast<int>(i), 0, const_cast<LPWSTR>(keyText.c_str()));
        ListView_SetItemText(hKeys, static_cast<int>(i), 1, const_cast<LPWSTR>(label.c_str()));
    }
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT;
    col.pszText = const_cast<LPWSTR>(g_keyPanelShift ? Tr(L"Shift+キー") : Tr(L"キー"));
    ListView_SetColumn(hKeys, 0, &col);
    SendMessageW(hKeys, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hKeys, nullptr, TRUE);
}

void RefreshFavoritesListBox(HWND hFav);

void SetKeyPanelShift(HWND hwnd, bool shift) {
    if (g_keyPanelShift == shift) return;
    g_keyPanelShift = shift;
    RefreshKeyPanel(hwnd);
    RefreshFavoritesListBox(GetDlgItem(hwnd, kIdFavoritesList));  // 呼び出しキーの表示を切り替える
}

// 欄の境界 (スプリッター) の現在位置。ResizeLayout が更新する。非表示なら x が -1。
struct SplitterRects {
    int favX = -1;   // エイリアス欄とファイル一覧の境界の左端
    int keyX = -1;   // ファイル一覧とキー一覧の境界の左端
    int width = 0;
    int height = 0;  // 境界の縦の長さ (状態表示の上まで)
    int prevY = -1;  // ファイル一覧とプレビュー欄の境界の上端 (非表示なら -1)
    int prevX = 0;   // その境界の左端と幅 (中央の列と同じ)
    int prevW = 0;
};
SplitterRects g_splitters;

int ScaleForWindow(HWND hwnd, int px) {
    return MulDiv(px, static_cast<int>(GetDpiForWindow(hwnd)), 96);
}

// 各欄の幅を、ウィンドウの幅に収まるよう補正した値で返す (表示していない欄は 0)。
void ComputePaneWidths(HWND hwnd, int clientWidth, int& favWidth, int& keyWidth) {
    int split = ScaleForWindow(hwnd, kSplitterWidth);
    int minPane = ScaleForWindow(hwnd, kMinPaneWidth);
    int minCenter = ScaleForWindow(hwnd, kMinCenterWidth);
    if (g_favWidth <= 0) g_favWidth = ScaleForWindow(hwnd, kDefaultFavoritesWidth);
    if (g_keyWidth <= 0) g_keyWidth = ScaleForWindow(hwnd, kDefaultKeyPanelWidth);

    favWidth = g_showFavoritesPanel ? std::max(minPane, g_favWidth) : 0;
    keyWidth = g_showKeyPanel ? std::max(minPane, g_keyWidth) : 0;
    int splitters = (g_showFavoritesPanel ? split : 0) + (g_showKeyPanel ? split : 0);
    int excess = favWidth + keyWidth + splitters + minCenter - clientWidth;
    if (excess > 0) {  // 狭いウィンドウでは両方の欄を縮める
        int shrinkFav = std::min(excess / 2 + excess % 2, std::max(0, favWidth - minPane));
        favWidth -= shrinkFav;
        excess -= shrinkFav;
        keyWidth -= std::min(excess, std::max(0, keyWidth - minPane));
    }
}

void ResizeLayout(HWND hwnd, int clientWidth, int clientHeight) {
    HWND hFav = GetDlgItem(hwnd, kIdFavoritesList);
    HWND hTab = GetDlgItem(hwnd, kIdTabControl);
    HWND hList = GetDlgItem(hwnd, kIdFileListView);
    HWND hKeys = GetDlgItem(hwnd, kIdKeyPanel);
    HWND hStatus = GetDlgItem(hwnd, kIdStatus);
    HWND hFavBar = GetDlgItem(hwnd, kIdFavBar);
    HWND hKeyBar = GetDlgItem(hwnd, kIdKeyBar);
    HWND hFavGear = GetDlgItem(hwnd, kIdFavGear);
    HWND hTabGear = GetDlgItem(hwnd, kIdTabGear);
    HWND hKeyGear = GetDlgItem(hwnd, kIdKeyGear);
    HWND hLang = GetDlgItem(hwnd, kIdLangBtn);
    if (!hLang) return;
    if (!hFav || !hTab || !hList || !hKeys || !hStatus || !hFavBar || !hKeyBar || !hFavGear || !hTabGear ||
        !hKeyGear) {
        return;
    }

    int bar = ScaleForWindow(hwnd, kBarHeight);
    int split = ScaleForWindow(hwnd, kSplitterWidth);
    int statusHeight = ScaleForWindow(hwnd, kStatusHeight);
    int bodyHeight = std::max(0, clientHeight - statusHeight);
    int langWidth = std::min(ScaleForWindow(hwnd, kLangButtonWidth), clientWidth);
    MoveWindow(hStatus, 0, bodyHeight, std::max(0, clientWidth - langWidth), std::min(statusHeight, clientHeight), TRUE);
    MoveWindow(hLang, std::max(0, clientWidth - langWidth), bodyHeight, langWidth, std::min(statusHeight, clientHeight), TRUE);

    int favWidth = 0, keyWidth = 0;
    ComputePaneWidths(hwnd, clientWidth, favWidth, keyWidth);
    int listHeight = std::max(0, bodyHeight - bar);

    for (HWND h : {hFav, hFavBar, hFavGear}) ShowWindow(h, g_showFavoritesPanel ? SW_SHOW : SW_HIDE);
    if (g_showFavoritesPanel) {
        MoveWindow(hFavBar, 0, 0, std::max(0, favWidth - bar), bar, TRUE);
        MoveWindow(hFavGear, std::max(0, favWidth - bar), 0, bar, bar, TRUE);
        MoveWindow(hFav, 0, bar, favWidth, listHeight, TRUE);
    }

    for (HWND h : {hKeys, hKeyBar, hKeyGear}) ShowWindow(h, g_showKeyPanel ? SW_SHOW : SW_HIDE);
    int keyX = std::max(0, clientWidth - keyWidth);
    if (g_showKeyPanel) {
        MoveWindow(hKeyBar, keyX, 0, std::max(0, keyWidth - bar), bar, TRUE);
        MoveWindow(hKeyGear, keyX + std::max(0, keyWidth - bar), 0, bar, bar, TRUE);
        MoveWindow(hKeys, keyX, bar, keyWidth, listHeight, TRUE);
        ListView_SetColumnWidth(hKeys, 1, LVSCW_AUTOSIZE_USEHEADER);  // 最終列が残りの幅いっぱいになる
    }

    int centerX = g_showFavoritesPanel ? favWidth + split : 0;
    int centerRight = g_showKeyPanel ? keyX - split : clientWidth;
    int centerWidth = std::max(0, centerRight - centerX);

    g_splitters.width = split;
    g_splitters.height = bodyHeight;
    g_splitters.favX = g_showFavoritesPanel ? favWidth : -1;
    g_splitters.keyX = g_showKeyPanel ? keyX - split : -1;

    MoveWindow(hTab, centerX, 0, std::max(0, centerWidth - bar), bar, TRUE);
    MoveWindow(hTabGear, centerX + std::max(0, centerWidth - bar), 0, bar, bar, TRUE);

    // 中央の列の縦の割り振り: ファイル一覧 / 境界 / プレビュー欄。
    int listH = listHeight;
    g_splitters.prevY = -1;
    if (g_preview) {
        ShowWindow(g_preview, g_showPreview ? SW_SHOW : SW_HIDE);
        ShowWindow(hList, (g_showPreview && g_previewMax) ? SW_HIDE : SW_SHOW);
    }
    if (g_preview && g_showPreview) {
        int minList = ScaleForWindow(hwnd, kMinListHeight);
        int minPreview = ScaleForWindow(hwnd, kMinPreviewHeight);
        if (g_previewMax) {
            MoveWindow(g_preview, centerX, bar, centerWidth, listHeight, TRUE);
        } else {
            if (g_previewHeight <= 0) g_previewHeight = listHeight * 2 / 5;
            int previewH = std::clamp(g_previewHeight, minPreview, std::max(minPreview, listHeight - split - minList));
            listH = std::max(0, listHeight - previewH - split);
            MoveWindow(g_preview, centerX, bar + listH + split, centerWidth, std::max(0, listHeight - listH - split), TRUE);
            g_splitters.prevY = bar + listH;
            g_splitters.prevX = centerX;
            g_splitters.prevW = centerWidth;
        }
    }
    MoveWindow(hList, centerX, bar, centerWidth, listH, TRUE);

    int nameWidth = centerWidth - (kSizeColWidth + kTypeColWidth + kDateColWidth);
    if (nameWidth < kMinNameColWidth) {
        nameWidth = kMinNameColWidth;
    }
    if (g_tabs.empty() || ActiveTab().viewMode == ViewMode::Detail) {
        ListView_SetColumnWidth(hList, kColName, nameWidth);
    }
}

void RelayoutNow(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    ResizeLayout(hwnd, rc.right, rc.bottom);
}

// ファイル一覧へフォーカスを移す。
void FocusList(HWND hwnd) {
    HWND hList = GetDlgItem(hwnd, kIdFileListView);
    SetFocus(hList);
    g_lastFocusedControl = hList;
}

// エイリアス一覧へフォーカスを移す (非表示なら表示する)。
void FocusAliasList(HWND hwnd) {
    HWND hFav = GetDlgItem(hwnd, kIdFavoritesList);
    if (!g_showFavoritesPanel) {
        g_showFavoritesPanel = true;
        RelayoutNow(hwnd);
    }
    if (SendMessageW(hFav, LB_GETCURSEL, 0, 0) == LB_ERR && SendMessageW(hFav, LB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(hFav, LB_SETCURSEL, 0, 0);
    }
    SetFocus(hFav);
    g_lastFocusedControl = hFav;
}

// 一覧⇔エイリアス一覧のフォーカスをその場で切替える。
void ToggleFocusBetweenListAndAliases(HWND hwnd) {
    HWND hFav = GetDlgItem(hwnd, kIdFavoritesList);
    if (GetFocus() == hFav) {
        FocusList(hwnd);
    } else {
        FocusAliasList(hwnd);
    }
}

// 現在カーソルが合っている項目のインデックスを返す。無ければ -1。
int GetCursorItemIndex(HWND hList) {
    int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (index < 0) {
        index = ListView_GetNextItem(hList, -1, LVNI_FOCUSED);
    }
    return index;
}

std::wstring GetItemName(HWND hList, int index) {
    wchar_t buf[MAX_PATH];
    ListView_GetItemText(hList, index, kColName, buf, MAX_PATH);
    return buf;
}

void SelectItem(HWND hList, int index) {
    ListView_SetItemState(hList, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(hList, index, FALSE);
}

void RefreshFileListView(HWND hList, const std::wstring& previousItemName) {
    int oldIndex = GetCursorItemIndex(hList);  // 見つからないとき (削除後など) はこの位置に近い項目へ
    PopulateFileListView(hList, g_currentDir);

    // 削除・移動・リネームでなくなったファイルのマークを外す。
    if (!g_tabs.empty()) {
        ActiveTab().marks.RemoveIf([](const std::wstring& name) {
            return GetFileAttributesW(BuildFullPath(g_currentDir, name).c_str()) == INVALID_FILE_ATTRIBUTES;
        });
    }
    UpdateWindowTitle(GetParent(hList));

    // 操作前にカーソルがあった項目 (削除・移動時は次の項目) にできるだけ近い位置へ戻す。
    int count = ListView_GetItemCount(hList);
    int restoreIndex = std::max(oldIndex, 0);
    for (int i = 0; i < count; ++i) {
        if (EqualNoCase(GetItemName(hList, i), previousItemName)) {
            restoreIndex = i;
            break;
        }
    }
    if (count > 0) {
        SelectItem(hList, std::min(restoreIndex, count - 1));
    }
    SchedulePreviewUpdate(GetParent(hList));
}

// 操作対象のフルパス一覧。マークがあれば (一覧の表示順で) マークされた項目全て、
// 無ければカーソル項目だけ。
std::vector<std::wstring> GatherTargets(HWND hList) {
    std::vector<std::wstring> paths;
    if (!g_tabs.empty() && ActiveTab().marks.Count() > 0) {
        int count = ListView_GetItemCount(hList);
        for (int i = 0; i < count; ++i) {
            std::wstring name = GetItemName(hList, i);
            if (ActiveTab().marks.Contains(name)) {
                paths.push_back(BuildFullPath(g_currentDir, name));
            }
        }
        return paths;
    }
    int index = GetCursorItemIndex(hList);
    if (index >= 0) {
        paths.push_back(BuildFullPath(g_currentDir, GetItemName(hList, index)));
    }
    return paths;
}

// 現在カーソルのある項目名 (無ければ空)。操作後に近い位置へカーソルを戻すために使う。
std::wstring CursorItemName(HWND hList) {
    int index = GetCursorItemIndex(hList);
    return index >= 0 ? GetItemName(hList, index) : L"";
}

// カーソル項目のフルパス (無ければ空)。
std::wstring CursorItemPath(HWND hList) {
    int index = GetCursorItemIndex(hList);
    return index >= 0 ? BuildFullPath(g_currentDir, GetItemName(hList, index)) : L"";
}

// ---- プレビュー欄 (画像・Markdown・テキストの表示と内蔵エディタ。本体は preview_pane.cpp) ----

void UpdatePreviewNow(HWND hwnd) {
    if (!g_preview || !g_showPreview) return;
    PreviewPaneShowFile(CursorItemPath(GetDlgItem(hwnd, kIdFileListView)));
}

// カーソルが動いたら、短い間をおいてからプレビューを更新する (キーを押しっぱなしで連続して読み込まないため)。
void SchedulePreviewUpdate(HWND hwnd) {
    if (g_showPreview) SetTimer(hwnd, kPreviewTimerId, kPreviewDelayMs, nullptr);
}

bool FocusIsInPreview() {
    HWND f = GetFocus();
    return g_preview && f && (f == g_preview || IsChild(g_preview, f));
}

void SetPreviewVisible(HWND hwnd, bool show) {
    if (!g_preview) return;
    if (!show) {
        bool hadFocus = FocusIsInPreview();
        g_showPreview = false;
        g_previewMax = false;
        KillTimer(hwnd, kPreviewTimerId);
        RelayoutNow(hwnd);
        if (hadFocus) FocusList(hwnd);
        return;
    }
    g_showPreview = true;
    RelayoutNow(hwnd);
    UpdatePreviewNow(hwnd);
}

// 一覧のカーソルを前後へ動かす (画像のプレビューを ← → で切り替えるとき)。画像を見ているときは次の画像まで飛ばす。
void StepPreviewCursor(HWND hwnd, int delta) {
    HWND hList = GetDlgItem(hwnd, kIdFileListView);
    int count = ListView_GetItemCount(hList);
    int cur = GetCursorItemIndex(hList);
    if (cur < 0 || count <= 0 || delta == 0) return;
    int step = delta < 0 ? -1 : 1;
    int remaining = delta < 0 ? -delta : delta;
    bool imagesOnly = IsImageFile(GetItemName(hList, cur));
    int index = cur;
    while (remaining > 0) {
        int next = index + step;
        while (next >= 0 && next < count && imagesOnly && !IsImageFile(GetItemName(hList, next))) next += step;
        if (next < 0 || next >= count) break;
        index = next;
        --remaining;
    }
    if (index != cur) {
        ListView_SetItemState(hList, -1, 0, LVIS_SELECTED);
        SelectItem(hList, index);
    }
}

// ---- ファイル操作 (コピー・移動・削除など。「元に戻す」の記録もここで行う) ----

// sources を destDir へコピー/移動する共通処理。完了後に一覧を更新する。
void RunTransfer(HWND hwnd, HWND hList, const std::vector<std::wstring>& sources,
                 const std::wstring& destDir, bool isMove) {
    if (sources.empty()) return;
    if (isMove && EqualNoCase(TrimTrailingSlash(destDir), TrimTrailingSlash(g_currentDir))) {
        SetStatusMessage(hwnd, Tr(L"同じフォルダへは移動できません。"));
        return;
    }
    std::wstring cursorName = CursorItemName(hList);

    // 宛先に同名のものが既にあるか (上書きされたものは元に戻せないので記録から外す)。
    std::vector<std::wstring> preexisting;
    for (const std::wstring& src : sources) {
        std::wstring p = BuildFullPath(destDir, NameOfPath(src));
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) preexisting.push_back(p);
    }

    std::vector<OpPair> done;
    bool ok = isMove ? MoveItems(hwnd, sources, destDir, &done) : CopyItems(hwnd, sources, destDir, &done);

    if (isMove) {
        PushUndoMoved(Tr(L"移動"), done);
    } else {
        std::vector<std::wstring> created;
        for (const OpPair& p : done) {
            bool overwritten = false;
            for (const std::wstring& e : preexisting) overwritten |= EqualNoCase(e, p.to);
            if (!p.to.empty() && !overwritten) created.push_back(p.to);
        }
        PushUndoCreated(Tr(L"コピー"), created);
    }
    RefreshFileListView(hList, cursorName);
    SetStatusMessage(hwnd, ok ? Fmt(isMove ? L"{0} 件を移動しました: {1}" : L"{0} 件をコピーしました: {1}", {Num(done.size()), destDir})
                              : Fmt(isMove ? L"移動は完了しませんでした ({0} 件)。" : L"コピーは完了しませんでした ({0} 件)。", {Num(done.size())}));
}

// 他のタブのフォルダを宛先として選ぶ。他のタブが1つならそのフォルダ、複数ならメニューで選ぶ。
// 他のタブが無い場合は noOtherTab を true にして空文字列を返す。キャンセル時も空文字列。
std::wstring PickOtherTabDirectory(HWND hwndOwner, HWND hList, bool& noOtherTab) {
    constexpr UINT kTabMenuIdBase = 9001;
    noOtherTab = false;

    std::vector<std::wstring> tabDirs;
    for (int i = 0; i < static_cast<int>(g_tabs.size()); ++i) {
        if (i != g_activeTabIndex) tabDirs.push_back(g_tabs[i].directory);
    }
    if (tabDirs.empty()) {
        noOtherTab = true;
        return L"";
    }
    if (tabDirs.size() == 1) {
        return tabDirs[0];
    }

    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < tabDirs.size(); ++i) {
        std::wstring label = GetLastPathComponent(tabDirs[i]) + L"  —  " + tabDirs[i];
        AppendMenuW(menu, MF_STRING, kTabMenuIdBase + i, label.c_str());
    }

    // キーボード操作からの呼び出しなので、カーソル項目の画面上の位置にメニューを出す。
    POINT pt{0, 0};
    int cursorIndex = GetCursorItemIndex(hList);
    RECT itemRect;
    if (cursorIndex >= 0 && ListView_GetItemRect(hList, cursorIndex, &itemRect, LVIR_BOUNDS)) {
        pt = {itemRect.left, itemRect.bottom};
        ClientToScreen(hList, &pt);
    } else {
        GetCursorPos(&pt);
    }
    UINT selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                      pt.x, pt.y, hwndOwner, nullptr);
    PostMessageW(hwndOwner, WM_NULL, 0, 0);  // メニューが正しく閉じるようにする (MSDN推奨)
    DestroyMenu(menu);

    if (selected < kTabMenuIdBase) return L"";
    size_t index = selected - kTabMenuIdBase;
    return index < tabDirs.size() ? tabDirs[index] : L"";
}

// フォルダ指定ダイアログで宛先を選ぶ。初期値は他のタブのフォルダ (無ければカレント)。
std::wstring PromptDestination(HWND hwnd, bool isMove) {
    std::wstring initial = g_currentDir;
    for (int i = 0; i < static_cast<int>(g_tabs.size()); ++i) {
        if (i != g_activeTabIndex) {
            initial = g_tabs[i].directory;
            break;
        }
    }
    std::wstring result;
    if (!PromptFolder(hwnd, isMove ? Tr(L"フォルダを指定して移動") : Tr(L"フォルダを指定してコピー"),
                      isMove ? Tr(L"移動先") : Tr(L"コピー先"), g_currentDir, initial, result)) {
        return L"";
    }
    return result;
}

// コピー・移動の本体。toTab が true なら他のタブへ (1つならそのまま、複数ならメニュー、
// 無ければダイアログ)、false ならフォルダ指定ダイアログ。
void OnCmdTransfer(HWND hwnd, HWND hList, bool isMove, bool toTab) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    if (targets.empty()) return;

    std::wstring destDir;
    if (toTab) {
        bool noOtherTab = false;
        destDir = PickOtherTabDirectory(hwnd, hList, noOtherTab);
        if (noOtherTab) {
            destDir = PromptDestination(hwnd, isMove);  // 他のタブが無いときはフォルダ指定にする
        }
    } else {
        destDir = PromptDestination(hwnd, isMove);
    }
    if (destDir.empty()) return;
    RunTransfer(hwnd, hList, targets, destDir, isMove);
}

// Explorer等から複数ファイルがドロップされたときのコールバック (WM_DROPFILES から呼ばれる)。
// カレントディレクトリへコピーまたは移動する。
void OnFilesDropped(const std::vector<std::wstring>& paths, bool isMove) {
    if (!g_dropTargetList || !g_mainHwnd) {
        return;
    }
    RunTransfer(g_mainHwnd, g_dropTargetList, paths, g_currentDir, isMove);
}

// hList の WM_DROPFILES を処理する (Explorer 等からのドロップ受け入れ)。
// Shift を押しながらのドロップは移動、それ以外は既定でコピーとする。
LRESULT CALLBACK FileListSubclassProc(HWND hList, UINT msg, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR /*subclassId*/, DWORD_PTR /*refData*/) {
    if (msg == WM_DROPFILES) {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> paths;
        for (UINT i = 0; i < count; ++i) {
            wchar_t buf[MAX_PATH];
            if (DragQueryFileW(hDrop, i, buf, MAX_PATH)) {
                paths.emplace_back(buf);
            }
        }
        DragFinish(hDrop);

        bool isMove = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        OnFilesDropped(paths, isMove);
        return 0;
    }
    // 1キー操作のコマンドを実行した直後の文字入力は捨てる (一覧の頭文字検索でカーソルが飛ばないように)。
    if (msg == WM_CHAR && GetTickCount64() - g_commandKeyTick < 200) return 0;
    if (msg == WM_CONTEXTMENU) {
        // マウス右クリック、またはキーボードのメニューキー/Shift+F10 (その場合 lParam は -1,-1)。
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        ShowFileContextMenu(GetParent(hList), hList, x, y);
        return 0;
    }
    return DefSubclassProc(hList, msg, wParam, lParam);
}

void OnCmdDelete(HWND hwnd, HWND hList, bool permanent) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    if (targets.empty()) return;

    if (permanent) {
        std::wstring message = targets.size() == 1
            ? Fmt(L"「{0}」を完全に削除します。元に戻せません。\nよろしいですか?", {NameOfPath(targets[0])})
            : Fmt(L"{0} 件を完全に削除します。元に戻せません。\nよろしいですか?", {Num(targets.size())});
        if (ThemedMessageBox(hwnd, message.c_str(), kWindowTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    }

    std::wstring cursorName = CursorItemName(hList);
    std::vector<OpPair> done;
    bool ok = DeleteItems(hwnd, targets, permanent, &done);  // エラーダイアログは IFileOperation 側が出す
    if (!permanent) {
        PushUndoTrashed(Tr(L"削除"), done);
    }
    RefreshFileListView(hList, cursorName);
    SetStatusMessage(hwnd, ok ? Fmt(permanent ? L"{0} 件を完全に削除しました。" : L"{0} 件をごみ箱へ移しました。(Z で元に戻せます)", {Num(done.size())})
                              : Tr(L"削除は完了しませんでした。"));
}

void OnCmdRename(HWND, HWND hList) {
    int index = GetCursorItemIndex(hList);
    if (index < 0) return;

    HWND hEdit = ListView_EditLabel(hList, index);

    // ファイルは拡張子を除いた部分だけを選択状態にする (フォルダ・".gitignore" のような名前は全体)。
    if (hEdit) {
        std::wstring name = GetItemName(hList, index);
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0 && !IsDirectoryPath(BuildFullPath(g_currentDir, name))) {
            SendMessageW(hEdit, EM_SETSEL, 0, static_cast<LPARAM>(dot));
        }
    }
}

// LVN_ENDLABELEDIT (リネーム確定) の処理
void OnEndLabelEdit(HWND hwnd, HWND hList, const NMLVDISPINFOW* dispInfo) {
    if (dispInfo->item.pszText == nullptr) {
        return;  // 編集キャンセル
    }

    std::wstring oldName = GetItemName(hList, dispInfo->item.iItem);
    std::wstring newName = dispInfo->item.pszText;
    if (newName.empty() || newName == oldName) {
        return;
    }

    std::wstring fullPath = BuildFullPath(g_currentDir, oldName);
    std::wstring newPath;
    if (RenameItem(nullptr, fullPath, newName, &newPath)) {
        PushUndoMoved(Tr(L"リネーム"), {OpPair{fullPath, newPath}});
        RefreshFileListView(hList, newName);
        SetStatusMessage(hwnd, Fmt(L"「{0}」を「{1}」にリネームしました。", {oldName, newName}));
    }
}

// "名前.拡張子" → "名前 - コピー.拡張子" (フォルダは拡張子を分けない)。
std::wstring MakeCopyName(const std::wstring& name, bool isDirectory) {
    size_t dot = name.find_last_of(L'.');
    if (isDirectory || dot == std::wstring::npos || dot == 0) {
        return name + Tr(L" - コピー");
    }
    return name.substr(0, dot) + Tr(L" - コピー") + name.substr(dot);
}

// B: 複製。1件のときは名前を確認するダイアログを出し、複数のときは自動で名前を付ける。
void OnCmdDuplicate(HWND hwnd, HWND hList) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    if (targets.empty()) return;

    std::vector<std::wstring> created;
    std::wstring lastName;
    for (const std::wstring& src : targets) {
        bool isDir = IsDirectoryPath(src);
        std::wstring parent = src.substr(0, src.find_last_of(L'\\'));
        std::wstring suggested = NameOfPath(MakeUniquePath(BuildFullPath(parent, MakeCopyName(NameOfPath(src), isDir)), isDir));
        std::wstring name = suggested;
        if (targets.size() == 1) {
            if (!PromptText(hwnd, Tr(L"複製"), Tr(L"複製後の名前"), name)) return;
            if (!IsValidFileName(name)) {
                ShowMessage(hwnd, Tr(L"その名前は使えません。"), MB_ICONWARNING);
                return;
            }
        }
        std::wstring newPath;
        if (!DuplicateItem(hwnd, src, name, &newPath)) break;
        if (!newPath.empty()) created.push_back(newPath);
        lastName = NameOfPath(newPath);
    }
    PushUndoCreated(Tr(L"複製"), created);
    RefreshFileListView(hList, lastName.empty() ? CursorItemName(hList) : lastName);
    SetStatusMessage(hwnd, Fmt(L"{0} 件を複製しました。", {Num(created.size())}));
}

// K: フォルダ作成
void OnCmdMakeFolder(HWND hwnd, HWND hList) {
    std::wstring name;
    if (!PromptText(hwnd, Tr(L"フォルダ作成"), Tr(L"フォルダ名"), name)) return;
    if (!IsValidFileName(name)) {
        ShowMessage(hwnd, Tr(L"フォルダ名に使えない文字が含まれているか、空です。"), MB_ICONWARNING);
        return;
    }
    std::wstring path = BuildFullPath(g_currentDir, name);
    if (!CreateDirectoryW(path.c_str(), nullptr)) {
        DWORD err = GetLastError();
        ShowMessage(hwnd, err == ERROR_ALREADY_EXISTS ? Tr(L"同じ名前のフォルダまたはファイルが既にあります。")
                                                      : Tr(L"フォルダを作成できませんでした。"), MB_ICONWARNING);
        return;
    }
    PushUndoCreated(Tr(L"フォルダ作成"), {path});
    RefreshFileListView(hList, name);
    SetStatusMessage(hwnd, Fmt(L"フォルダ「{0}」を作成しました。", {name}));
}

// A: 属性変更 (読み取り専用・隠しファイル・システム・アーカイブ)。
// ダイアログを開いた時点の状態から変えた属性だけを、対象すべてに反映する。
void OnCmdAttributes(HWND hwnd, HWND hList) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    if (targets.empty()) return;

    struct Bit {
        DWORD mask;
        const wchar_t* label;
    };
    static const Bit kBits[] = {
        {FILE_ATTRIBUTE_READONLY, Tr(L"読み取り専用")},
        {FILE_ATTRIBUTE_HIDDEN, Tr(L"隠しファイル")},
        {FILE_ATTRIBUTE_SYSTEM, Tr(L"システム")},
        {FILE_ATTRIBUTE_ARCHIVE, Tr(L"アーカイブ")},
    };
    DWORD firstAttrs = GetFileAttributesW(targets[0].c_str());
    if (firstAttrs == INVALID_FILE_ATTRIBUTES) return;

    std::vector<DialogField> fields;
    for (const Bit& b : kBits) {
        fields.push_back(DialogField{b.label, (firstAttrs & b.mask) ? L"1" : L"0", 0, FieldKind::Check, false});
    }
    DialogOptions options;
    if (targets.size() > 1) {
        options.hint = Fmt(L"{0} 件が対象です (変更した項目だけ反映します)", {Num(targets.size())});
    }
    std::wstring title = targets.size() == 1 ? Fmt(L"属性変更 - {0}", {NameOfPath(targets[0])}) : std::wstring(Tr(L"属性変更"));
    if (!ShowFieldsDialog(hwnd, title, fields, options)) return;

    DWORD setMask = 0, clearMask = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        bool before = (firstAttrs & kBits[i].mask) != 0;
        bool after = fields[i].value == L"1";
        if (after && !before) setMask |= kBits[i].mask;
        if (!after && before) clearMask |= kBits[i].mask;
    }
    if (!setMask && !clearMask) return;

    std::vector<std::pair<std::wstring, DWORD>> old;
    for (const std::wstring& path : targets) {
        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;
        DWORD wanted = (attrs & ~clearMask) | setMask;
        if (wanted == attrs) continue;
        // 通常のファイル以外に NORMAL を重ねないよう、他の属性があれば NORMAL を外す。
        if (wanted & ~static_cast<DWORD>(FILE_ATTRIBUTE_NORMAL)) wanted &= ~static_cast<DWORD>(FILE_ATTRIBUTE_NORMAL);
        if (SetFileAttributesW(path.c_str(), wanted)) old.emplace_back(path, attrs);
    }
    PushUndoAttributes(Tr(L"属性変更"), old);
    RefreshFileListView(hList, CursorItemName(hList));
    SetStatusMessage(hwnd, Fmt(L"{0} 件の属性を変更しました。", {Num(old.size())}));
}

// S: ショートカット作成 ("名前 - ショートカット.lnk" を同じフォルダに作る)
void OnCmdCreateShortcut(HWND hwnd, HWND hList) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    if (targets.empty()) return;
    std::vector<std::wstring> created;
    for (const std::wstring& target : targets) {
        std::wstring link = MakeUniquePath(BuildFullPath(g_currentDir, NameOfPath(target) + Tr(L" - ショートカット.lnk")), false);
        if (CreateShortcutFile(target, link)) created.push_back(link);
    }
    PushUndoCreated(Tr(L"ショートカット作成"), created);
    RefreshFileListView(hList, created.empty() ? CursorItemName(hList) : NameOfPath(created.back()));
    SetStatusMessage(hwnd, Fmt(L"{0} 件のショートカットを作成しました。", {Num(created.size())}));
}

// P: zip 圧縮 (Windows 標準の tar.exe を別スレッドで実行する)
void OnCmdZip(HWND hwnd, HWND hList) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    if (targets.empty()) return;

    std::wstring defaultName;
    if (targets.size() == 1) {
        std::wstring n = NameOfPath(targets[0]);
        size_t dot = n.find_last_of(L'.');
        defaultName = (!IsDirectoryPath(targets[0]) && dot != std::wstring::npos && dot > 0) ? n.substr(0, dot) : n;
    } else {
        defaultName = GetLastPathComponent(g_currentDir);
        for (wchar_t& c : defaultName) {
            if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
        }
    }
    defaultName += L".zip";
    std::wstring name = defaultName;
    if (!PromptText(hwnd, Tr(L"zip圧縮"), Tr(L"zipファイル名"), name)) return;
    if (name.size() < 4 || !EqualNoCase(name.substr(name.size() - 4), L".zip")) name += L".zip";
    if (!IsValidFileName(name)) {
        ShowMessage(hwnd, Tr(L"その名前は使えません。"), MB_ICONWARNING);
        return;
    }

    ArchiveRequest req;
    req.extract = false;
    req.workDir = g_currentDir;
    for (const std::wstring& t : targets) req.names.push_back(NameOfPath(t));
    req.zipPath = MakeUniquePath(BuildFullPath(g_currentDir, name), false);  // 同名があれば番号を付ける
    SetStatusMessage(hwnd, Fmt(L"圧縮中: {0} ...", {NameOfPath(req.zipPath)}));
    StartArchiveJob(hwnd, kMsgArchiveDone, req);
}

// U: zip 解凍 (zip と同じ名前のフォルダは作らず、中身をいまのフォルダへ直接展開する)。
// いったん作業用フォルダへ展開してから、最上位の項目をいまのフォルダへ移す
// (同名の既存項目は上書きせず番号を付ける。「元に戻す」で作った項目だけを消せるようにするため)。
// Shift+U (intoFolder): zip と同じ名前のフォルダ (同名があれば番号付き) を作り、その中へ展開する。
void OnCmdUnzip(HWND hwnd, HWND hList, bool intoFolder = false) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    int started = 0;
    static int sequence = 0;
    for (const std::wstring& zip : targets) {
        if (zip.size() < 4 || !EqualNoCase(zip.substr(zip.size() - 4), L".zip") || IsDirectoryPath(zip)) continue;
        std::wstring destDir;
        if (intoFolder) {
            std::wstring stem = NameOfPath(zip);
            stem.resize(stem.size() - 4);
            if (stem.empty() || !IsValidFileName(stem)) stem = L"zip";
            destDir = MakeUniquePath(BuildFullPath(g_currentDir, stem), true);
        } else {
            destDir = BuildFullPath(
                g_currentDir, L".film_unzip_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++sequence));
        }
        if (!CreateDirectoryW(destDir.c_str(), nullptr)) continue;

        ArchiveRequest req;
        req.extract = true;
        req.intoFolder = intoFolder;
        req.zipPath = zip;
        req.destDir = destDir;
        StartArchiveJob(hwnd, kMsgArchiveDone, req);
        ++started;
    }
    if (started == 0) {
        SetStatusMessage(hwnd, Tr(L"zip ファイルを選んでください。"));
    } else {
        SetStatusMessage(hwnd, Tr(L"展開中 ..."));
    }
}

// zip の圧縮・展開が終わったとき (別スレッドからの通知)。
void OnArchiveDone(HWND hwnd, ArchiveResult* result) {
    std::unique_ptr<ArchiveResult> r(result);
    HWND hList = GetDlgItem(hwnd, kIdFileListView);
    if (!r->ok) {
        if (r->extract) RemoveDirectoryW(r->output.c_str());  // 空のまま残った展開先フォルダを片付ける
        ShowMessage(hwnd, std::wstring(r->extract ? Tr(L"zip を展開できませんでした。\n") : Tr(L"zip を作成できませんでした。\n")) + r->message,
                    MB_ICONWARNING);
        SetStatusMessage(hwnd, Tr(L"失敗しました。"));
        return;
    }
    std::wstring parent = r->output.substr(0, r->output.find_last_of(L'\\'));
    std::vector<std::wstring> created;
    if (r->extract && r->intoFolder) {
        created.push_back(r->output);  // zip 名のフォルダそのものが作った項目 (元に戻すと中身ごと消える)
    } else if (r->extract) {
        // 作業用フォルダの最上位の項目を、zip のあるフォルダへ移す。
        std::vector<std::pair<std::wstring, bool>> items;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((r->output + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring n = fd.cFileName;
                if (n != L"." && n != L"..") items.emplace_back(n, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        for (const auto& item : items) {
            std::wstring dest = MakeUniquePath(BuildFullPath(parent, item.first), item.second);
            if (MoveFileExW(BuildFullPath(r->output, item.first).c_str(), dest.c_str(), 0)) created.push_back(dest);
        }
        RemoveDirectoryW(r->output.c_str());
    } else {
        created.push_back(r->output);
    }
    PushUndoCreated(r->extract ? Tr(L"zip解凍") : Tr(L"zip圧縮"), created);
    if (EqualNoCase(TrimTrailingSlash(parent), TrimTrailingSlash(g_currentDir))) {
        RefreshFileListView(hList, created.empty() ? CursorItemName(hList) : NameOfPath(created.front()));
    }
    SetStatusMessage(hwnd, r->extract ? Fmt(L"{0} 件を展開しました。", {Num(created.size())})
                                      : Fmt(L"圧縮しました: {0}", {NameOfPath(r->output)}));
}

// Z: 元に戻す
void OnCmdUndo(HWND hwnd, HWND hList) {
    std::wstring message;
    HCURSOR oldCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));  // ごみ箱の復元は 1 秒ほどかかる
    bool ok = UndoLast(hwnd, message);
    SetCursor(oldCursor);
    RefreshFileListView(hList, CursorItemName(hList));
    if (message.find(L'\n') != std::wstring::npos) {
        ShowMessage(hwnd, message, ok ? MB_ICONINFORMATION : MB_ICONWARNING);
    }
    SetStatusMessage(hwnd, message.substr(0, message.find(L'\n')));
}

// E: テキストエディタで編集 (バイナリは開かない)
void OnCmdEdit(HWND hwnd, HWND hList) {
    std::wstring path = CursorItemPath(hList);
    if (path.empty()) return;
    if (IsDirectoryPath(path)) {
        SetStatusMessage(hwnd, Tr(L"フォルダは編集できません。"));
        return;
    }
    switch (EditFile(hwnd, path, g_currentDir)) {
        case EditResult::Binary:
            SetStatusMessage(hwnd, Fmt(L"バイナリファイルは編集できません: {0}", {NameOfPath(path)}));
            break;
        case EditResult::Failed:
            ShowMessage(hwnd, Fmt(L"エディタを起動できませんでした: {0}", {path}), MB_ICONWARNING);
            break;
        case EditResult::Started:
            break;
    }
}

// Shift+E: エイリアスの定義ファイル (favorites.json) をエディタで開く。
// 保存後にこのアプリへ戻ると、変更を検出して読み直す。
void OnCmdEditAliases(HWND hwnd) {
    std::wstring path = GetFavoritesFilePath();
    if (path.empty()) return;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WriteUtf16FileAtomic(path, L"[]\r\n");
    }
    if (EditFile(hwnd, path, L"", true) != EditResult::Started) {
        ShowMessage(hwnd, Fmt(L"エディタを起動できませんでした: {0}", {path}), MB_ICONWARNING);
    } else {
        SetStatusMessage(hwnd, Tr(L"favorites.json を編集して保存すると、このウィンドウに戻ったときに反映されます。"));
    }
}

// V: プレビュー欄の表示/非表示
void OnCmdTogglePreview(HWND hwnd) {
    SetPreviewVisible(hwnd, !g_showPreview);
}

// Shift+V: 一覧を隠してプレビュー欄を広げる / 元に戻す
void OnCmdMaximizePreview(HWND hwnd) {
    if (!g_showPreview) SetPreviewVisible(hwnd, true);
    g_previewMax = !g_previewMax;
    RelayoutNow(hwnd);
    if (g_previewMax) {
        UpdatePreviewNow(hwnd);
        PreviewPaneFocusContent();
    } else if (FocusIsInPreview() || GetFocus() == nullptr) {
        FocusList(hwnd);
    }
}

// Tab: プレビュー欄の内容へフォーカスを移す (矢印キー・PageUp/PageDown でスクロールできる)
void OnCmdFocusPreview(HWND hwnd) {
    if (!g_showPreview) return;  // ビューワーが開いていないときは何もしない (Tab で勝手に開かない)
    UpdatePreviewNow(hwnd);
    if (!PreviewPaneFocusContent()) SetStatusMessage(hwnd, Tr(L"プレビューできる項目を選んでください。"));
}

// 操作対象のフルパスをクリップボードにテキストとしてコピーする (複数は改行区切り)。
void OnCmdCopyPath(HWND hwnd, HWND hList) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    if (targets.empty()) return;

    std::wstring fullPath;
    for (size_t i = 0; i < targets.size(); ++i) {
        if (i) fullPath += L"\r\n";
        fullPath += targets[i];
    }

    // クリップボード履歴などが一時的に掴んでいることがあるので、少し待って再試行する。
    bool opened = false;
    for (int i = 0; i < 10 && !opened; ++i) {
        opened = OpenClipboard(hwnd) != FALSE;
        if (!opened) Sleep(30);
    }
    if (!opened) {
        SetStatusMessage(hwnd, Tr(L"クリップボードを開けませんでした。"));
        return;
    }
    EmptyClipboard();

    size_t byteSize = (fullPath.size() + 1) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, byteSize);
    if (hGlobal) {
        void* dest = GlobalLock(hGlobal);
        memcpy(dest, fullPath.c_str(), byteSize);
        GlobalUnlock(hGlobal);
        SetClipboardData(CF_UNICODETEXT, hGlobal);
    }
    CloseClipboard();
    SetStatusMessage(hwnd, targets.size() == 1 ? std::wstring(Tr(L"フルパスをコピーしました。"))
                                               : Fmt(L"{0} 件のフルパスをコピーしました。", {Num(targets.size())}));
}

// リストビューからのドラッグ開始 (LVN_BEGINDRAG)。ドロップ先が MOVE を選んだ場合は
// (データの複製はドロップ先が行うため) 元ファイルをこちらで削除する必要がある。
void OnBeginDrag(HWND hwnd, HWND hList) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    if (targets.empty()) return;

    std::wstring cursorName = CursorItemName(hList);
    DWORD effect = BeginFileDrag(hwnd, targets);
    if (effect == DROPEFFECT_MOVE) {
        DeleteItems(hwnd, targets, false, nullptr);
        RefreshFileListView(hList, cursorName);
    }
}

// Ctrl+C / Ctrl+X: 操作対象をクリップボードに置く (Explorer と相互運用できる形式)。
void OnCmdClipboardCopy(HWND hwnd, HWND hList, bool cut) {
    std::vector<std::wstring> targets = GatherTargets(hList);
    if (!targets.empty()) {
        SetFilesToClipboard(targets, cut);
        SetStatusMessage(hwnd, Fmt(cut ? L"{0} 件を切り取りました。" : L"{0} 件をコピーしました。", {Num(targets.size())}));
    }
}

// Ctrl+V: クリップボード上のファイルをカレントディレクトリに貼り付ける。
// 切り取りだった場合は移動し、完了後にクリップボードを空にする。
void OnCmdPaste(HWND hwnd, HWND hList) {
    std::vector<std::wstring> paths;
    bool isCut = false;
    if (!GetFilesFromClipboard(paths, isCut)) {
        return;
    }
    RunTransfer(hwnd, hList, paths, g_currentDir, isCut);
    if (isCut) {
        ClearFileClipboard();
    }
}

// ---- ディレクトリ移動・タブ ----

// ディレクトリを移動し、一覧を再表示する。selectItemName が指定されていればその項目を、
// 無ければ先頭項目を選択する。アクティブタブの見出し・ウィンドウタイトルも更新する。
void NavigateToDirectory(HWND hwnd, HWND hList, const std::wstring& newDir,
                          const std::wstring& selectItemName) {
    if (newDir != g_currentDir && !g_tabs.empty()) {
        ActiveTab().marks.Clear();  // マークはディレクトリ内のファイル名なので、移動したら破棄する
    }
    g_currentDir = newDir;
    g_statusMessage.clear();
    PopulateFileListView(hList, g_currentDir);

    int count = ListView_GetItemCount(hList);
    if (count > 0) {
        int restoreIndex = 0;
        if (!selectItemName.empty()) {
            for (int i = 0; i < count; ++i) {
                if (EqualNoCase(GetItemName(hList, i), selectItemName)) {
                    restoreIndex = i;
                    break;
                }
            }
        }
        SelectItem(hList, restoreIndex);
    }

    HWND hTab = GetDlgItem(hwnd, kIdTabControl);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    std::wstring label = GetLastPathComponent(g_currentDir);
    item.pszText = const_cast<LPWSTR>(label.c_str());
    TabCtrl_SetItem(hTab, g_activeTabIndex, &item);

    UpdateWindowTitle(hwnd);
    MarkSessionDirty(hwnd);
}

// Enter: カーソル項目がフォルダならその中へ、ファイルなら関連付けられたアプリで開く。
void NavigateIntoSelected(HWND hwnd, HWND hList) {
    std::wstring fullPath = CursorItemPath(hList);
    if (fullPath.empty()) return;

    if (IsDirectoryPath(fullPath)) {
        NavigateToDirectory(hwnd, hList, fullPath, L"");
    } else if (!OpenWithAssociation(hwnd, fullPath, g_currentDir)) {
        ShowMessage(hwnd, Fmt(L"開けませんでした: {0}", {fullPath}), MB_ICONWARNING);
    }
}

// Shift+Enter: プログラムを選択して実行 (「プログラムから開く」ダイアログ)。
void OpenWithSelected(HWND hwnd, HWND hList) {
    std::wstring fullPath = CursorItemPath(hList);
    if (fullPath.empty()) return;
    if (IsDirectoryPath(fullPath)) {
        SetStatusMessage(hwnd, Tr(L"フォルダには使えません。"));
        return;
    }
    if (!OpenWithChooser(hwnd, fullPath)) {
        ShowMessage(hwnd, Fmt(L"開けませんでした: {0}", {fullPath}), MB_ICONWARNING);
    }
}

// 親ディレクトリへ移動する。移動元のフォルダにカーソルを合わせる。
void NavigateToParent(HWND hwnd, HWND hList) {
    std::wstring parent = GetParentDirectory(g_currentDir);
    if (parent == g_currentDir) {
        return;  // 既にドライブルート
    }
    std::wstring childName = GetLastPathComponent(g_currentDir);
    NavigateToDirectory(hwnd, hList, parent, childName);
}

// Shift+BS: ドライブ (共有) のルートへ移動する。
void NavigateToRoot(HWND hwnd, HWND hList) {
    std::wstring root = GetRootOfPath(g_currentDir);
    if (root.empty() || EqualNoCase(TrimTrailingSlash(root), TrimTrailingSlash(g_currentDir))) {
        return;
    }
    NavigateToDirectory(hwnd, hList, root, L"");
}

// G: フォルダを指定して移動 (TAB でフォルダ名・エイリアス名を補完)
void OnCmdGotoFolder(HWND hwnd, HWND hList) {
    std::wstring dir;
    if (PromptFolder(hwnd, Tr(L"フォルダ移動"), Tr(L"移動先"), g_currentDir, g_currentDir, dir)) {
        NavigateToDirectory(hwnd, hList, dir, L"");
    }
}

// アクティブタブの現在の状態 (ディレクトリ・カーソル項目) を g_tabs に書き戻す。
void SaveActiveTabState(HWND hList) {
    if (g_activeTabIndex < 0 || g_activeTabIndex >= static_cast<int>(g_tabs.size())) {
        return;
    }
    int index = GetCursorItemIndex(hList);
    g_tabs[g_activeTabIndex].directory = g_currentDir;
    g_tabs[g_activeTabIndex].cursorItemName = index >= 0 ? GetItemName(hList, index) : L"";
}

// index 番目のタブの状態を読み込んで一覧に反映する (g_activeTabIndex は呼び出し側で更新済みとする)。
void LoadActiveTabState(HWND hwnd, HWND hList) {
    const TabState& tab = g_tabs[g_activeTabIndex];
    g_currentDir = tab.directory;
    g_statusMessage.clear();
    PopulateFileListView(hList, g_currentDir);
    // タブごとに表示モード (詳細/一覧) が違うことがあるので、列幅などのレイアウトも整え直す。
    RelayoutNow(hwnd);

    int count = ListView_GetItemCount(hList);
    if (count > 0) {
        int restoreIndex = 0;
        if (!tab.cursorItemName.empty()) {
            for (int i = 0; i < count; ++i) {
                if (EqualNoCase(GetItemName(hList, i), tab.cursorItemName)) {
                    restoreIndex = i;
                    break;
                }
            }
        }
        SelectItem(hList, restoreIndex);
    }
    UpdateWindowTitle(hwnd);
}

// 新しいタブを追加し、アクティブにする。dir を省略すると現在のディレクトリを引き継ぐ。
void OnCmdNewTab(HWND hwnd, HWND hList, const std::wstring& dir = L"") {
    HWND hTab = GetDlgItem(hwnd, kIdTabControl);
    SaveActiveTabState(hList);

    TabState newTab;
    newTab.directory = dir.empty() ? g_currentDir : dir;
    newTab.sortKey = ActiveTab().sortKey;  // ソート・表示モードは現在のタブから引き継ぐ
    newTab.sortDescending = ActiveTab().sortDescending;
    newTab.viewMode = ActiveTab().viewMode;
    g_tabs.push_back(std::move(newTab));
    g_activeTabIndex = static_cast<int>(g_tabs.size()) - 1;

    TCITEMW item{};
    item.mask = TCIF_TEXT;
    std::wstring label = GetLastPathComponent(g_tabs[g_activeTabIndex].directory);
    item.pszText = const_cast<LPWSTR>(label.c_str());
    TabCtrl_InsertItem(hTab, g_activeTabIndex, &item);
    TabCtrl_SetCurSel(hTab, g_activeTabIndex);

    LoadActiveTabState(hwnd, hList);
    MarkSessionDirty(hwnd);
}

// O: フォルダを指定して新しいタブを開く
void OnCmdOpenTabAt(HWND hwnd, HWND hList) {
    std::wstring dir;
    if (PromptFolder(hwnd, Tr(L"新しいタブを開く"), Tr(L"フォルダ"), g_currentDir, g_currentDir, dir)) {
        OnCmdNewTab(hwnd, hList, dir);
    }
}

// アクティブタブを閉じる。最後の1枚は閉じない。
void OnCmdCloseTab(HWND hwnd, HWND hList) {
    if (g_tabs.size() <= 1) {
        return;
    }
    HWND hTab = GetDlgItem(hwnd, kIdTabControl);

    TabCtrl_DeleteItem(hTab, g_activeTabIndex);
    g_tabs.erase(g_tabs.begin() + g_activeTabIndex);
    g_activeTabIndex = std::min(g_activeTabIndex, static_cast<int>(g_tabs.size()) - 1);

    TabCtrl_SetCurSel(hTab, g_activeTabIndex);
    LoadActiveTabState(hwnd, hList);
    MarkSessionDirty(hwnd);
}

// タブを相対移動 (+1で次、-1で前。両端はラップする)。
void SwitchTabBy(HWND hwnd, HWND hList, int delta) {
    int count = static_cast<int>(g_tabs.size());
    if (count <= 1) {
        return;
    }
    HWND hTab = GetDlgItem(hwnd, kIdTabControl);
    SaveActiveTabState(hList);

    g_activeTabIndex = (g_activeTabIndex + delta + count) % count;

    TabCtrl_SetCurSel(hTab, g_activeTabIndex);
    LoadActiveTabState(hwnd, hList);
    MarkSessionDirty(hwnd);
}

// タブをクリックして切替えたとき (TCN_SELCHANGE) の処理。
void OnTabControlSelChange(HWND hwnd, HWND hList, HWND hTab) {
    SaveActiveTabState(hList);
    g_activeTabIndex = TabCtrl_GetCurSel(hTab);
    LoadActiveTabState(hwnd, hList);
    MarkSessionDirty(hwnd);
}

// タブストリップを g_tabs の内容で作り直す (並べ替え・セット切替の後)。
void RebuildTabStrip(HWND hwnd) {
    HWND hTab = GetDlgItem(hwnd, kIdTabControl);
    TabCtrl_DeleteAllItems(hTab);
    for (size_t i = 0; i < g_tabs.size(); ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        std::wstring label = GetLastPathComponent(g_tabs[i].directory);
        item.pszText = const_cast<LPWSTR>(label.c_str());
        TabCtrl_InsertItem(hTab, static_cast<int>(i), &item);
    }
    TabCtrl_SetCurSel(hTab, g_activeTabIndex);
}

// アクティブタブを左 (-1) / 右 (+1) へ 1 つ動かす。端では何もしない。
void MoveActiveTab(HWND hwnd, HWND hList, int delta) {
    int target = g_activeTabIndex + delta;
    if (target < 0 || target >= static_cast<int>(g_tabs.size())) return;
    SaveActiveTabState(hList);
    std::swap(g_tabs[g_activeTabIndex], g_tabs[target]);
    g_activeTabIndex = target;
    RebuildTabStrip(hwnd);
    MarkSessionDirty(hwnd);
}

// ---- タブセット (仮想デスクトップのように切り替えられるタブの組) ----

// g_tabSets を少なくとも 1 件にし、g_activeSet を範囲内にする。
void EnsureTabSets() {
    if (g_tabSets.empty()) {
        TabSet set;
        set.name = Tr(L"セット1");
        g_tabSets.push_back(std::move(set));
    }
    g_activeSet = std::clamp(g_activeSet, 0, static_cast<int>(g_tabSets.size()) - 1);
}

// 作業用の g_tabs をアクティブなセットへ書き戻す。
void CommitActiveSet() {
    EnsureTabSets();
    g_tabSets[g_activeSet].tabs = g_tabs;
    g_tabSets[g_activeSet].activeIndex = g_activeTabIndex;
}

// index 番目のセットを表示する。
void SwitchToTabSet(HWND hwnd, HWND hList, int index) {
    EnsureTabSets();
    if (index < 0 || index >= static_cast<int>(g_tabSets.size()) || index == g_activeSet) return;
    SaveActiveTabState(hList);
    CommitActiveSet();
    g_activeSet = index;
    g_tabs = g_tabSets[index].tabs;
    g_activeTabIndex = std::clamp(g_tabSets[index].activeIndex, 0, static_cast<int>(g_tabs.size()) - 1);
    RebuildTabStrip(hwnd);
    LoadActiveTabState(hwnd, hList);
    MarkSessionDirty(hwnd);
    SetStatusMessage(hwnd, Fmt(L"タブセット「{0}」に切り替えました。", {g_tabSets[g_activeSet].name}));
}

// Shift+L: 現在のフォルダを開いた 1 枚のタブだけを持つ新しいセットを追加して切り替える。
void OnCmdNewTabSet(HWND hwnd, HWND hList) {
    EnsureTabSets();
    SaveActiveTabState(hList);
    CommitActiveSet();

    TabSet set;
    for (int n = static_cast<int>(g_tabSets.size()) + 1;; ++n) {
        set.name = Fmt(L"セット{0}", {Num(n)});
        bool used = false;
        for (const TabSet& s : g_tabSets) used |= (s.name == set.name);
        if (!used) break;
    }
    TabState tab;
    tab.directory = g_currentDir;
    tab.sortKey = ActiveTab().sortKey;
    tab.sortDescending = ActiveTab().sortDescending;
    tab.viewMode = ActiveTab().viewMode;
    set.tabs.push_back(std::move(tab));
    g_tabSets.push_back(std::move(set));
    SwitchToTabSet(hwnd, hList, static_cast<int>(g_tabSets.size()) - 1);
}

void OnCmdRenameTabSet(HWND hwnd) {
    EnsureTabSets();
    std::wstring name = g_tabSets[g_activeSet].name;
    if (!PromptText(hwnd, Tr(L"タブセットの名前"), Tr(L"名前"), name) || name.empty()) return;
    g_tabSets[g_activeSet].name = name;
    UpdateWindowTitle(hwnd);
    MarkSessionDirty(hwnd);
}

void OnCmdDeleteTabSet(HWND hwnd, HWND hList) {
    EnsureTabSets();
    if (g_tabSets.size() <= 1) {
        SetStatusMessage(hwnd, Tr(L"最後のタブセットは削除できません。"));
        return;
    }
    std::wstring message = Fmt(L"タブセット「{0}」とその中のタブを閉じます。\nよろしいですか?", {g_tabSets[g_activeSet].name});
    if (ThemedMessageBox(hwnd, message.c_str(), kWindowTitle, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;

    g_tabSets.erase(g_tabSets.begin() + g_activeSet);
    g_activeSet = std::min(g_activeSet, static_cast<int>(g_tabSets.size()) - 1);
    g_tabs = g_tabSets[g_activeSet].tabs;
    g_activeTabIndex = std::clamp(g_tabSets[g_activeSet].activeIndex, 0, static_cast<int>(g_tabs.size()) - 1);
    RebuildTabStrip(hwnd);
    LoadActiveTabState(hwnd, hList);
    MarkSessionDirty(hwnd);
}

// キーボード操作からのメニューを、カーソル項目の位置 (無ければマウス位置) に出して選択結果を返す。
UINT TrackMenuAtCursor(HWND hwnd, HWND hList, HMENU menu) {
    POINT pt{0, 0};
    int cursorIndex = GetCursorItemIndex(hList);
    RECT itemRect;
    if (cursorIndex >= 0 && ListView_GetItemRect(hList, cursorIndex, &itemRect, LVIR_BOUNDS)) {
        pt = {itemRect.left, itemRect.bottom};
        ClientToScreen(hList, &pt);
    } else {
        GetCursorPos(&pt);
    }
    UINT selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    return selected;
}

// L: タブセットの切替メニュー (他のタブへコピーのときと同じ形式)。
void OnCmdTabSetMenu(HWND hwnd, HWND hList) {
    constexpr UINT kSetBase = 9101, kNew = 9190, kRename = 9191, kDelete = 9192;
    EnsureTabSets();
    SaveActiveTabState(hList);
    CommitActiveSet();

    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < g_tabSets.size(); ++i) {
        std::wstring label = g_tabSets[i].name + L"  (" + Fmt(L"{0} タブ", {Num(g_tabSets[i].tabs.size())}) + L")";
        UINT flags = MF_STRING | (static_cast<int>(i) == g_activeSet ? MF_CHECKED : 0);
        AppendMenuW(menu, flags, kSetBase + i, label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kNew, Tr(L"新しいタブセットを追加 (Shift+L)"));
    AppendMenuW(menu, MF_STRING, kRename, Tr(L"このタブセットの名前を変更..."));
    AppendMenuW(menu, MF_STRING | (g_tabSets.size() > 1 ? 0 : MF_GRAYED), kDelete, Tr(L"このタブセットを削除..."));
    UINT selected = TrackMenuAtCursor(hwnd, hList, menu);
    DestroyMenu(menu);

    if (selected == kNew) OnCmdNewTabSet(hwnd, hList);
    else if (selected == kRename) OnCmdRenameTabSet(hwnd);
    else if (selected == kDelete) OnCmdDeleteTabSet(hwnd, hList);
    else if (selected >= kSetBase && selected < kSetBase + g_tabSets.size()) {
        SwitchToTabSet(hwnd, hList, static_cast<int>(selected - kSetBase));
    }
}

// 現在の g_tabs / g_activeTabIndex / ウィンドウ位置から SessionState を組み立てる。
SessionState BuildCurrentSessionState(HWND hwnd) {
    SessionState state;

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp)) {
        state.window.valid = true;
        state.window.left = wp.rcNormalPosition.left;
        state.window.top = wp.rcNormalPosition.top;
        state.window.width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
        state.window.height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
        state.window.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    }

    state.favoritesWidth = g_favWidth;
    state.keyPanelWidth = g_keyWidth;
    state.previewHeight = g_previewHeight;

    CommitActiveSet();  // 作業用のタブをアクティブなセットへ反映してから保存する
    state.activeSet = g_activeSet;
    for (const TabSet& s : g_tabSets) {
        SessionTabSet saved;
        saved.name = s.name;
        saved.activeTabIndex = s.activeIndex;
        for (const TabState& t : s.tabs) {
            saved.tabs.push_back(SessionTabState{t.directory, t.cursorItemName,
                                                  static_cast<int>(t.sortKey), t.sortDescending,
                                                  static_cast<int>(t.viewMode)});
        }
        state.sets.push_back(std::move(saved));
    }
    return state;
}

// デバウンスタイマーを止めて即座に保存する (WM_QUERYENDSESSION 等、待てない場面用)。
void PersistSessionNow(HWND hwnd) {
    KillTimer(hwnd, kSessionSaveTimerId);
    HWND hList = GetDlgItem(hwnd, kIdFileListView);
    if (hList) {
        SaveActiveTabState(hList);  // アクティブタブの最新カーソル位置を反映
    }
    SaveSession(BuildCurrentSessionState(hwnd));
}

// 短いデバウンスを挟んでセッション保存を予約する。
void MarkSessionDirty(HWND hwnd) {
    SetTimer(hwnd, kSessionSaveTimerId, kSessionSaveDebounceMs, nullptr);
}

// ---- エイリアスの登録・編集 ----

// エイリアス名として使えるか (区切り文字・ドライブ指定の ':' を含まない)。
bool IsValidAliasName(const std::wstring& name) {
    return !name.empty() && name.find_first_of(L"\\/:") == std::wstring::npos;
}

// 登録・編集の共通ダイアログ。OK されて内容が有効なら true (name/key/path に結果が入る)。
// 無効な入力は、理由を示して同じ内容のまま入力し直させる。
bool PromptAliasFields(HWND hwnd, const std::wstring& title, std::wstring& name, wchar_t& key,
                       std::wstring& path, size_t editingIndex) {
    std::vector<DialogField> fields = {
        {Tr(L"名前"), name, 0},
        {Tr(L"呼び出しキー"), key ? std::wstring(1, key) : L"", 1},
        {Tr(L"フォルダ"), path, 0, FieldKind::Text, true},
    };
    DialogOptions options;
    options.baseDir = g_currentDir;
    options.hint = Tr(L"名前を入力すると、そのフォルダへ移動できます (G・O キー、HOME\\lib のような指定)。");

    for (;;) {
        if (!ShowFieldsDialog(hwnd, title, fields, options)) return false;

        std::wstring newName = fields[0].value;
        size_t b = newName.find_first_not_of(L" \t");
        size_t e = newName.find_last_not_of(L" \t");
        newName = b == std::wstring::npos ? L"" : newName.substr(b, e - b + 1);
        std::wstring folder = ResolvePath(g_currentDir, ExpandAliasPath(fields[2].value));

        if (!IsValidAliasName(newName)) {
            ShowMessage(hwnd, Tr(L"名前が空か、使えない文字 (\\ / :) を含んでいます。"), MB_ICONWARNING);
            continue;
        }
        if (!IsDirectoryPath(folder)) {
            ShowMessage(hwnd, Fmt(L"フォルダが見つかりません:\n{0}", {folder}), MB_ICONWARNING);
            continue;
        }
        const FavoriteEntry* sameName = FindFavoriteByName(newName);
        const std::vector<FavoriteEntry>& all = GetFavorites();
        if (sameName && static_cast<size_t>(sameName - all.data()) != editingIndex) {
            ShowMessage(hwnd, Fmt(L"同じ名前のエイリアスが既にあります: {0}", {newName}), MB_ICONWARNING);
            continue;
        }
        name = newName;
        key = fields[1].value.empty() ? 0 : fields[1].value[0];
        path = folder;
        return true;
    }
}

// Shift+J / Ctrl+D: カレントフォルダをエイリアスに登録する (名前・呼び出しキーを入力)。
void OnCmdRegisterAlias(HWND hwnd) {
    HWND hFav = GetDlgItem(hwnd, kIdFavoritesList);
    for (const FavoriteEntry& f : GetFavorites()) {
        if (EqualNoCase(f.path, g_currentDir)) {
            ShowMessage(hwnd, Fmt(L"このフォルダは既に「{0}」として登録されています。", {f.name}));
            return;
        }
    }
    std::wstring name = GetLastPathComponent(g_currentDir);
    for (wchar_t& c : name) {
        if (c == L'\\' || c == L'/' || c == L':') c = L' ';
    }
    size_t trim = name.find_last_not_of(L' ');
    name = trim == std::wstring::npos ? L"" : name.substr(0, trim + 1);
    wchar_t key = 0;
    std::wstring path = g_currentDir;
    if (!PromptAliasFields(hwnd, Tr(L"エイリアスに登録"), name, key, path, static_cast<size_t>(-1))) return;

    switch (AddFavorite(name, path, key)) {
        case AddResult::Added:
            RefreshFavoritesListBox(hFav);
            SetStatusMessage(hwnd, Fmt(L"エイリアス「{0}」を登録しました。", {name}));
            break;
        case AddResult::DuplicatePath:
            ShowMessage(hwnd, Tr(L"そのフォルダは既に登録されています。"), MB_ICONWARNING);
            break;
        case AddResult::DuplicateName:
            ShowMessage(hwnd, Tr(L"同じ名前のエイリアスが既にあります。"), MB_ICONWARNING);
            break;
    }
}

// 選択中のエイリアスの名前・呼び出しキー・フォルダを編集する (一覧上で F2)。
void EditSelectedFavorite(HWND hwnd, HWND hFav) {
    int index = static_cast<int>(SendMessageW(hFav, LB_GETCURSEL, 0, 0));
    const std::vector<FavoriteEntry>& favorites = GetFavorites();
    if (index < 0 || index >= static_cast<int>(favorites.size())) return;

    std::wstring name = favorites[index].name;
    wchar_t key = favorites[index].key;
    std::wstring path = favorites[index].path;
    if (!PromptAliasFields(hwnd, Tr(L"エイリアスを編集"), name, key, path, static_cast<size_t>(index))) return;

    UpdateFavorite(static_cast<size_t>(index), name, key, path);
    RefreshFavoritesListBox(hFav);
    if (const FavoriteEntry* edited = FindFavoriteByName(name)) {  // 名前順に並べ直されるので、編集した項目を探す
        SendMessageW(hFav, LB_SETCURSEL, static_cast<WPARAM>(edited - GetFavorites().data()), 0);
    }
}

// エイリアス一覧で項目を選んだときの実行部分。openInNewTab が true なら新しいタブで、
// そうでなければアクティブタブの移動先として開く。どちらも開いたらファイル一覧へフォーカスを戻す。
void ActivateFavorite(HWND hwnd, HWND hFav, bool openInNewTab) {
    int index = static_cast<int>(SendMessageW(hFav, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR) return;

    const std::vector<FavoriteEntry>& favorites = GetFavorites();
    if (index < 0 || index >= static_cast<int>(favorites.size())) return;

    std::wstring path = favorites[index].path;
    if (!IsDirectoryPath(path)) {
        ShowMessage(hwnd, Fmt(L"フォルダが見つかりません:\n{0}", {path}), MB_ICONWARNING);
        return;
    }
    HWND hList = GetDlgItem(hwnd, kIdFileListView);
    if (openInNewTab) {
        OnCmdNewTab(hwnd, hList, path);
    } else {
        NavigateToDirectory(hwnd, hList, path, L"");
    }
    FocusList(hwnd);
}

// 選択中のエイリアスを削除する。
void RemoveSelectedFavorite(HWND hFav) {
    int index = static_cast<int>(SendMessageW(hFav, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR) return;

    const std::vector<FavoriteEntry>& favorites = GetFavorites();
    if (index < 0 || index >= static_cast<int>(favorites.size())) return;

    RemoveFavoriteAt(static_cast<size_t>(index));
    RefreshFavoritesListBox(hFav);

    int count = static_cast<int>(SendMessageW(hFav, LB_GETCOUNT, 0, 0));
    if (count > 0) {
        SendMessageW(hFav, LB_SETCURSEL, std::min(index, count - 1), 0);
    }
}

// エイリアス一覧 (ListBox) のサブクラスプロシージャ。
// Enter/Shift+Enter (移動 / 新規ウィンドウ)、Delete (削除)、F2 (編集)、Esc (一覧へ戻る) を処理する。
LRESULT CALLBACK FavoritesListSubclassProc(HWND hFav, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR /*subclassId*/, DWORD_PTR /*refData*/) {
    switch (msg) {
        case WM_KEYDOWN: {
            HWND hwndParent = GetParent(hFav);
            if (wParam == VK_RETURN) {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                ActivateFavorite(hwndParent, hFav, shiftHeld);
                return 0;
            }
            if (wParam == VK_DELETE) {
                RemoveSelectedFavorite(hFav);
                return 0;
            }
            if (wParam == VK_F2) {
                EditSelectedFavorite(hwndParent, hFav);
                return 0;
            }
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            // Shift+呼び出しキー: そのエイリアスを新しいタブで開き、ファイル一覧へフォーカスを戻す。
            // Shift なしの文字キーは、リストボックス標準の頭文字検索 (その文字で始まる項目へ移動) に任せる。
            if (shift && !ctrl && !alt && ((wParam >= 'A' && wParam <= 'Z') || (wParam >= '0' && wParam <= '9'))) {
                int favIndex = FindFavoriteByKey(static_cast<wchar_t>(wParam));
                if (favIndex >= 0) {
                    MSG pendingChar;  // 続く WM_CHAR で頭文字検索が動かないよう捨てる
                    while (PeekMessageW(&pendingChar, hFav, WM_CHAR, WM_CHAR, PM_REMOVE)) {
                    }
                    SendMessageW(hFav, LB_SETCURSEL, favIndex, 0);
                    ActivateFavorite(hwndParent, hFav, true);
                    return 0;
                }
            }
            const KeyBinding* b = FindKeyBindingByKey(static_cast<WORD>(wParam), ctrl, alt, shift);
            if (b && b->command == L"FocusList") {
                FocusList(hwndParent);  // Esc でファイル一覧へ戻る (J などの文字キーは頭文字検索に使う)
                return 0;
            }
            break;
        }
        case WM_SETFOCUS:
            g_lastFocusedControl = hFav;
            break;
        default:
            break;
    }
    return DefSubclassProc(hFav, msg, wParam, lParam);
}

// ---- ソート・表示モード・マーク ----

void SetSort(HWND hwnd, HWND hList, SortKey key) {
    TabState& tab = ActiveTab();
    if (tab.sortKey == key) {
        tab.sortDescending = !tab.sortDescending;  // 同じキーをもう一度で昇順/降順を切り替える
    } else {
        tab.sortKey = key;
        tab.sortDescending = false;
    }
    RefreshFileListView(hList, CursorItemName(hList));
    MarkSessionDirty(hwnd);
}

void ToggleViewMode(HWND hwnd, HWND hList) {
    TabState& tab = ActiveTab();
    tab.viewMode = tab.viewMode == ViewMode::Detail ? ViewMode::List : ViewMode::Detail;
    RefreshFileListView(hList, CursorItemName(hList));
    RelayoutNow(hwnd);
    MarkSessionDirty(hwnd);
}

// マークの変化を一覧に反映する。背景も消して描き直す (マーク印の描き残しを防ぐ)。
void RepaintList(HWND hList) {
    RedrawWindow(hList, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// カーソル項目のマークを切り替え、カーソルを1つ下へ進める。
void ToggleMarkAtCursor(HWND hwnd, HWND hList) {
    int index = GetCursorItemIndex(hList);
    if (index < 0) return;
    ActiveTab().marks.Toggle(GetItemName(hList, index));
    int count = ListView_GetItemCount(hList);
    if (index + 1 < count) {
        SelectItem(hList, index + 1);
    }
    RepaintList(hList);
    g_statusMessage.clear();
    UpdateWindowTitle(hwnd);
}

void MarkAllItems(HWND hwnd, HWND hList) {
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; ++i) {
        ActiveTab().marks.Add(GetItemName(hList, i));
    }
    RepaintList(hList);
    g_statusMessage.clear();
    UpdateWindowTitle(hwnd);
}

void ClearAllMarks(HWND hwnd, HWND hList) {
    ActiveTab().marks.Clear();
    RepaintList(hList);
    g_statusMessage.clear();
    UpdateWindowTitle(hwnd);
}

// H: 1件でもマークがあれば全解除、無ければ全件マーク。
void ToggleMarkAll(HWND hwnd, HWND hList) {
    if (ActiveTab().marks.Count() > 0) {
        ClearAllMarks(hwnd, hList);
    } else {
        MarkAllItems(hwnd, hList);
    }
}

void InvertMarks(HWND hwnd, HWND hList) {
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; ++i) {
        ActiveTab().marks.Toggle(GetItemName(hList, i));
    }
    RepaintList(hList);
    g_statusMessage.clear();
    UpdateWindowTitle(hwnd);
}

// ---- ダークモード追従 ----

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// 設定 (自動 / ライト / ダーク) に合わせてタイトルバーとコントロールの配色を切り替える。
void ApplyTheme(HWND hwnd) {
    ThemeMode mode = GetSettings().theme;
    g_darkMode = mode == ThemeMode::Dark || (mode == ThemeMode::System && ThemeSystemIsDark());
    ThemeSetDark(g_darkMode);
    ThemeApplyAppMode(g_darkMode);

    BOOL useDark = g_darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    if (g_darkBrush) {
        DeleteObject(g_darkBrush);
        g_darkBrush = nullptr;
    }
    if (g_darkMode) {
        g_darkBrush = CreateSolidBrush(kDarkBackground);
    }

    HWND hList = GetDlgItem(hwnd, kIdFileListView);
    HWND hFav = GetDlgItem(hwnd, kIdFavoritesList);
    HWND hKeys = GetDlgItem(hwnd, kIdKeyPanel);
    HWND hTab = GetDlgItem(hwnd, kIdTabControl);
    HWND hStatus = GetDlgItem(hwnd, kIdStatus);
    const wchar_t* themeName = g_darkMode ? L"DarkMode_Explorer" : L"Explorer";
    for (HWND h : {hList, hFav, hKeys}) {
        if (h) SetWindowTheme(h, themeName, nullptr);
    }
    if (hFav) {  // ダークでは白い立体枠が浮くので外す
        LONG_PTR ex = GetWindowLongPtrW(hFav, GWL_EXSTYLE);
        SetWindowLongPtrW(hFav, GWL_EXSTYLE, g_darkMode ? (ex & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE)) : (ex | WS_EX_CLIENTEDGE));
        SetWindowPos(hFav, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    if (hTab) SetWindowTheme(hTab, L"", L"");  // タブは自前で描く (配色を揃えるため)
    for (HWND h : {hList, hKeys}) {
        if (h) ThemeStyleListView(h);
    }
    PreviewPaneApplyTheme();
    for (HWND h : {hList, hFav, hKeys, hTab, hStatus, hwnd}) {
        if (h) RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }
}

// アイコンの右下に重ねる、緑の丸に白いチェックのマーク印。
void DrawMarkBadge(HDC hdc, const RECT& iconRect, int dpi) {
    int s = MulDiv(11, dpi, 96);
    int left = iconRect.right - s + MulDiv(3, dpi, 96);
    int top = iconRect.bottom - s + MulDiv(3, dpi, 96);

    HBRUSH brush = CreateSolidBrush(RGB(0, 150, 70));
    HPEN ring = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, ring);
    Ellipse(hdc, left, top, left + s, top + s);

    HPEN check = CreatePen(PS_SOLID, std::max(1, s / 6), RGB(255, 255, 255));
    SelectObject(hdc, check);
    MoveToEx(hdc, left + s * 22 / 100, top + s * 52 / 100, nullptr);
    LineTo(hdc, left + s * 42 / 100, top + s * 72 / 100);
    LineTo(hdc, left + s * 78 / 100, top + s * 30 / 100);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(check);
    DeleteObject(ring);
    DeleteObject(brush);
}

LRESULT OnListCustomDraw(HWND hList, NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: {
            if (g_tabs.empty() || ActiveTab().marks.Count() == 0) {
                return CDRF_DODEFAULT;
            }
            int index = static_cast<int>(cd->nmcd.dwItemSpec);
            if (!ActiveTab().marks.Contains(GetItemName(hList, index))) {
                return CDRF_DODEFAULT;
            }
            // マークされた行は、項目の描画が終わった後 (事後描画) で色を重ねて印を付ける。
            // (ビジュアルスタイル適用中の一覧は、事前描画で指定した背景色を反映しないため。)
            return CDRF_NOTIFYPOSTPAINT;
        }
        case CDDS_ITEMPOSTPAINT: {
            int index = static_cast<int>(cd->nmcd.dwItemSpec);
            // 行全体に半透明の色を重ねる (文字は透けて読める。選択行でもマークされていると分かる)。
            RECT row;
            if (ListView_GetItemRect(hList, index, &row, LVIR_BOUNDS)) {
                HDC mem = CreateCompatibleDC(cd->nmcd.hdc);
                HBITMAP bmp = CreateCompatibleBitmap(cd->nmcd.hdc, 1, 1);
                HGDIOBJ old = SelectObject(mem, bmp);
                HBRUSH brush = CreateSolidBrush(g_darkMode ? RGB(215, 170, 40) : RGB(255, 200, 0));
                RECT one{0, 0, 1, 1};
                FillRect(mem, &one, brush);
                BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(g_darkMode ? 70 : 90), 0};
                AlphaBlend(cd->nmcd.hdc, row.left, row.top, row.right - row.left, row.bottom - row.top, mem, 0, 0, 1, 1,
                           blend);
                DeleteObject(brush);
                SelectObject(mem, old);
                DeleteObject(bmp);
                DeleteDC(mem);
            }
            // アイコンに重ねてチェック印を描く (選択行でもマークの有無が分かるようにする)。
            RECT iconRect;
            if (ListView_GetItemRect(hList, index, &iconRect, LVIR_ICON)) {
                DrawMarkBadge(cd->nmcd.hdc, iconRect, static_cast<int>(GetDpiForWindow(hList)));
            }
            return CDRF_DODEFAULT;
        }
        default:
            return CDRF_DODEFAULT;
    }
}

// キー一覧の描画: コマンドが割り当てられていない行は文字を灰色にする。
LRESULT OnKeyPanelCustomDraw(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: {
            size_t row = static_cast<size_t>(cd->nmcd.dwItemSpec);
            if (row < g_panelCommands.size() && g_panelCommands[row].empty()) {
                cd->clrText = GetSysColor(COLOR_GRAYTEXT);
                return CDRF_NEWFONT;
            }
            return CDRF_DODEFAULT;
        }
        default:
            return CDRF_DODEFAULT;
    }
}

// ---- キー割り当ての編集 (keymap.json) ----

HACCEL CreateAppAccelerators();
FILETIME g_keymapKnownTime{};

bool GetFileWriteTime(const std::wstring& path, FILETIME& out) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    out = data.ftLastWriteTime;
    return true;
}

// キー割り当てを読み直して、アクセラレータとキー一覧に反映する。
void ReloadKeyBindings(HWND hwnd) {
    g_keyBindings = LoadKeyConfig();
    GetFileWriteTime(GetKeyConfigFilePath(), g_keymapKnownTime);
    HACCEL old = g_hAccel;
    g_hAccel = CreateAppAccelerators();
    if (old) DestroyAcceleratorTable(old);
    RefreshKeyPanel(hwnd);
}

// ウィンドウに戻ったとき、keymap.json が (テキストエディタ等で) 変わっていれば読み直す。
bool ReloadKeyBindingsIfChanged(HWND hwnd) {
    FILETIME now{};
    if (!GetFileWriteTime(GetKeyConfigFilePath(), now) || CompareFileTime(&now, &g_keymapKnownTime) == 0) return false;
    ReloadKeyBindings(hwnd);
    return true;
}

void OnCmdEditKeymap(HWND hwnd) {
    std::wstring path = GetKeyConfigFilePath();
    if (path.empty()) return;
    if (EditFile(hwnd, path, L"", true) != EditResult::Started) {
        ShowMessage(hwnd, Fmt(L"エディタを起動できませんでした: {0}", {path}), MB_ICONWARNING);
    } else {
        SetStatusMessage(hwnd, Tr(L"keymap.json を編集して保存すると、このウィンドウに戻ったときに反映されます。"));
    }
}

void OnCmdResetKeymap(HWND hwnd) {
    if (ThemedMessageBox(hwnd, Tr(L"キー割り当てを既定値に戻します (今の内容は keymap.bak.json に残します)。\nよろしいですか?"), kWindowTitle,
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    std::wstring path = GetKeyConfigFilePath();
    std::wstring backup = GetConfigFilePath(L"keymap.bak.json");
    MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);
    ReloadKeyBindings(hwnd);  // ファイルが無いので既定値が書き出される
    SetStatusMessage(hwnd, Tr(L"キー割り当てを既定値に戻しました。"));
}

// ---- コマンドの実行 ----

// 言語を切り替えた後、画面上の文字列をすべて現在の言語で付け直す。
void ApplyLanguage(HWND hwnd) {
    HWND hList = GetDlgItem(hwnd, kIdFileListView);
    SetFileListColumnTexts(hList);
    SetDlgItemTextW(hwnd, kIdFavBar, Tr(L"  エイリアス"));
    SetDlgItemTextW(hwnd, kIdKeyBar, Tr(L"  キー操作"));
    HWND hKeys = GetDlgItem(hwnd, kIdKeyPanel);
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT;
    col.pszText = const_cast<LPWSTR>(Tr(L"機能"));
    ListView_SetColumn(hKeys, 1, &col);
    RefreshKeyPanel(hwnd);
    g_statusMessage.clear();
    UpdateWindowTitle(hwnd);
    InvalidateRect(GetDlgItem(hwnd, kIdLangBtn), nullptr, TRUE);
    PreviewPaneApplyLanguage();
}

void SetUiLanguage(HWND hwnd, Language lang) {
    if (GetLanguage() == lang) return;
    SetLanguage(lang);
    GetSettings().language = lang;
    SaveSettings();
    ApplyLanguage(hwnd);
}

void OnCmdSettings(HWND hwnd) {
    std::vector<KeyBinding> bindings = g_keyBindings;
    SettingsDialogResult r = ShowSettingsDialog(hwnd, bindings);
    if (!r.accepted) return;
    SaveSettings();
    SetLanguage(GetSettings().language);
    ApplyTheme(hwnd);
    if (r.keysChanged) {
        SaveKeyConfig(bindings);
        ReloadKeyBindings(hwnd);
    }
    ApplyLanguage(hwnd);
    SetStatusMessage(hwnd, Tr(L"設定を保存しました。"));
}

// 名前で指定されたコマンドを実行する。キー入力 (LVN_KEYDOWN / アクセラレータ) と
// キー一覧のクリックの両方から呼ばれる。
void ExecuteCommand(HWND hwnd, const std::wstring& command) {
    HWND hList = GetDlgItem(hwnd, kIdFileListView);
    HWND hFav = GetDlgItem(hwnd, kIdFavoritesList);
    if (g_tabs.empty()) return;

    // 移動・ナビゲーション
    if (command == L"NavigateInto") NavigateIntoSelected(hwnd, hList);
    else if (command == L"OpenWith") OpenWithSelected(hwnd, hList);
    else if (command == L"NavigateToParent") NavigateToParent(hwnd, hList);
    else if (command == L"NavigateToRoot") NavigateToRoot(hwnd, hList);
    else if (command == L"GotoFolder") OnCmdGotoFolder(hwnd, hList);
    else if (command == L"FocusList") FocusList(hwnd);
    else if (command == L"FocusAliasList") FocusAliasList(hwnd);
    else if (command == L"ToggleFocus") ToggleFocusBetweenListAndAliases(hwnd);
    else if (command == L"Refresh") {
        RefreshFileListView(hList, CursorItemName(hList));
        SetStatusMessage(hwnd, Tr(L"最新の情報に更新しました。"));
    }
    // タブ
    else if (command == L"NewTab") OnCmdNewTab(hwnd, hList);
    else if (command == L"OpenTabAt") OnCmdOpenTabAt(hwnd, hList);
    else if (command == L"CloseTab") OnCmdCloseTab(hwnd, hList);
    else if (command == L"NextTab") SwitchTabBy(hwnd, hList, +1);
    else if (command == L"PrevTab") SwitchTabBy(hwnd, hList, -1);
    else if (command == L"MoveTabLeft") MoveActiveTab(hwnd, hList, -1);
    else if (command == L"MoveTabRight") MoveActiveTab(hwnd, hList, +1);
    else if (command == L"TabSetMenu") OnCmdTabSetMenu(hwnd, hList);
    else if (command == L"NewTabSet") OnCmdNewTabSet(hwnd, hList);
    else if (command == L"RenameTabSet") OnCmdRenameTabSet(hwnd);
    else if (command == L"DeleteTabSet") OnCmdDeleteTabSet(hwnd, hList);
    // ファイル操作
    else if (command == L"CopyToTab") OnCmdTransfer(hwnd, hList, false, true);
    else if (command == L"CopyToFolder") OnCmdTransfer(hwnd, hList, false, false);
    else if (command == L"MoveToTab") OnCmdTransfer(hwnd, hList, true, true);
    else if (command == L"MoveToFolder") OnCmdTransfer(hwnd, hList, true, false);
    else if (command == L"Delete") OnCmdDelete(hwnd, hList, false);
    else if (command == L"DeletePermanent") OnCmdDelete(hwnd, hList, true);
    else if (command == L"Rename") OnCmdRename(hwnd, hList);
    else if (command == L"Duplicate") OnCmdDuplicate(hwnd, hList);
    else if (command == L"MakeFolder") OnCmdMakeFolder(hwnd, hList);
    else if (command == L"Attributes") OnCmdAttributes(hwnd, hList);
    else if (command == L"CreateShortcut") OnCmdCreateShortcut(hwnd, hList);
    else if (command == L"Zip") OnCmdZip(hwnd, hList);
    else if (command == L"Unzip") OnCmdUnzip(hwnd, hList);
    else if (command == L"UnzipToFolder") OnCmdUnzip(hwnd, hList, true);
    else if (command == L"Undo") OnCmdUndo(hwnd, hList);
    else if (command == L"Properties") {
        std::vector<std::wstring> targets = GatherTargets(hList);
        if (!targets.empty() && !ShowProperties(hwnd, targets)) {
            SetStatusMessage(hwnd, Tr(L"プロパティを表示できませんでした。"));
        }
    }
    else if (command == L"Edit") OnCmdEdit(hwnd, hList);
    else if (command == L"EditAliases") OnCmdEditAliases(hwnd);
    else if (command == L"TogglePreview") OnCmdTogglePreview(hwnd);
    else if (command == L"MaximizePreview") OnCmdMaximizePreview(hwnd);
    else if (command == L"FocusPreview") OnCmdFocusPreview(hwnd);
    else if (command == L"OpenPowerShell" || command == L"OpenCmd") {
        if (!LaunchTerminal(g_currentDir, command == L"OpenPowerShell")) {
            ShowMessage(hwnd, Tr(L"ターミナルを起動できませんでした。"), MB_ICONWARNING);
        }
    }
    // クリップボード
    else if (command == L"CopyPath") OnCmdCopyPath(hwnd, hList);
    else if (command == L"ClipCopy") OnCmdClipboardCopy(hwnd, hList, false);
    else if (command == L"ClipCut") OnCmdClipboardCopy(hwnd, hList, true);
    else if (command == L"Paste") OnCmdPaste(hwnd, hList);
    // 表示・ソート・マーク
    else if (command == L"SortName") SetSort(hwnd, hList, SortKey::Name);
    else if (command == L"SortExt") SetSort(hwnd, hList, SortKey::Ext);
    else if (command == L"SortSize") SetSort(hwnd, hList, SortKey::Size);
    else if (command == L"SortDate") SetSort(hwnd, hList, SortKey::Date);
    else if (command == L"ToggleView") ToggleViewMode(hwnd, hList);
    else if (command == L"ToggleMark") ToggleMarkAtCursor(hwnd, hList);
    else if (command == L"ToggleMarkAll") ToggleMarkAll(hwnd, hList);
    else if (command == L"MarkAll") MarkAllItems(hwnd, hList);
    else if (command == L"ClearMarks") ClearAllMarks(hwnd, hList);
    else if (command == L"InvertMarks") InvertMarks(hwnd, hList);
    // エイリアス・パネル
    else if (command == L"RegisterAlias") OnCmdRegisterAlias(hwnd);
    else if (command == L"EditSelectedAlias" || command == L"DeleteSelectedAlias") {
        if (SendMessageW(hFav, LB_GETCURSEL, 0, 0) == LB_ERR) {
            SetStatusMessage(hwnd, Tr(L"エイリアス欄で項目を選んでください。"));
        } else if (command == L"EditSelectedAlias") {
            EditSelectedFavorite(hwnd, hFav);
        } else {
            RemoveSelectedFavorite(hFav);
        }
    }
    else if (command == L"Settings") OnCmdSettings(hwnd);
    else if (command == L"EditKeymap") OnCmdEditKeymap(hwnd);
    else if (command == L"ResetKeymap") OnCmdResetKeymap(hwnd);
    else if (command == L"ToggleFavoritesPanel") {
        g_showFavoritesPanel = !g_showFavoritesPanel;
        if (!g_showFavoritesPanel && GetFocus() == hFav) FocusList(hwnd);
        RelayoutNow(hwnd);
    } else if (command == L"ToggleKeyPanel") {
        g_showKeyPanel = !g_showKeyPanel;
        RelayoutNow(hwnd);
    }
}

// 各バー右端の設定ボタンのメニュー。項目は (表示, コマンド名)。表示が null なら区切り線。
struct GearItem {
    const wchar_t* label;
    const wchar_t* command;
};

void ShowGearMenu(HWND hwnd, int gearId) {
    const GearItem kAliasItems[] = {
        {Tr(L"エイリアスに登録 (Shift+J)"), L"RegisterAlias"},
        {Tr(L"選択中のエイリアスを編集 (F2)"), L"EditSelectedAlias"},
        {Tr(L"選択中のエイリアスを削除 (Del)"), L"DeleteSelectedAlias"},
        {nullptr, nullptr},
        {Tr(L"定義ファイル favorites.json を編集 (Shift+E)"), L"EditAliases"},
    };
    const GearItem kTabItems[] = {
        {Tr(L"現在のフォルダで新しいタブ (T)"), L"NewTab"},
        {Tr(L"フォルダを指定して新しいタブ (O)"), L"OpenTabAt"},
        {Tr(L"タブを閉じる (Q)"), L"CloseTab"},
        {nullptr, nullptr},
        {Tr(L"タブを左へ移動 (Shift+,)"), L"MoveTabLeft"},
        {Tr(L"タブを右へ移動 (Shift+.)"), L"MoveTabRight"},
        {nullptr, nullptr},
        {Tr(L"タブセットを切り替え (L)..."), L"TabSetMenu"},
        {Tr(L"新しいタブセットを追加 (Shift+L)"), L"NewTabSet"},
        {Tr(L"このタブセットの名前を変更..."), L"RenameTabSet"},
        {Tr(L"このタブセットを削除..."), L"DeleteTabSet"},
    };
    const GearItem kKeyItems[] = {
        {Tr(L"設定... (キー割り当て・エディタ・言語・テーマ)"), L"Settings"},
        {Tr(L"プレビュー欄の表示/非表示 (V)"), L"TogglePreview"},
        {nullptr, nullptr},
        {Tr(L"キー割り当てを編集 (keymap.json)"), L"EditKeymap"},
        {Tr(L"キー割り当てを既定値に戻す..."), L"ResetKeymap"},
    };
    const GearItem* items = kKeyItems;
    size_t count = std::size(kKeyItems);
    if (gearId == kIdFavGear) {
        items = kAliasItems;
        count = std::size(kAliasItems);
    } else if (gearId == kIdTabGear) {
        items = kTabItems;
        count = std::size(kTabItems);
    }

    constexpr UINT kBase = 9301;
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < count; ++i) {
        if (items[i].label) AppendMenuW(menu, MF_STRING, kBase + i, items[i].label);
        else AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    RECT rc;
    GetWindowRect(GetDlgItem(hwnd, gearId), &rc);
    UINT selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, rc.right, rc.bottom, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    // ボタンがフォーカスを持ったままにならないよう、元のコントロールへ戻す。
    HWND restore = g_lastFocusedControl ? g_lastFocusedControl : GetDlgItem(hwnd, kIdFileListView);
    SetFocus(restore);
    if (selected >= kBase && selected < kBase + count) ExecuteCommand(hwnd, items[selected - kBase].command);
}

// 設定ボタンの描画: バーと同じ背景に歯車の記号。
void DrawGearButton(const DRAWITEMSTRUCT* dis) {
    COLORREF bg = g_darkMode ? RGB(48, 48, 48) : GetSysColor(COLOR_BTNFACE);
    COLORREF fg = g_darkMode ? kDarkText : GetSysColor(COLOR_BTNTEXT);
    if (dis->itemState & ODS_SELECTED) bg = g_darkMode ? RGB(70, 70, 70) : GetSysColor(COLOR_BTNSHADOW);
    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, brush);
    DeleteObject(brush);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    HGDIOBJ old = SelectObject(dis->hDC, g_gearFont ? g_gearFont : GetStockObject(DEFAULT_GUI_FONT));
    RECT rc = dis->rcItem;
    DrawTextW(dis->hDC, L"⚙", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dis->hDC, old);
}

// 言語切り替えボタンの描画: "JA | EN" のうち、いま使っている側を強調する。
void DrawLangButton(const DRAWITEMSTRUCT* dis) {
    RECT rc = dis->rcItem;
    HBRUSH bg = CreateSolidBrush(ThemeBarBg());
    FillRect(dis->hDC, &rc, bg);
    DeleteObject(bg);
    int mid = (rc.left + rc.right) / 2;
    RECT left{rc.left, rc.top, mid, rc.bottom};
    RECT right{mid, rc.top, rc.right, rc.bottom};
    bool ja = GetLanguage() == Language::Japanese;
    RECT active = ja ? left : right;
    InflateRect(&active, -2, -3);
    HBRUSH accent = CreateSolidBrush(RGB(0, 120, 215));
    FillRect(dis->hDC, &active, accent);
    DeleteObject(accent);
    SetBkMode(dis->hDC, TRANSPARENT);
    HGDIOBJ old = SelectObject(dis->hDC, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dis->hDC, ja ? RGB(255, 255, 255) : ThemeBarText());
    DrawTextW(dis->hDC, L"JA", 2, &left, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dis->hDC, ja ? ThemeBarText() : RGB(255, 255, 255));
    DrawTextW(dis->hDC, L"EN", 2, &right, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dis->hDC, old);
}

// アクセラレータ (どこにフォーカスがあっても効くキー) にするコマンド。
// 1キー操作 (修飾キー無しの文字キー) は入力欄に誤爆するのでここには含めず、一覧の LVN_KEYDOWN で処理する。
bool IsAccelerableCommand(const std::wstring& command) {
    static const wchar_t* kCommands[] = {L"ToggleFocus", L"NewTab", L"CloseTab", L"NextTab", L"PrevTab",
                                         L"RegisterAlias", L"ToggleFavoritesPanel", L"ToggleKeyPanel", L"Settings"};
    for (const wchar_t* c : kCommands) {
        if (command == c) return true;
    }
    return false;
}

// カーソルが合っている項目に対して、Explorer標準の右クリックメニューを表示する
// (IShellFolder::GetUIObjectOf 経由で IContextMenu を取得)。
// screenX/screenY が (-1,-1) の場合はキーボード起動 (メニューキー/Shift+F10) とみなし、
// 項目の位置から表示位置を計算する。フォルダの空白部分の右クリック (背景メニュー) は
// 今回のスコープ外 (項目が無ければ何もしない)。
void ShowFileContextMenu(HWND hwndOwner, HWND hList, int screenX, int screenY) {
    int index = GetCursorItemIndex(hList);
    if (index < 0) {
        return;
    }

    std::wstring name = GetItemName(hList, index);
    std::wstring fullPath = BuildFullPath(g_currentDir, name);

    if (screenX == -1 && screenY == -1) {
        RECT itemRect;
        if (ListView_GetItemRect(hList, index, &itemRect, LVIR_BOUNDS)) {
            POINT pt{itemRect.left, itemRect.bottom};
            ClientToScreen(hList, &pt);
            screenX = pt.x;
            screenY = pt.y;
        } else {
            screenX = screenY = 0;
        }
    }

    PIDLIST_ABSOLUTE pidlFull = nullptr;
    if (FAILED(SHParseDisplayName(fullPath.c_str(), nullptr, &pidlFull, 0, nullptr)) || !pidlFull) {
        return;
    }

    IShellFolder* parentFolder = nullptr;
    PCUITEMID_CHILD childPidl = nullptr;
    HRESULT hr = SHBindToParent(pidlFull, IID_PPV_ARGS(&parentFolder), &childPidl);
    if (SUCCEEDED(hr)) {
        IContextMenu* contextMenu = nullptr;
        hr = parentFolder->GetUIObjectOf(hwndOwner, 1, &childPidl, IID_IContextMenu, nullptr,
                                          reinterpret_cast<void**>(&contextMenu));
        if (SUCCEEDED(hr) && contextMenu) {
            HMENU hMenu = CreatePopupMenu();
            constexpr UINT kCmdFirst = 1;
            constexpr UINT kCmdLast = 0x7000;
            contextMenu->QueryContextMenu(hMenu, 0, kCmdFirst, kCmdLast, CMF_NORMAL | CMF_EXPLORE);

            // シェル拡張が要求する描画/メッセージをメインウィンドウから転送できるよう、
            // 表示中だけ IContextMenu2/3 を控えておく (WM_INITMENUPOPUP 等)。
            contextMenu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(&g_activeContextMenu3));
            if (!g_activeContextMenu3) {
                contextMenu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&g_activeContextMenu2));
            }

            UINT cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                         screenX, screenY, hwndOwner, nullptr);
            PostMessageW(hwndOwner, WM_NULL, 0, 0);  // メニューが正しく閉じるようにする (MSDN推奨)

            if (g_activeContextMenu3) {
                g_activeContextMenu3->Release();
                g_activeContextMenu3 = nullptr;
            }
            if (g_activeContextMenu2) {
                g_activeContextMenu2->Release();
                g_activeContextMenu2 = nullptr;
            }

            if (cmd >= kCmdFirst) {
                CMINVOKECOMMANDINFOEX info{};
                info.cbSize = sizeof(info);
                info.fMask = CMIC_MASK_UNICODE;
                info.hwnd = hwndOwner;
                info.lpVerb = MAKEINTRESOURCEA(cmd - kCmdFirst);
                info.lpVerbW = MAKEINTRESOURCEW(cmd - kCmdFirst);
                info.lpDirectory = nullptr;
                info.lpDirectoryW = g_currentDir.c_str();
                info.nShow = SW_SHOWNORMAL;
                contextMenu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
            }

            DestroyMenu(hMenu);
            contextMenu->Release();
        }
        parentFolder->Release();
    }
    CoTaskMemFree(pidlFull);

    // 削除・リネーム・移動等、シェルコマンドが実行された可能性があるので一覧を更新する。
    RefreshFileListView(hList, name);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(
                GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            HWND hFav = CreateFavoritesListBox(hwnd, hInstance);
            HWND hTab = CreateTabStrip(hwnd, hInstance);
            HWND hList = CreateFileListView(hwnd, hInstance);
            CreateKeyPanel(hwnd, hInstance);
            g_preview = PreviewPaneCreate(hwnd);
            if (g_preview) SetWindowLongPtrW(g_preview, GWLP_ID, kIdPreview);
            CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX |
                                                   SS_ENDELLIPSIS,
                            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStatus)),
                            hInstance, nullptr);
            // 各欄の見出しバーと、右端の設定ボタン (タブストリップは hTab 自体がバー)。
            auto makeBar = [&](const wchar_t* text, int id) {
                CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
            };
            auto makeGear = [&](int id) {
                CreateWindowExW(0, L"BUTTON", Tr(L"設定"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY,
                                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
            };
            makeBar(Tr(L"  エイリアス"), kIdFavBar);
            makeBar(Tr(L"  キー操作"), kIdKeyBar);
            makeGear(kIdFavGear);
            makeGear(kIdTabGear);
            makeGear(kIdKeyGear);
            CreateWindowExW(0, L"BUTTON", L"JA | EN", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY, 0, 0, 0, 0, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLangBtn)), hInstance, nullptr);

            // 全コントロールで同じフォントを使う。
            int dpi = static_cast<int>(GetDpiForWindow(hwnd));
            g_uiFont = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
            g_gearFont = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
            if (g_uiFont) {
                for (int id : {kIdFavoritesList, kIdTabControl, kIdFileListView, kIdKeyPanel, kIdStatus, kIdFavBar,
                               kIdKeyBar}) {
                    SendDlgItemMessageW(hwnd, id, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
                }
            }

            RefreshFavoritesListBox(hFav);
            RefreshKeyPanel(hwnd);

            // g_tabs はセッション復元によって wWinMain 側で既に埋まっている場合がある。
            // 空 (保存データが無かった/全て無効だった) 場合のみ、カレントディレクトリの
            // 単一タブとしてデフォルト初期化する。
            if (g_tabs.empty()) {
                wchar_t currentDir[MAX_PATH];
                GetCurrentDirectoryW(MAX_PATH, currentDir);
                TabState tab;
                tab.directory = currentDir;
                g_tabs.push_back(std::move(tab));
                g_activeTabIndex = 0;
            }
            EnsureTabSets();

            for (size_t i = 0; i < g_tabs.size(); ++i) {
                TCITEMW tabItem{};
                tabItem.mask = TCIF_TEXT;
                std::wstring label = GetLastPathComponent(g_tabs[i].directory);
                tabItem.pszText = const_cast<LPWSTR>(label.c_str());
                TabCtrl_InsertItem(hTab, static_cast<int>(i), &tabItem);
            }
            TabCtrl_SetCurSel(hTab, g_activeTabIndex);
            LoadActiveTabState(hwnd, hList);

            g_mainHwnd = hwnd;
            g_dropTargetList = hList;

            ApplyTheme(hwnd);

            SetFocus(hList);
            g_lastFocusedControl = hList;
            return 0;
        }
        case WM_SETFOCUS: {
            HWND hList = GetDlgItem(hwnd, kIdFileListView);
            SetFocus(g_lastFocusedControl ? g_lastFocusedControl : hList);
            return 0;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wParam) != WA_INACTIVE) {
                // 他のアプリ (テキストエディタ等) で favorites.json が編集されていれば読み直す。
                if (ReloadFavoritesIfChanged()) {
                    RefreshFavoritesListBox(GetDlgItem(hwnd, kIdFavoritesList));
                    SetStatusMessage(hwnd, Tr(L"エイリアスの変更を読み込みました。"));
                }
                if (ReloadKeyBindingsIfChanged(hwnd)) {
                    SetStatusMessage(hwnd, Tr(L"キー割り当ての変更を読み込みました。"));
                }
                SetKeyPanelShift(hwnd, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            } else {
                SetKeyPanelShift(hwnd, false);
            }
            break;
        }
        case WM_SIZE: {
            ResizeLayout(hwnd, LOWORD(lParam), HIWORD(lParam));
            MarkSessionDirty(hwnd);
            return 0;
        }
        case WM_MOVE: {
            MarkSessionDirty(hwnd);
            return 0;
        }
        case WM_SETTINGCHANGE: {
            // OS のライト/ダーク切替 ("ImmersiveColorSet") に追従する。
            if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
                ApplyTheme(hwnd);
            }
            break;
        }
        case WM_ERASEBKGND: {
            // 欄の間のすき間 (スプリッター) の色。ダークモードで白く浮かないようにする。
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(g_darkMode ? RGB(48, 48, 48) : GetSysColor(COLOR_BTNFACE));
            FillRect(reinterpret_cast<HDC>(wParam), &rc, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_LBUTTONDOWN: {
            // 欄の境界 (スプリッター) のドラッグ開始。
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (y < g_splitters.height) {
                if (g_splitters.favX >= 0 && x >= g_splitters.favX && x < g_splitters.favX + g_splitters.width) {
                    g_dragSplitter = 1;
                } else if (g_splitters.keyX >= 0 && x >= g_splitters.keyX && x < g_splitters.keyX + g_splitters.width) {
                    g_dragSplitter = 2;
                }
            }
            // ファイル一覧とプレビュー欄の境界 (横長)
            if (!g_dragSplitter && g_splitters.prevY >= 0 && y >= g_splitters.prevY && y < g_splitters.prevY + g_splitters.width &&
                x >= g_splitters.prevX && x < g_splitters.prevX + g_splitters.prevW) {
                g_dragSplitter = 3;
            }
            if (g_dragSplitter) SetCapture(hwnd);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_dragSplitter) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                int x = GET_X_LPARAM(lParam);
                int split = g_splitters.width;
                int minPane = ScaleForWindow(hwnd, kMinPaneWidth);
                int minCenter = ScaleForWindow(hwnd, kMinCenterWidth);
                if (g_dragSplitter == 3) {
                    // 境界の中心がマウスの位置になるように、プレビュー欄の高さを決める。
                    int y = GET_Y_LPARAM(lParam);
                    int bodyHeight = rc.bottom - ScaleForWindow(hwnd, kStatusHeight);
                    g_previewHeight = std::max(ScaleForWindow(hwnd, kMinPreviewHeight), bodyHeight - (y + split / 2));
                } else if (g_dragSplitter == 1) {
                    int keyWidth = g_showKeyPanel ? g_keyWidth + split : 0;
                    g_favWidth = std::clamp(x - split / 2, minPane, std::max(minPane, static_cast<int>(rc.right) - keyWidth - split - minCenter));
                } else {
                    int favWidth = g_showFavoritesPanel ? g_favWidth + split : 0;
                    g_keyWidth = std::clamp(static_cast<int>(rc.right) - x - split / 2, minPane,
                                            std::max(minPane, static_cast<int>(rc.right) - favWidth - split - minCenter));
                }
                ResizeLayout(hwnd, rc.right, rc.bottom);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (g_dragSplitter) {
                g_dragSplitter = 0;
                ReleaseCapture();
                MarkSessionDirty(hwnd);
                return 0;
            }
            break;
        }
        case WM_CAPTURECHANGED: {
            g_dragSplitter = 0;
            break;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                bool onFav = g_splitters.favX >= 0 && pt.x >= g_splitters.favX && pt.x < g_splitters.favX + g_splitters.width;
                bool onKey = g_splitters.keyX >= 0 && pt.x >= g_splitters.keyX && pt.x < g_splitters.keyX + g_splitters.width;
                bool onPrev = g_splitters.prevY >= 0 && pt.y >= g_splitters.prevY && pt.y < g_splitters.prevY + g_splitters.width &&
                              pt.x >= g_splitters.prevX && pt.x < g_splitters.prevX + g_splitters.prevW;
                if (g_dragSplitter == 3 || onPrev) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                    return TRUE;
                }
                if (g_dragSplitter || ((onFav || onKey) && pt.y < g_splitters.height)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        }
        case WM_DRAWITEM: {
            if (wParam == kIdFavGear || wParam == kIdTabGear || wParam == kIdKeyGear) {
                DrawGearButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
                return TRUE;
            }
            if (wParam == kIdLangBtn) {
                DrawLangButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
                return TRUE;
            }
            LRESULT result = 0;  // それ以外はシェルのメニュー拡張 (右クリックメニュー) への転送
            if (g_activeContextMenu3 &&
                SUCCEEDED(g_activeContextMenu3->HandleMenuMsg2(msg, wParam, lParam, &result))) {
                return result;
            }
            if (g_activeContextMenu2 &&
                SUCCEEDED(g_activeContextMenu2->HandleMenuMsg(msg, wParam, lParam))) {
                return 0;
            }
            break;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSTATIC: {
            // 見出しバー (エイリアス・キー操作) は、一覧より少し濃い色にする。
            int ctrlId = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
            if (msg == WM_CTLCOLORSTATIC && (ctrlId == kIdFavBar || ctrlId == kIdKeyBar || ctrlId == kIdStatus)) {
                static HBRUSH darkBar = CreateSolidBrush(RGB(48, 48, 48));
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, g_darkMode ? kDarkText : GetSysColor(COLOR_BTNTEXT));
                SetBkColor(hdc, g_darkMode ? RGB(48, 48, 48) : GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(g_darkMode ? darkBar : GetSysColorBrush(COLOR_BTNFACE));
            }
            if (g_darkMode && g_darkBrush) {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, kDarkText);
                SetBkColor(hdc, kDarkBackground);
                return reinterpret_cast<LRESULT>(g_darkBrush);
            }
            break;
        }
        case WM_TIMER: {
            if (wParam == kSessionSaveTimerId) {
                PersistSessionNow(hwnd);
                return 0;
            }
            if (wParam == kStatusClearTimerId) {
                KillTimer(hwnd, kStatusClearTimerId);
                g_statusMessage.clear();
                UpdateStatusBar(hwnd);
                return 0;
            }
            if (wParam == kPreviewTimerId) {
                KillTimer(hwnd, kPreviewTimerId);
                UpdatePreviewNow(hwnd);
                return 0;
            }
            break;
        }
        case kMsgPreviewReturn: {
            // プレビュー欄から一覧へ戻る (Esc / Tab)。広げていたら元に戻す。
            if (g_previewMax) {
                g_previewMax = false;
                RelayoutNow(hwnd);
            }
            FocusList(hwnd);
            SchedulePreviewUpdate(hwnd);
            return 0;
        }
        case kMsgPreviewStep: {
            StepPreviewCursor(hwnd, static_cast<int>(wParam));
            return 0;
        }
        case kMsgPreviewClose: {
            SetPreviewVisible(hwnd, false);
            return 0;
        }
        case WM_QUERYENDSESSION: {
            // ログオフ・シャットダウン要求時は、デバウンスを待たず同期的に保存する。
            PersistSessionNow(hwnd);
            return TRUE;
        }
        case kMsgArchiveDone: {
            OnArchiveDone(hwnd, reinterpret_cast<ArchiveResult*>(lParam));
            return 0;
        }
        case kMsgPanelExec: {
            // キー一覧のクリック: ファイル一覧へフォーカスを戻してからコマンドを実行する。
            size_t row = static_cast<size_t>(wParam);
            HWND hKeys = GetDlgItem(hwnd, kIdKeyPanel);
            ListView_SetItemState(hKeys, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            if (row < g_panelCommands.size() && !g_panelCommands[row].empty()) {
                FocusList(hwnd);
                ExecuteCommand(hwnd, g_panelCommands[row]);
            }
            return 0;
        }
        case WM_INITMENUPOPUP:
        case WM_MEASUREITEM:
        case WM_MENUCHAR: {
            // 右クリックメニュー表示中のみ、シェル拡張が要求するオーナードロー等のメッセージを
            // IContextMenu2/3 に転送する (ShowFileContextMenu 参照)。
            LRESULT result = 0;
            if (g_activeContextMenu3 &&
                SUCCEEDED(g_activeContextMenu3->HandleMenuMsg2(msg, wParam, lParam, &result))) {
                return result;
            }
            if (g_activeContextMenu2 &&
                SUCCEEDED(g_activeContextMenu2->HandleMenuMsg(msg, wParam, lParam))) {
                return 0;
            }
            break;
        }
        case WM_COMMAND: {
            HWND hFav = GetDlgItem(hwnd, kIdFavoritesList);

            if (LOWORD(wParam) == static_cast<WORD>(kIdFavoritesList) && HIWORD(wParam) == LBN_DBLCLK) {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                ActivateFavorite(hwnd, hFav, shiftHeld);
                return 0;
            }

            // 右下の言語切り替え (左半分 = 日本語 / 右半分 = English)
            if (LOWORD(wParam) == static_cast<WORD>(kIdLangBtn) &&
                (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == BN_DOUBLECLICKED)) {
                HWND hLang = GetDlgItem(hwnd, kIdLangBtn);
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hLang, &pt);
                RECT rc;
                GetClientRect(hLang, &rc);
                SetUiLanguage(hwnd, pt.x < rc.right / 2 ? Language::Japanese : Language::English);
                FocusList(hwnd);
                return 0;
            }

            // 各バー右端の設定ボタン
            if (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == BN_DOUBLECLICKED) {
                int gear = static_cast<int>(LOWORD(wParam));
                if (gear == kIdFavGear || gear == kIdTabGear || gear == kIdKeyGear) {
                    ShowGearMenu(hwnd, gear);
                    return 0;
                }
            }

            // アクセラレータ (HIWORD(wParam) == 1)
            int id = static_cast<int>(LOWORD(wParam));
            if (HIWORD(wParam) == 1 && id >= kAccelBase && id < kAccelBase + static_cast<int>(g_accelCommands.size())) {
                ExecuteCommand(hwnd, g_accelCommands[id - kAccelBase]);
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            const NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
            HWND hList = GetDlgItem(hwnd, kIdFileListView);

            if (hdr->idFrom == static_cast<UINT_PTR>(kIdFileListView)) {
                if (hdr->code == LVN_ENDLABELEDIT) {
                    OnEndLabelEdit(hwnd, hdr->hwndFrom, reinterpret_cast<NMLVDISPINFOW*>(lParam));
                    return TRUE;
                }
                if (hdr->code == NM_SETFOCUS) {
                    g_lastFocusedControl = hdr->hwndFrom;
                }
                if (hdr->code == LVN_ITEMCHANGED && g_showPreview) {
                    const NMLISTVIEW* nm = reinterpret_cast<const NMLISTVIEW*>(lParam);
                    if ((nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & (LVIS_FOCUSED | LVIS_SELECTED))) {
                        SchedulePreviewUpdate(hwnd);
                    }
                }
                if (hdr->code == NM_DBLCLK) {
                    NavigateIntoSelected(hwnd, hList);
                }
                if (hdr->code == LVN_BEGINDRAG) {
                    OnBeginDrag(hwnd, hList);
                }
                if (hdr->code == LVN_KEYDOWN) {
                    // 1キー操作はここでリストビューが実際にフォーカスを持つ時だけ処理する
                    // (名前変更中の入力欄・エイリアス一覧にフォーカスがある間に誤爆しないため)。
                    // 実際の割り当ては keymap.json (無ければ既定値) に従う。
                    const NMLVKEYDOWN* keyDown = reinterpret_cast<const NMLVKEYDOWN*>(lParam);
                    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (const KeyBinding* b = FindKeyBindingByKey(keyDown->wVKey, ctrl, alt, shift)) {
                        // TranslateMessage が WM_KEYDOWN の前に積んだ WM_CHAR を捨てる。残すと、
                        // ダイアログ・メニューにその文字が入力されてしまう
                        // (一覧の先頭文字検索の誤動作も防ぐ)。
                        MSG pendingChar;
                        while (PeekMessageW(&pendingChar, hdr->hwndFrom, WM_CHAR, WM_CHAR, PM_REMOVE)) {
                        }
                        g_commandKeyTick = GetTickCount64();  // これから届く WM_CHAR も捨てる (FileListSubclassProc)
                        ExecuteCommand(hwnd, b->command);
                        g_commandKeyTick = GetTickCount64();
                    }
                }
                if (hdr->code == LVN_COLUMNCLICK) {
                    // 列見出しのクリックでその列をキーにソートする (もう一度で昇順/降順切替)。
                    static const SortKey kColumnKeys[] = {SortKey::Name, SortKey::Size, SortKey::Ext, SortKey::Date};
                    int column = reinterpret_cast<const NMLISTVIEW*>(lParam)->iSubItem;
                    if (column >= 0 && column < 4) {
                        SetSort(hwnd, hList, kColumnKeys[column]);
                    }
                }
                if (hdr->code == NM_CUSTOMDRAW) {
                    return OnListCustomDraw(hList, reinterpret_cast<NMLVCUSTOMDRAW*>(lParam));
                }
            } else if (hdr->idFrom == static_cast<UINT_PTR>(kIdKeyPanel)) {
                if (hdr->code == LVN_ITEMACTIVATE) {
                    int row = reinterpret_cast<const NMITEMACTIVATE*>(lParam)->iItem;
                    if (row >= 0) {
                        PostMessageW(hwnd, kMsgPanelExec, static_cast<WPARAM>(row), 0);
                    }
                }
                if (hdr->code == NM_CUSTOMDRAW) {
                    return OnKeyPanelCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(lParam));
                }
            } else if (hdr->idFrom == static_cast<UINT_PTR>(kIdTabControl) &&
                       hdr->code == TCN_SELCHANGE) {
                OnTabControlSelChange(hwnd, hList, hdr->hwndFrom);
            }
            break;
        }
        case WM_DESTROY: {
            if (g_darkBrush) {
                DeleteObject(g_darkBrush);
                g_darkBrush = nullptr;
            }
            if (g_uiFont) {
                DeleteObject(g_uiFont);
                g_uiFont = nullptr;
            }
            if (g_gearFont) {
                DeleteObject(g_gearFont);
                g_gearFont = nullptr;
            }
            FlushFileClipboard();     // コピー/切り取り中のファイルをアプリ終了後も貼り付け可能にする
            PersistSessionNow(hwnd);  // デバウンスを待たず最終保存
            HWND hList = GetDlgItem(hwnd, kIdFileListView);
            if (hList) {
                RemoveWindowSubclass(hList, FileListSubclassProc, kFileListSubclassId);
            }
            HWND hFav = GetDlgItem(hwnd, kIdFavoritesList);
            if (hFav) {
                RemoveWindowSubclass(hFav, FavoritesListSubclassProc, kFavoritesListSubclassId);
            }
            PostQuitMessage(0);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// RegisterApplicationRecoveryCallback から OS が別スレッドで呼ぶ。メインスレッドがハングして
// いる可能性があるため、UI へメッセージを送らず (SendMessage するとこちらもハングする)、
// メモリ上のタブ状態だけからセッションを保存する。
DWORD WINAPI RecoveryCallback(PVOID) {
    BOOL canceled = FALSE;
    ApplicationRecoveryInProgress(&canceled);
    if (!canceled && g_mainHwnd) {
        SaveSession(BuildCurrentSessionState(g_mainHwnd));
    }
    ApplicationRecoveryFinished(!canceled);
    return 0;
}

HWND CreateMainWindow(HINSTANCE hInstance, int nCmdShow, const SessionWindowState& savedWindow) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;
    // アイコンは exe のリソース (res\filM.ico)。大 (タスクバー・Alt+Tab) と小 (タイトルバー) を大きさに合わせて読む。
    wc.hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    if (!RegisterClassExW(&wc)) {
        return nullptr;
    }

    // ウィンドウ位置のクランプ: 保存された位置・大きさを、現在のモニター構成の作業領域内に
    // 収まるよう補正する (モニター構成を変えた後に画面外へ消えるのを防ぐ)。
    // 保存位置が全モニターの外にある場合は、最寄りのモニターへ寄せる。
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1200, h = 768;
    if (savedWindow.valid) {
        RECT candidate{savedWindow.left, savedWindow.top,
                        savedWindow.left + savedWindow.width, savedWindow.top + savedWindow.height};
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        HMONITOR monitor = MonitorFromRect(&candidate, MONITOR_DEFAULTTONEAREST);
        if (monitor && GetMonitorInfoW(monitor, &mi)) {
            const RECT& work = mi.rcWork;
            w = std::min(std::max(savedWindow.width, 320), static_cast<int>(work.right - work.left));
            h = std::min(std::max(savedWindow.height, 240), static_cast<int>(work.bottom - work.top));
            x = std::clamp(savedWindow.left, static_cast<int>(work.left), static_cast<int>(work.right) - w);
            y = std::clamp(savedWindow.top, static_cast<int>(work.top), static_cast<int>(work.bottom) - h);
        }
    }

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        nullptr, nullptr, hInstance, nullptr);

    if (hwnd) {
        ShowWindow(hwnd, (savedWindow.valid && savedWindow.maximized) ? SW_SHOWMAXIMIZED : nCmdShow);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

// g_keyBindings のうち「どこにフォーカスがあっても効く」コマンドを、アクセラレータテーブルへ変換する。
// 修飾キー無しの文字キー (1キー操作) は含めない (名前変更の入力欄などに誤爆するため)。
HACCEL CreateAppAccelerators() {
    std::vector<ACCEL> accels;
    g_accelCommands.clear();
    for (const KeyBinding& b : g_keyBindings) {
        if (!IsAccelerableCommand(b.command)) {
            continue;
        }
        WORD vk = KeyNameToVirtualKey(b.key);
        if (vk == 0) {
            continue;
        }
        bool functionKey = vk >= VK_F1 && vk <= VK_F12;
        if (!b.ctrl && !b.alt && !functionKey) {
            continue;
        }
        BYTE fVirt = FVIRTKEY;
        if (b.ctrl) fVirt |= FCONTROL;
        if (b.alt) fVirt |= FALT;
        if (b.shift) fVirt |= FSHIFT;

        auto it = std::find(g_accelCommands.begin(), g_accelCommands.end(), b.command);
        if (it == g_accelCommands.end()) {
            g_accelCommands.push_back(b.command);
            it = g_accelCommands.end() - 1;
        }
        accels.push_back(ACCEL{fVirt, vk, static_cast<WORD>(kAccelBase + (it - g_accelCommands.begin()))});
    }
    return CreateAcceleratorTableW(accels.data(), static_cast<int>(accels.size()));
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    // DLL の検索先からカレントフォルダを外す (作業中のフォルダに置かれた偽の DLL を読み込まないため)。
    SetDllDirectoryW(L"");

    // ドラッグ&ドロップ (RegisterDragDrop/DoDragDrop) が必要とするため、単なる
    // CoInitializeEx ではなく OleInitialize を使う (内部で COM 初期化も行われる)。
    OleInitialize(nullptr);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);

    // Windows Update等によるOS再起動後、Windows側がアプリを自動的に再起動できるようにする
    // (セッションは起動時に自動復元されるため、コマンドライン引数の指定は不要)。
    RegisterApplicationRestart(nullptr, 0);

    // ハングと判断されて OS に強制終了される直前に、保存の猶予をもらう (保険。常時保存が主策)。
    RegisterApplicationRecoveryCallback(RecoveryCallback, nullptr, RECOVERY_DEFAULT_PING_INTERVAL, 0);

    // 表示言語・テーマ・エディタの設定 (settings.json)。
    LoadSettings();

    // キー設定を読み込む (ファイルが無ければ既定値を書き出してユーザーが編集できるようにする)。
    g_keyBindings = LoadKeyConfig();

    // 複数起動時、セッションの所有者になれるのは最初のインスタンスのみ (後発は読み書きしない)。
    AcquirePrimaryInstance();

    // エイリアス (フォルダのお気に入り) の読み込み。
    LoadFavorites();

    // 保存済みセッションを読み込み、タブを復元する (確認ダイアログなしで無条件に復元)。
    // ディレクトリが既に存在しないタブは読み飛ばす。1つも復元できなければ、
    // WM_CREATE 側でカレントディレクトリの単一タブとしてデフォルト初期化される。
    SessionState session = LoadSession();
    g_favWidth = session.favoritesWidth;
    g_keyWidth = session.keyPanelWidth;
    g_previewHeight = std::max(0, session.previewHeight);
    for (const SessionTabSet& savedSet : session.sets) {
        TabSet set;
        set.name = savedSet.name.empty() ? Fmt(L"セット{0}", {Num(g_tabSets.size() + 1)}) : savedSet.name;
        for (const SessionTabState& saved : savedSet.tabs) {
            DWORD attrs = GetFileAttributesW(saved.directory.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                TabState tab;
                tab.directory = saved.directory;
                tab.cursorItemName = saved.cursorItem;
                tab.sortKey = saved.sortKey >= 0 && saved.sortKey <= 3 ? static_cast<SortKey>(saved.sortKey) : SortKey::Name;
                tab.sortDescending = saved.sortDescending;
                tab.viewMode = saved.viewMode == 1 ? ViewMode::List : ViewMode::Detail;
                set.tabs.push_back(std::move(tab));
            }
        }
        if (set.tabs.empty()) continue;  // フォルダが 1 つも残っていないセットは復元しない
        set.activeIndex = std::clamp(savedSet.activeTabIndex, 0, static_cast<int>(set.tabs.size()) - 1);
        g_tabSets.push_back(std::move(set));
    }
    if (!g_tabSets.empty()) {
        g_activeSet = std::clamp(session.activeSet, 0, static_cast<int>(g_tabSets.size()) - 1);
        g_tabs = g_tabSets[g_activeSet].tabs;
        g_activeTabIndex = g_tabSets[g_activeSet].activeIndex;
    }

    GetFileWriteTime(GetKeyConfigFilePath(), g_keymapKnownTime);
    g_hAccel = CreateAppAccelerators();

    HWND hwndMain = CreateMainWindow(hInstance, nCmdShow, session.window);
    if (!hwndMain) {
        OleUninitialize();
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Shift キーの押下・解放に合わせて、右側のキー一覧を Shift+キー の表示に切り替える。
        if ((msg.message == WM_KEYDOWN || msg.message == WM_KEYUP) && msg.wParam == VK_SHIFT &&
            (msg.hwnd == hwndMain || IsChild(hwndMain, msg.hwnd))) {
            SetKeyPanelShift(hwndMain, msg.message == WM_KEYDOWN);
        }
        // マウスホイール: プレビュー欄の上にマウスがあれば、一覧にフォーカスがあってもそちらをスクロールする。
        if (msg.message == WM_MOUSEWHEEL && g_preview && g_showPreview && IsWindowVisible(g_preview) && !IsChild(g_preview, msg.hwnd)) {
            POINT pt{static_cast<short>(LOWORD(msg.lParam)), static_cast<short>(HIWORD(msg.lParam))};
            RECT pr;
            GetWindowRect(g_preview, &pr);
            if (PtInRect(&pr, pt)) {
                PreviewPaneWheel(static_cast<short>(HIWORD(msg.wParam)));
                continue;
            }
        }
        if (TranslateAcceleratorW(hwndMain, g_hAccel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hAccel) DestroyAcceleratorTable(g_hAccel);
    OleUninitialize();
    return static_cast<int>(msg.wParam);
}











