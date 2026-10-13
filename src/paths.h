// 設定ファイル (session.json / keymap.json / favorites.json 等) の置き場所と、パス文字列の正規化。
//
// 保存先: exe と同じフォルダ

#pragma once

#include <string>

// 設定ディレクトリのフルパス。既定は exe と同じフォルダ (書き込めない場所なら %APPDATA%\filM)。
// 旧版の %APPDATA%\SpringMClone に設定があれば、GetConfigFilePath が初回に引き継ぐ。取得できなければ空文字列。
// 環境変数 FILM_CONFIG_DIR が設定されていればそのディレクトリを使う。
std::wstring GetConfigDirectory();

// 設定ディレクトリ配下のファイルのフルパスを返す。ディレクトリが無ければ作成する。
// 取得できなければ空文字列。
std::wstring GetConfigFilePath(const wchar_t* fileName);

// base を基準に path を絶対パスへ解決し、"." ".." を取り除く。
// ドライブ付き絶対パス・UNC はそのまま正規化する。先頭 "~" はユーザープロファイル。
std::wstring ResolvePath(const std::wstring& base, const std::wstring& path);

// path が属するドライブ (または UNC 共有) のルート ("C:\" / "\\srv\share\") を返す。
std::wstring GetRootOfPath(const std::wstring& path);
