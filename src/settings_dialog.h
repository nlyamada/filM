// 設定ダイアログ: 表示言語・テーマ・テキストエディタ・キー割り当て。

#pragma once

#include <windows.h>

#include <vector>

#include "keyconfig.h"

struct SettingsDialogResult {
    bool accepted = false;      // OK で閉じた
    bool keysChanged = false;   // bindings が変わった (呼び出し側で SaveKeyConfig して読み直す)
};

// モーダルで表示する。AppSettings (GetSettings()) は OK のとき直接更新される (保存は呼び出し側)。
// bindings は編集後の内容が入る。
SettingsDialogResult ShowSettingsDialog(HWND owner, std::vector<KeyBinding>& bindings);
