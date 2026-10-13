// Markdown 表示の部品 (WebView2 に HTML を描く子ウィンドウ)。プレビュー欄に埋め込んで使う。
//
// 表示には viewer\ フォルダ (marked / KaTeX / mermaid) と WebView2Loader.dll が exe の隣に要る。
// WebView2 ランタイムは Windows 11 に標準で入っている。本文は ```mermaid の図と $...$ / $$...$$ の数式に対応。
// 安全のため、文書内のスクリプトは動かさず、外部サイトの画像も読み込まない。

#pragma once

#include <windows.h>

#include <string>

constexpr UINT kMsgMarkdownViewKey = WM_APP + 0x32;     // 親へ: wParam = 仮想キー (Esc / Tab)
constexpr UINT kMsgMarkdownViewFailed = WM_APP + 0x33;  // 親へ: WebView2 を使えなかった (理由は MarkdownViewLastError)
constexpr UINT kMsgMarkdownViewInfo = WM_APP + 0x34;    // 親へ: 文書内のリンクで別のファイルに移った

bool IsMarkdownFile(const std::wstring& path);

HWND MarkdownViewCreate(HWND parent);

// path の Markdown を表示する。WebView2 の準備に時間がかかるときは、準備が済み次第表示する。
// すぐに使えないと分かったら false を返して error に理由を入れる。
// 準備の途中で失敗したときは、親に kMsgMarkdownViewFailed が届く。
bool MarkdownViewShow(HWND host, const std::wstring& path, bool dark, bool remoteImages, std::wstring& error);

// いま表示している Markdown のパス (文書内のリンクで移った後の値)。
std::wstring MarkdownViewCurrentPath();
std::wstring MarkdownViewLastError();

// WebView にフォーカスを移す (矢印キー・PageUp/PageDown でスクロールできるようになる)。
void MarkdownViewFocus(HWND host);

// マウスホイールの分だけ縦にスクロールする (notches > 0 で上へ)。
void MarkdownViewScroll(int notches);

// 表示を捨てる (別の種類のファイルに切り替えるとき)。
void MarkdownViewClear();
