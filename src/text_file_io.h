// UTF-16LE (BOM付き) のテキストファイル読み書き。
// 設定ファイル (session.json / keymap.json 等) はすべてこの形式で統一する。

#pragma once

#include <string>

// path を UTF-16LE (BOM付き) として読み込む。成功したら true。
bool ReadUtf16File(const std::wstring& path, std::wstring& out);

// text を UTF-16LE (BOM付き) として path に書き込む (上書き)。成功したら true。
bool WriteUtf16File(const std::wstring& path, const std::wstring& text);

// path.tmp に書いてからリネームするアトミック書き込み。書き込み中に強制終了されても
// 既存のファイルが壊れない。成功したら true。
bool WriteUtf16FileAtomic(const std::wstring& path, const std::wstring& text);
