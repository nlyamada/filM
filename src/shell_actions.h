// シェル連携の操作 (関連付けで開く・プロパティ・ターミナル起動・編集・ショートカット・zip)

#pragma once

#include <windows.h>

#include <string>
#include <vector>

// path を関連付けられたアプリで開く (Enter)。
bool OpenWithAssociation(HWND hwnd, const std::wstring& path, const std::wstring& workDir);

// 「プログラムから開く」ダイアログを出して選んだアプリで開く (Shift+Enter)。
bool OpenWithChooser(HWND hwnd, const std::wstring& path);

// paths (同じフォルダ内の項目) のプロパティを表示する。
bool ShowProperties(HWND hwnd, const std::vector<std::wstring>& paths);

// dir をカレントにして PowerShell / コマンドプロンプトを開く。
bool LaunchTerminal(const std::wstring& dir, bool powershell);

enum class EditResult { Started, Binary, Failed };

// path をテキストエディタで開く。バイナリは開かず Binary を返す。
// エディタは環境変数 FILM_EDITOR、無ければ関連付けの "edit"、それも無ければメモ帳。
// forceText が true ならバイナリ判定をしない (設定ファイルを開く場合)。
EditResult EditFile(HWND hwnd, const std::wstring& path, const std::wstring& workDir, bool forceText = false);

// 先頭 8KB に NUL バイトを含むファイル (UTF-16 の BOM 付きは除く) をバイナリとみなす。
bool IsBinaryFile(const std::wstring& path);

// 画像ファイルかどうか (拡張子で判定)。
bool IsImageFile(const std::wstring& path);

// target へのショートカット linkPath (.lnk) を作る。
bool CreateShortcutFile(const std::wstring& target, const std::wstring& linkPath);

// desired が存在すれば "名前 (2).拡張子" のように番号を付けた存在しないパスを返す。
// isDirectory が true なら拡張子を分けず末尾に番号を付ける。
std::wstring MakeUniquePath(const std::wstring& desired, bool isDirectory);

// ---- zip (Windows 標準の tar.exe を使う。UI を止めないよう別スレッドで実行) ----

struct ArchiveRequest {
    bool extract = false;
    std::wstring workDir;                 // 圧縮: 元項目があるフォルダ
    std::vector<std::wstring> names;      // 圧縮: workDir 内の項目名
    std::wstring zipPath;                 // 圧縮: 作成する zip / 展開: 展開する zip
    std::wstring destDir;                 // 展開: 展開先フォルダ (作成済みであること)
    bool intoFolder = false;              // 展開: destDir は最終的な展開先 (zip 名のフォルダ。移し替えない)
};

struct ArchiveResult {
    bool ok = false;
    bool extract = false;
    bool intoFolder = false;
    std::wstring output;   // 圧縮: zip のパス / 展開: 展開先フォルダ
    std::wstring message;  // 失敗時の tar のメッセージ
};

// 別スレッドで実行し、完了したら notify に msg を PostMessage する。
// lParam は new した ArchiveResult* で、受け取り側が delete する。
void StartArchiveJob(HWND notify, UINT msg, const ArchiveRequest& request);
