// 「元に戻す」(Z) の操作履歴
//
// ファイル操作の API には Explorer のような取り消しの仕組みが無いため、このアプリで行った操作を
// メモリ上に記録して (最大 50 件、終了すると消える)、逆の操作を実行する。
//   作成系 (コピー・複製・zip・展開・フォルダ作成・ショートカット)  → 作ったものをごみ箱へ
//   移動・リネーム                                                  → 元の場所・名前へ戻す
//   ごみ箱への削除                                                  → ごみ箱から復元
//   属性変更                                                        → 元の属性へ戻す
// 完全な削除 (Shift+D) は取り消せない。

#pragma once

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "file_ops.h"

void PushUndoCreated(const std::wstring& description, const std::vector<std::wstring>& createdPaths);
void PushUndoMoved(const std::wstring& description, const std::vector<OpPair>& movedPairs);
// trashedPairs: DeleteItems が返した (削除前のパス, ごみ箱側のパス)。ごみ箱側のパスが空のもの (完全に削除された) は記録しない。
void PushUndoTrashed(const std::wstring& description, const std::vector<OpPair>& trashedPairs);
void PushUndoAttributes(const std::wstring& description,
                        const std::vector<std::pair<std::wstring, DWORD>>& oldAttributes);

bool CanUndo();

// 直前の操作を取り消す。履歴が無ければ false。結果の説明を message に入れる。
// 取り消しに失敗した場合も、その記録は履歴から外す (同じ失敗を繰り返さないため)。
bool UndoLast(HWND hwndOwner, std::wstring& message);

