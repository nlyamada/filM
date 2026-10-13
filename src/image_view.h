// 画像表示の部品 (WIC でデコードして描く子ウィンドウ)。プレビュー欄に埋め込んで使う。
//
// 操作: + - / マウスホイール 拡大縮小 / 0 (F) 全体表示 / 1 等倍 / ドラッグで移動 / ダブルクリックで全体 ⇔ 等倍。
// それ以外のキー (← → Esc Tab など) は親ウィンドウへ kMsgImageViewKey で渡す。
// EXIF の向きを反映する。非常に大きな画像は読み込み時に縮小して表示する。

#pragma once

#include <windows.h>

#include <string>

constexpr UINT kMsgImageViewKey = WM_APP + 0x30;   // 親へ: wParam = 仮想キー (部品が処理しないキー)
constexpr UINT kMsgImageViewInfo = WM_APP + 0x31;  // 親へ: 表示倍率が変わった (ImageViewInfo を読み直す)

HWND ImageViewCreate(HWND parent);

// 読み込んで表示する。失敗したら false (部品の中にエラー文が出る)。
bool ImageViewLoad(HWND view, const std::wstring& path);

// 画像を捨てて空にする。
void ImageViewClear(HWND view);

// "1920 x 1080  -  100%" のような説明。画像が無ければ空。
std::wstring ImageViewInfo(HWND view);
