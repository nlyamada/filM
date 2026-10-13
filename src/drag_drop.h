// ドラッグ&ドロップ・クリップボード (ファイルの受け渡し)
// - ドラッグ開始 (ファイル操作部から外部へ): OLE (IDropSource/IDataObject, DoDragDrop) を使用。
// - ドロップ受け入れ (Explorer 等から自分へ): 古典的な DragAcceptFiles/WM_DROPFILES を使用。
//   (IDropTarget/RegisterDragDrop によるカスタム実装は、この環境で COMCTL32.dll 内での
//   ヒープ破壊とみられる再現性のあるクラッシュを引き起こしたため採用しない。
//   WM_DROPFILES は枯れた仕組みで、OLEベースのドラッグ元 (Explorer含む) からの
//   ドロップも問題なく受け取れる。)
// - クリップボード: Explorer と同じ形式 (CF_HDROP + "Preferred DropEffect") で
//   コピー/切り取り/貼り付けを相互運用する。IDataObject は自作せず SHCreateDataObject を使う。
//
// 呼び出し元スレッドは OleInitialize 済みであること (CoInitializeEx では不可。
// DoDragDrop / OleSetClipboard は OLE に依存するため)。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

// filePaths (同一フォルダ内の項目) を CF_HDROP としてドラッグ開始する (同期呼び出し、ドロップされるまで戻らない)。
// 戻り値は実際の操作結果 (DROPEFFECT_COPY / DROPEFFECT_MOVE / DROPEFFECT_NONE)。
// DROPEFFECT_MOVE が返った場合、コピー先へのデータ転送はドロップ先が行うため、
// 呼び出し元 (ドラッグ元) が元ファイルの削除を行う必要がある (OLE DnD の仕様)。
DWORD BeginFileDrag(HWND hwndSource, const std::vector<std::wstring>& filePaths);
DWORD BeginFileDrag(HWND hwndSource, const std::wstring& filePath);

// filePaths をクリップボードに置く。cut が true なら「切り取り」(貼り付け側が移動として扱う)。
bool SetFilesToClipboard(const std::vector<std::wstring>& filePaths, bool cut);

// クリップボード上のファイル一覧を取得する。ファイルが無ければ false。
// isCut には切り取りかどうか (Preferred DropEffect が MOVE) が入る。
bool GetFilesFromClipboard(std::vector<std::wstring>& filePaths, bool& isCut);

// 切り取りの貼り付け完了後などにクリップボードを空にする。
void ClearFileClipboard();

// アプリ終了時に呼ぶ。クリップボードのデータをアプリ終了後も使えるように実体化する。
void FlushFileClipboard();
