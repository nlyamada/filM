// ダークモードの共通部分。メインウィンドウ・ダイアログ・ビューアーが同じ配色を使う。

#pragma once

#include <windows.h>
#include <commctrl.h>

// 現在ダークで表示しているか (ThemeSetDark で更新)。
bool ThemeIsDark();
void ThemeSetDark(bool dark);

// OS の「アプリのモード」設定がダークか。
bool ThemeSystemIsDark();

// 配色 (ライト時はシステム色)。
COLORREF ThemeWindowBg();   // 一覧・入力欄の背景
COLORREF ThemeText();       // 一覧・入力欄の文字
COLORREF ThemeBarBg();      // 見出しバー・ダイアログの背景
COLORREF ThemeBarText();
COLORREF ThemeBarHot();     // バー上のボタンを押したときの色
COLORREF ThemeSelectBg();   // 選択中の項目の背景 (タブなど)
HBRUSH ThemeWindowBrush();  // ThemeWindowBg の共有ブラシ (解放しない)
HBRUSH ThemeBarBrush();     // ThemeBarBg の共有ブラシ

// メニューなど OS が描くものをダークにするかどうかを OS へ伝える (起動時とテーマ変更時に呼ぶ)。
void ThemeApplyAppMode(bool dark);

// トップレベルウィンドウ (ダイアログ含む) のタイトルバーと、子コントロールのテーマをダーク/ライトに合わせる。
void ThemeApplyToWindow(HWND hwnd);

// MessageBoxW の代わり。ダークモードのときだけ自前の暗いダイアログで表示する (ライトは OS 標準)。
// 戻り値は MessageBoxW と同じ (IDOK / IDYES / IDNO / IDCANCEL)。
int ThemedMessageBox(HWND owner, const wchar_t* text, const wchar_t* caption, UINT flags);

// WM_CTLCOLOR*の処理。ダークのとき true を返し、brush に使うブラシを入れる。
bool ThemeHandleCtlColor(UINT msg, HDC hdc, HBRUSH& brush);

// 一覧 (ListView) の配色・テーマ・列見出しの描画をいまのライト/ダークに合わせる。ダーク/ライト切替のたびに呼ぶ。
void ThemeStyleListView(HWND list);
