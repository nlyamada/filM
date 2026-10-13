#include "text_codec.h"

#include <windows.h>

#include <algorithm>
#include <string>

namespace {

bool ReadAll(const std::wstring& path, size_t maxBytes, std::string& bytes, bool& tooLarge) {
    tooLarge = false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        return false;
    }
    if (size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > maxBytes) {
        CloseHandle(h);
        tooLarge = true;
        return true;
    }
    bytes.assign(static_cast<size_t>(size.QuadPart), '\0');
    size_t total = 0;
    while (total < bytes.size()) {
        DWORD got = 0;
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - total, 1u << 24));
        if (!ReadFile(h, bytes.data() + total, chunk, &got, nullptr) || got == 0) break;
        total += got;
    }
    CloseHandle(h);
    bytes.resize(total);
    return true;
}

std::wstring DecodeUtf16(const char* data, size_t len, bool bigEndian) {
    std::wstring out(len / 2, L'\0');
    for (size_t i = 0; i < out.size(); ++i) {
        unsigned char a = static_cast<unsigned char>(data[i * 2]), b = static_cast<unsigned char>(data[i * 2 + 1]);
        out[i] = static_cast<wchar_t>(bigEndian ? (a << 8 | b) : (b << 8 | a));
    }
    return out;
}

// bytes を指定のコードページで文字列にする。strict なら不正な並びで失敗する。
bool DecodeCodePage(const char* data, size_t len, UINT cp, bool strict, std::wstring& out) {
    out.clear();
    if (len == 0) return true;
    int n = MultiByteToWideChar(cp, strict ? MB_ERR_INVALID_CHARS : 0, data, static_cast<int>(len), nullptr, 0);
    if (n <= 0) return false;
    out.assign(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(cp, strict ? MB_ERR_INVALID_CHARS : 0, data, static_cast<int>(len), out.data(), n);
    return true;
}

// 改行 (\r\n と単独の \r) を \n にそろえる。元が CRLF かどうかを返す。
bool NormalizeNewlines(std::wstring& text) {
    bool sawCrlf = false, sawLf = false;
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                sawCrlf = true;
                ++i;
            }
            out += L'\n';
        } else {
            if (c == L'\n') sawLf = true;
            out += c;
        }
    }
    text.swap(out);
    return sawCrlf || !sawLf;
}

}  // namespace

TextLoadResult LoadTextDocument(const std::wstring& path, TextDocument& doc, size_t maxBytes) {
    std::string bytes;
    bool tooLarge = false;
    if (!ReadAll(path, maxBytes, bytes, tooLarge)) return TextLoadResult::CannotOpen;
    if (tooLarge) return TextLoadResult::TooLarge;

    const char* data = bytes.data();
    size_t len = bytes.size();
    auto byteAt = [&](size_t i) { return static_cast<unsigned char>(data[i]); };
    std::wstring text;
    if (len >= 2 && byteAt(0) == 0xFF && byteAt(1) == 0xFE) {
        doc.encoding = TextEncoding::Utf16LE;
        text = DecodeUtf16(data + 2, len - 2, false);
    } else if (len >= 2 && byteAt(0) == 0xFE && byteAt(1) == 0xFF) {
        doc.encoding = TextEncoding::Utf16BE;
        text = DecodeUtf16(data + 2, len - 2, true);
    } else {
        size_t head = std::min<size_t>(len, 8192);
        if (std::find(data, data + head, '\0') != data + head) return TextLoadResult::Binary;
        if (len >= 3 && byteAt(0) == 0xEF && byteAt(1) == 0xBB && byteAt(2) == 0xBF) {
            doc.encoding = TextEncoding::Utf8Bom;
            DecodeCodePage(data + 3, len - 3, CP_UTF8, false, text);
        } else if (DecodeCodePage(data, len, CP_UTF8, true, text)) {
            doc.encoding = TextEncoding::Utf8;
        } else {
            doc.encoding = TextEncoding::Ansi;
            DecodeCodePage(data, len, CP_ACP, false, text);
        }
    }
    doc.crlf = NormalizeNewlines(text);
    doc.text = std::move(text);
    return TextLoadResult::Ok;
}

const wchar_t* TextEncodingName(TextEncoding enc) {
    switch (enc) {
        case TextEncoding::Utf8Bom: return L"UTF-8 (BOM)";
        case TextEncoding::Utf16LE: return L"UTF-16 LE";
        case TextEncoding::Utf16BE: return L"UTF-16 BE";
        case TextEncoding::Ansi: return L"ANSI";
        default: return L"UTF-8";
    }
}
