// キー設定ファイル (springm.key 相当) とコマンド一覧
//
// 元の springm.key は「コマンドID, 仮想キーコード, ラベル, Ctrl, Alt, Shift, 予備」の
// カンマ区切り形式だったが、数値のコマンドIDはSpringM本体固有の内部コマンド体系であり
// 本アプリのコマンド構成とは対応しないため、コマンド名は文字列 ("NewTab" 等) で持つ。
// 仮想キーコードも読みやすいキー名 ("F2", "Delete" 等) で保存する。
//
// 保存先: exe と同じフォルダ\keymap.json  ({"version": 2, "bindings": [...]})
// 初回起動時にファイルが無ければ、組み込みのデフォルト割り当てを書き出す
// (springm.key と同じ「ユーザーが直接編集できる設定ファイル」という役割)。
// 旧形式 (配列だけのファイル) は keymap.v1.json に退避して、新しい既定値で作り直す。

#pragma once

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

struct KeyBinding {
    std::wstring command;  // 例: "NewTab", "Delete", "NavigateInto"
    std::wstring key;      // 表示用キー名。例: "T", "F2", "Delete", "Enter"
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

// キー設定ファイルのフルパスを返す。
std::wstring GetKeyConfigFilePath();

// 組み込みのデフォルトキー割り当て一覧を返す。
std::vector<KeyBinding> GetDefaultKeyBindings();

// キー設定を保存する (keymap.json を書き換える)。
void SaveKeyConfig(const std::vector<KeyBinding>& bindings);

// 割り当て可能なコマンドの一覧 (コマンド名, 表示名)。
std::vector<std::pair<std::wstring, std::wstring>> GetAllCommands();

// キー設定を読み込む。ファイルが無ければデフォルトを書き出してから返す。
// ファイルはあるが壊れている場合は (上書きはせず) デフォルトを返す。
std::vector<KeyBinding> LoadKeyConfig();

// 表示用キー名を仮想キーコードに変換する。認識できなければ 0。
WORD KeyNameToVirtualKey(const std::wstring& name);

// 仮想キーコードを保存用のキー名に変換する。対応していないキー (修飾キー等) は空文字列。
std::wstring VirtualKeyToKeyName(WORD vk);

// "Ctrl+Shift+T" のような表示用の文字列。
std::wstring FormatKeyCombo(const KeyBinding& binding);

// コマンド名に対する画面表示用の説明 (右側のキー一覧に出す)。未登録なら空文字列。
std::wstring GetCommandLabel(const std::wstring& command);

// 右側のキー一覧に並べるキー (キー名, 表示名)。アルファベット順 → Del, BS, ",", ".", Spc, Esc, Ent。
struct PanelKey {
    const wchar_t* key;      // KeyBinding::key と同じ表記
    const wchar_t* display;  // 一覧での表示名
};
const std::vector<PanelKey>& GetPanelKeys();

// 修飾キー無し (shift が false) / Shift のみ (shift が true) で key に割り当てられたコマンド名。
// 無ければ空文字列。
std::wstring FindCommandForKey(const std::vector<KeyBinding>& bindings, const std::wstring& key, bool shift);
