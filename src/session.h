// セッション永続化 (タブ状態・ウィンドウ位置の保存/復元)
//
// 保存先: exe と同じフォルダ\session.json (UTF-16LE, BOM付き)
// 書き込みは一時ファイル→リネームのアトミック方式。
// 起動時は保存内容を無条件で復元する (確認ダイアログは挟まない)。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct SessionTabState {
    std::wstring directory;
    std::wstring cursorItem;
    int sortKey = 0;         // file_list.h の SortKey の値
    bool sortDescending = false;
    int viewMode = 0;        // 0=詳細, 1=一覧 (名前のみ)
};

struct SessionWindowState {
    bool valid = false;  // 保存データが無かった/壊れていた場合 false
    int left = 0;
    int top = 0;
    int width = 1024;
    int height = 768;
    bool maximized = false;
};

// タブのセット (仮想デスクトップのように切り替えられるタブの組)。
struct SessionTabSet {
    std::wstring name;
    int activeTabIndex = 0;
    std::vector<SessionTabState> tabs;
};

struct SessionState {
    SessionWindowState window;
    int favoritesWidth = 0;  // エイリアス欄の幅 (px)。0 なら既定
    int keyPanelWidth = 0;   // キー一覧の幅 (px)。0 なら既定
    int previewHeight = 0;   // プレビュー欄の高さ (px)。0 なら既定
    int activeSet = 0;
    std::vector<SessionTabSet> sets;  // 旧形式 (タブの組が 1 つだけ) のファイルも 1 セットとして読み込む
};

// 最初に起動したインスタンスだけがセッションの所有者 (復元・保存を行う) になる。
// 複数起動した場合に後発が先行のタブ状態を読み込んだり上書きしたりするのを防ぐ。
// 起動直後に一度だけ呼ぶ。自分が所有者なら true。
bool AcquirePrimaryInstance();
bool IsPrimaryInstance();

// セッションファイルのフルパスを返す (exe と同じフォルダ\session.json)。
std::wstring GetSessionFilePath();

// セッションを読み込む。ファイルが無い/壊れている場合は tabs が空の SessionState を返す。
SessionState LoadSession();

// セッションをアトミックに保存する (一時ファイルに書いてからリネーム)。
void SaveSession(const SessionState& state);
