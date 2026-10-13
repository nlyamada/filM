#include "json.h"

#include <cwctype>

JsonValue JsonValue::MakeObject() {
    JsonValue v;
    v.type = JsonType::Object;
    return v;
}

JsonValue JsonValue::MakeArray() {
    JsonValue v;
    v.type = JsonType::Array;
    return v;
}

JsonValue JsonValue::MakeString(const std::wstring& s) {
    JsonValue v;
    v.type = JsonType::String;
    v.stringValue = s;
    return v;
}

JsonValue JsonValue::MakeNumber(double n) {
    JsonValue v;
    v.type = JsonType::Number;
    v.numberValue = n;
    return v;
}

JsonValue JsonValue::MakeBool(bool b) {
    JsonValue v;
    v.type = JsonType::Bool;
    v.boolValue = b;
    return v;
}

void JsonValue::Set(const std::wstring& key, JsonValue value) {
    for (auto& kv : objectValue) {
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    }
    objectValue.emplace_back(key, std::move(value));
}

const JsonValue* JsonValue::Get(const std::wstring& key) const {
    for (const auto& kv : objectValue) {
        if (kv.first == key) {
            return &kv.second;
        }
    }
    return nullptr;
}

std::wstring JsonValue::AsString(const std::wstring& defaultValue) const {
    return type == JsonType::String ? stringValue : defaultValue;
}

double JsonValue::AsNumber(double defaultValue) const {
    return type == JsonType::Number ? numberValue : defaultValue;
}

bool JsonValue::AsBool(bool defaultValue) const {
    return type == JsonType::Bool ? boolValue : defaultValue;
}

namespace {

// --- パーサー ---------------------------------------------------------

struct Parser {
    const std::wstring& text;
    size_t pos = 0;
    bool ok = true;
    int depth = 0;  // 入れ子の深さ。深すぎる入力 (壊れた・悪意のあるファイル) でスタックを使い切らないよう制限する

    explicit Parser(const std::wstring& t) : text(t) {}

    wchar_t Peek() const { return pos < text.size() ? text[pos] : L'\0'; }
    wchar_t Next() { return pos < text.size() ? text[pos++] : L'\0'; }

    void SkipWhitespace() {
        while (pos < text.size() && iswspace(text[pos])) {
            ++pos;
        }
    }

    bool Consume(wchar_t c) {
        SkipWhitespace();
        if (Peek() != c) {
            ok = false;
            return false;
        }
        ++pos;
        return true;
    }

    bool ConsumeLiteral(const wchar_t* literal) {
        size_t len = wcslen(literal);
        if (text.compare(pos, len, literal) != 0) {
            return false;
        }
        pos += len;
        return true;
    }

    bool ParseValue(JsonValue& out) {
        SkipWhitespace();
        wchar_t c = Peek();
        if (c == L'{' || c == L'[') {
            constexpr int kMaxDepth = 64;
            if (depth >= kMaxDepth) {
                ok = false;
                return false;
            }
            ++depth;
            bool result = c == L'{' ? ParseObject(out) : ParseArray(out);
            --depth;
            return result;
        }
        if (c == L'"') return ParseString(out);
        if (c == L't' || c == L'f') return ParseBool(out);
        if (c == L'n') return ParseNull(out);
        return ParseNumber(out);
    }

    bool ParseObject(JsonValue& out) {
        out = JsonValue::MakeObject();
        if (!Consume(L'{')) return false;
        SkipWhitespace();
        if (Peek() == L'}') {
            ++pos;
            return true;
        }
        while (true) {
            SkipWhitespace();
            JsonValue keyValue;
            if (!ParseString(keyValue)) return false;
            if (!Consume(L':')) return false;
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.objectValue.emplace_back(keyValue.stringValue, std::move(value));

            SkipWhitespace();
            if (Peek() == L',') {
                ++pos;
                continue;
            }
            break;
        }
        return Consume(L'}');
    }

    bool ParseArray(JsonValue& out) {
        out = JsonValue::MakeArray();
        if (!Consume(L'[')) return false;
        SkipWhitespace();
        if (Peek() == L']') {
            ++pos;
            return true;
        }
        while (true) {
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.arrayValue.push_back(std::move(value));

            SkipWhitespace();
            if (Peek() == L',') {
                ++pos;
                continue;
            }
            break;
        }
        return Consume(L']');
    }

