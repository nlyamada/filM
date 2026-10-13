// ファイル操作 (コピー・移動・削除・リネーム・複製・ごみ箱からの復元)
// 設計メモの方針に従い IFileOperation (COM) を使用する。削除はごみ箱経由をデフォルトとする。
//
// 複数項目は1つの IFileOperation にまとめて渡す (進捗・上書き確認のダイアログが1回で済む)。
// 「元に戻す」(undo.cpp) のため、実際に作成された新しいパス (上書き確認で「両方残す」を選んだ
// 場合の別名を含む) を IFileOperationProgressSink で受け取って返す。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct OpPair {
    std::wstring from;  // 操作前のパス
    std::wstring to;    // 操作後のパス (取得できなければ空)
};

// sources を destDir 配下へコピー/移動する。全項目が成功したら true。
// done には成功した項目 (元のパス, 新しいパス) が入る (中断された場合は完了済みの分だけ)。
bool CopyItems(HWND hwndOwner, const std::vector<std::wstring>& sources, const std::wstring& destDir,
               std::vector<OpPair>* done);
bool MoveItems(HWND hwndOwner, const std::vector<std::wstring>& sources, const std::wstring& destDir,
               std::vector<OpPair>* done);

// sources を削除する。permanent が false ならごみ箱へ (既定)。true なら完全に削除する
// (確認は呼び出し側で行う)。
bool DeleteItems(HWND hwndOwner, const std::vector<std::wstring>& sources, bool permanent,
                 std::vector<OpPair>* done);

// fullPath を newName にリネームする。newPath に新しいパスが入る。
bool RenameItem(HWND hwndOwner, const std::wstring& fullPath, const std::wstring& newName,
                std::wstring* newPath);

// fullPath を同じフォルダ内に newName という名前で複製する (複製)。
bool DuplicateItem(HWND hwndOwner, const std::wstring& fullPath, const std::wstring& newName,
                   std::wstring* newPath);

// ごみ箱の中から、originalPath (削除前のフルパス) だった項目を探して元の場所へ復元する。
// binPath は DeleteItems が返した「ごみ箱側のパス」で、分かっていればそれで項目を特定する
// (空なら削除前のパスと削除元フォルダから探し、複数あれば最も新しく削除されたもの)。
bool RestoreFromRecycleBin(HWND hwndOwner, const std::wstring& originalPath, const std::wstring& binPath);

// フォルダ選択ダイアログ (IFileOpenDialog, FOS_PICKFOLDERS) を表示する。
// キャンセル時は空文字列を返す。
std::wstring PickFolderDialog(HWND hwndOwner);
