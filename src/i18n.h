// 日英切り替え
//
// ソース中の表示文字列は日本語を「キー」として Tr(L"日本語") で包む。
// 英語が選ばれているときは i18n.cpp の対訳表から英語を返し、表に無い文字列は日本語のまま返す。
// 文章を途中で切って連結しないこと (語順が変わるため)。数値や名前は Fmt の {0} {1} で差し込む。

#pragma once

#include <initializer_list>
#include <string>

enum class Language { Japanese = 0, English = 1 };

Language GetLanguage();
void SetLanguage(Language lang);

// OS の表示言語から既定の言語を決める (日本語以外は英語)。
Language DetectSystemLanguage();

// 日本語の文字列に対応する、現在の言語の文字列。返すポインタはプロセス終了まで有効。
const wchar_t* Tr(const wchar_t* japanese);

// Tr した書式文字列の {0} {1} ... を args で置き換える。
std::wstring Fmt(const wchar_t* japanese, std::initializer_list<std::wstring> args);

inline std::wstring Num(long long n) {
    return std::to_wstring(n);
}
