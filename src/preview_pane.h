// プレビュー欄 (ビューワー): ファイル一覧の下に出す、画像・Markdown・テキストの表示。
//
// V で表示/非表示を切り替え、表示中は一覧のカーソルに合わせて内容が変わる (表示するだけで、編集はできない)。
// Tab (または内容をクリック) で内容にフォーカスを移して、矢印キー・PageUp/PageDown でスクロールできる。
// 内容にフォーカスがあるとき、Tab は一覧へ戻り、Esc はビューワーを閉じて一覧へ戻る。

#pragma once

#include <windows.h>

#include <string>

// 親 (メインウィンドウ) へ届くメッセージ。
constexpr UINT kMsgPreviewReturn = WM_APP + 0x40;  // 一覧へフォーカスを戻してほしい (Tab)
constexpr UINT kMsgPreviewStep = WM_APP + 0x41;    // wParam = 前後の項目へ動かす数 (負で前へ。画像のとき ← →)
constexpr UINT kMsgPreviewClose = WM_APP + 0x42;   // 閉じてほしい (Esc / × ボタン)。フォーカスは一覧へ

HWND PreviewPaneCreate(HWND parent);

// path の内容を表示する (空やフォルダなら案内を出す)。
void PreviewPaneShowFile(const std::wstring& path);

// 内容へフォーカスを移す。移せない (何も表示していない) ときは false。
bool PreviewPaneFocusContent();

// マウスホイールを内容へ渡す (一覧にフォーカスがあってもマウスが欄の上ならスクロールできるように)。
void PreviewPaneWheel(int delta);

void PreviewPaneApplyTheme();
void PreviewPaneApplyLanguage();
