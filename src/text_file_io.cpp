#include "text_file_io.h"

#include <windows.h>

#include <vector>

bool ReadUtf16File(const std::wstring& path, std::wstring& out) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<BYTE> buffer(static_cast<size_t>(size.QuadPart));
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
    CloseHandle(hFile);
    if (!ok || bytesRead != buffer.size()) {
        return false;
    }

    size_t offset = 0;
    if (buffer.size() >= 2 && buffer[0] == 0xFF && buffer[1] == 0xFE) {
        offset = 2;  // BOM をスキップ
    }
    size_t charCount = (buffer.size() - offset) / sizeof(wchar_t);
    out.assign(reinterpret_cast<const wchar_t*>(buffer.data() + offset), charCount);
    return true;
}

bool WriteUtf16FileAtomic(const std::wstring& path, const std::wstring& text) {
    std::wstring tempPath = path + L".tmp";
    if (!WriteUtf16File(tempPath, text)) {
        return false;
    }
    return MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool WriteUtf16File(const std::wstring& path, const std::wstring& text) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    const WORD bom = 0xFEFF;
    DWORD written = 0;
    bool ok = WriteFile(hFile, &bom, sizeof(bom), &written, nullptr) != 0;
    if (ok && !text.empty()) {
        ok = WriteFile(hFile, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)),
                        &written, nullptr) != 0;
    }
    CloseHandle(hFile);
    return ok;
}
