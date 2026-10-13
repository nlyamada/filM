// 簡易入力ダイアログ (リソースファイル不要、メモリ上の DLGTEMPLATE で構築)。
// エイリアスの登録・フォルダ指定 (TAB でフォルダ名・エイリアス名を補完)・属性変更などに使う。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

enum class FieldKind { Text, Check };

struct DialogField {
    std::wstring label;
    std::wstring value;       // 入力の初期値。OK で入力結果が入る。Check は "1" / "0"
    int maxLength = 0;        // 0 なら制限なし
    FieldKind kind = FieldKind::Text;
    bool pathCompletion = false;  // true なら TAB でフォルダ名・エイリアス名を補完する
};

struct DialogOptions {
    std::wstring baseDir;  // 補完で相対パスの基準にするフォルダ
    std::wstring hint;     // ボタンの上に出す1行の説明
};

// モーダルで表示する。OK なら true (fields の value が更新される)、キャンセルなら false。
bool ShowFieldsDialog(HWND hwndOwner, const std::wstring& title, std::vector<DialogField>& fields,
                      const DialogOptions& options = {});

// 1行のテキスト入力 (フォルダ名・ファイル名など)。OK なら true。
bool PromptText(HWND hwndOwner, const std::wstring& title, const std::wstring& label, std::wstring& value);

// フォルダを指定させる。TAB でフォルダ名・エイリアス名を補完でき、エイリアス名や相対パスは
// 実際のフォルダへ解決する。存在しないフォルダなら再入力させる。OK で result に絶対パスが入る。
bool PromptFolder(HWND hwndOwner, const std::wstring& title, const std::wstring& label,
                  const std::wstring& baseDir, const std::wstring& initial, std::wstring& result);
