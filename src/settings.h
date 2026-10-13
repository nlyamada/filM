// アプリ全体の設定 (settings.json): 表示言語・テーマ・エディタ。
// 保存先は他の設定ファイルと同じ (exe と同じフォルダ)。

#pragma once

#include <string>

#include "i18n.h"

enum class ThemeMode { System = 0, Light = 1, Dark = 2 };

struct AppSettings {
    Language language = Language::Japanese;
    ThemeMode theme = ThemeMode::System;
    // テキストエディタ。空 = Windows の関連付け (無ければメモ帳)、"notepad" = メモ帳、それ以外は exe のパス。
    std::wstring editor;
    // Markdown のプレビューで、インターネット上の画像も表示するか (既定は表示しない。表示すると画像の取得で外部へ接続する)。
    bool markdownRemoteImages = false;
};

AppSettings& GetSettings();

// 読み込む。ファイルが無ければ OS の言語から既定値を決める。起動直後に一度呼ぶ。
void LoadSettings();

// 保存する (変更のたびに呼ぶ)。
void SaveSettings();