    bool ParseString(JsonValue& out) {
        SkipWhitespace();
        if (!Consume(L'"')) return false;
        std::wstring result;
        while (true) {
            if (pos >= text.size()) {
                ok = false;
                return false;
            }
            wchar_t c = Next();
            if (c == L'"') break;
            if (c == L'\\') {
                wchar_t esc = Next();
                switch (esc) {
                    case L'"': result += L'"'; break;
                    case L'\\': result += L'\\'; break;
                    case L'/': result += L'/'; break;
                    case L'b': result += L'\b'; break;
                    case L'f': result += L'\f'; break;
                    case L'n': result += L'\n'; break;
                    case L'r': result += L'\r'; break;
                    case L't': result += L'\t'; break;
                    case L'u': {
                        if (pos + 4 > text.size()) {
                            ok = false;
                            return false;
                        }
                        std::wstring hex = text.substr(pos, 4);
                        pos += 4;
                        result += static_cast<wchar_t>(wcstol(hex.c_str(), nullptr, 16));
                        break;
                    }
                    default:
                        result += esc;
                        break;
                }
            } else {
                result += c;
            }
        }
        out = JsonValue::MakeString(result);
        return true;
    }

    bool ParseNumber(JsonValue& out) {
        SkipWhitespace();
        size_t start = pos;
        if (Peek() == L'-') ++pos;
        while (pos < text.size() && (iswdigit(text[pos]) || text[pos] == L'.' ||
                                      text[pos] == L'e' || text[pos] == L'E' ||
                                      text[pos] == L'+' || text[pos] == L'-')) {
            ++pos;
        }
        if (pos == start) {
            ok = false;
            return false;
        }
        out = JsonValue::MakeNumber(wcstod(text.c_str() + start, nullptr));
        return true;
    }

    bool ParseBool(JsonValue& out) {
        SkipWhitespace();
        if (ConsumeLiteral(L"true")) {
            out = JsonValue::MakeBool(true);
            return true;
        }
        if (ConsumeLiteral(L"false")) {
            out = JsonValue::MakeBool(false);
            return true;
        }
        ok = false;
        return false;
    }

    bool ParseNull(JsonValue& out) {
        SkipWhitespace();
        if (ConsumeLiteral(L"null")) {
            out = JsonValue{};
            return true;
        }
        ok = false;
        return false;
    }
};

// --- シリアライザ -------------------------------------------------------

void EscapeStringInto(const std::wstring& s, std::wstring& out) {
    out += L'"';
    for (wchar_t c : s) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += L'"';
}

void DumpInto(const JsonValue& value, int indent, std::wstring& out) {
    std::wstring pad(static_cast<size_t>(indent) * 2, L' ');
    std::wstring padInner(static_cast<size_t>(indent + 1) * 2, L' ');

    switch (value.type) {
        case JsonType::Null:
            out += L"null";
            break;
        case JsonType::Bool:
            out += value.boolValue ? L"true" : L"false";
            break;
        case JsonType::Number: {
            wchar_t buf[64];
            double n = value.numberValue;
            if (n == static_cast<long long>(n)) {
                swprintf(buf, 64, L"%lld", static_cast<long long>(n));
            } else {
                swprintf(buf, 64, L"%g", n);
            }
            out += buf;
            break;
        }
        case JsonType::String:
            EscapeStringInto(value.stringValue, out);
            break;
        case JsonType::Array: {
            if (value.arrayValue.empty()) {
                out += L"[]";
                break;
            }
            out += L"[\n";
            for (size_t i = 0; i < value.arrayValue.size(); ++i) {
                out += padInner;
                DumpInto(value.arrayValue[i], indent + 1, out);
                if (i + 1 < value.arrayValue.size()) out += L',';
                out += L'\n';
            }
            out += pad;
            out += L']';
            break;
        }
        case JsonType::Object: {
            if (value.objectValue.empty()) {
                out += L"{}";
                break;
            }
            out += L"{\n";
            for (size_t i = 0; i < value.objectValue.size(); ++i) {
                out += padInner;
                EscapeStringInto(value.objectValue[i].first, out);
                out += L": ";
                DumpInto(value.objectValue[i].second, indent + 1, out);
                if (i + 1 < value.objectValue.size()) out += L',';
                out += L'\n';
            }
            out += pad;
            out += L'}';
            break;
        }
    }
}

}  // namespace

bool ParseJson(const std::wstring& text, JsonValue& out) {
    Parser parser(text);
    if (!parser.ParseValue(out)) {
        return false;
    }
    return parser.ok;
}

std::wstring DumpJson(const JsonValue& value) {
    std::wstring out;
    DumpInto(value, 0, out);
    return out;
}
