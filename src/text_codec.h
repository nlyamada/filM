// テキストファイルの読み込み (文字コードと改行の自動判定)。プレビューと Markdown 表示で使う。
//
// 読み込んだ本文の改行は "\n" に統一する (元が CRLF だったかは crlf に残す)。

#pragma once

#include <string>

enum class TextEncoding { Utf8 = 0, Utf8Bom, Utf16LE, Utf16BE, Ansi };

struct TextDocument {
    std::wstring text;  // 改行は "\n"
    TextEncoding encoding = TextEncoding::Utf8;
    bool crlf = true;   // 元の改行が CRLF か (改行が 1 つも無いときは true)
};

enum class TextLoadResult { Ok, CannotOpen, TooLarge, Binary };

// maxBytes を超えるファイルは TooLarge。NUL バイトを含む (UTF-16 の BOM が無い) ものは Binary。
TextLoadResult LoadTextDocument(const std::wstring& path, TextDocument& doc, size_t maxBytes);

const wchar_t* TextEncodingName(TextEncoding enc);
