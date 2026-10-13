#include "i18n.h"

#include <windows.h>

#include <unordered_map>

namespace {

Language g_language = Language::Japanese;

struct Entry {
    const wchar_t* ja;
    const wchar_t* en;
};

// 日本語 → 英語の対訳表。
const Entry kTable[] = {
#include "i18n_table.inc"
};

}  // namespace

Language GetLanguage() {
    return g_language;
}

void SetLanguage(Language lang) {
    g_language = lang;
}

Language DetectSystemLanguage() {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE ? Language::Japanese : Language::English;
}

const wchar_t* Tr(const wchar_t* japanese) {
    if (g_language == Language::Japanese || !japanese) return japanese;
    static const std::unordered_map<std::wstring, const wchar_t*> map = [] {
        std::unordered_map<std::wstring, const wchar_t*> m;
        for (const Entry& e : kTable) m.emplace(e.ja, e.en);
        return m;
    }();
    auto it = map.find(japanese);
    return it == map.end() ? japanese : it->second;
}

std::wstring Fmt(const wchar_t* japanese, std::initializer_list<std::wstring> args) {
    std::wstring s = Tr(japanese);
    size_t i = 0;
    for (const std::wstring& a : args) {
        std::wstring key = L"{" + std::to_wstring(i++) + L"}";
        for (size_t pos = s.find(key); pos != std::wstring::npos; pos = s.find(key, pos + a.size())) {
            s.replace(pos, key.size(), a);
        }
    }
    return s;
}
