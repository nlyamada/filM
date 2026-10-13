#include "file_list.h"
#include "i18n.h"

#include <shellapi.h>

#include <algorithm>
#include <cwchar>

namespace {

bool IsDotOrDotDot(const wchar_t* name) {
    return (name[0] == L'.' && name[1] == L'\0') ||
           (name[0] == L'.' && name[1] == L'.' && name[2] == L'\0');
}

// 拡張子 (ドットを除く)。先頭が "." だけのドットファイルは拡張子なし扱い。
const wchar_t* ExtensionOf(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) {
        return L"";
    }
    return name.c_str() + dot + 1;
}

}  // namespace

std::vector<FileEntry> EnumerateDirectory(const std::wstring& directory, SortKey key, bool descending) {
    std::vector<FileEntry> entries;

    std::wstring pattern = directory;
    if (!pattern.empty() && pattern.back() != L'\\') {
        pattern += L'\\';
    }
    pattern += L'*';

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return entries;
    }

    do {
        if (IsDotOrDotDot(findData.cFileName)) {
            continue;
        }

        FileEntry entry;
        entry.name = findData.cFileName;
        entry.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.isCloudPlaceholder =
            (findData.dwFileAttributes & (FILE_ATTRIBUTE_RECALL_ON_OPEN |
                                          FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
                                          FILE_ATTRIBUTE_OFFLINE)) != 0;
        entry.size = (static_cast<ULONGLONG>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
        entry.lastWriteTime = findData.ftLastWriteTime;
        entries.push_back(std::move(entry));
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    std::sort(entries.begin(), entries.end(), [key, descending](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory;  // フォルダを先に (降順でも変えない)
        }
        int primary = 0;
        switch (key) {
            case SortKey::Ext:
                primary = a.isDirectory ? 0 : _wcsicmp(ExtensionOf(a.name), ExtensionOf(b.name));
                break;
            case SortKey::Size:
                primary = a.isDirectory ? 0 : (a.size < b.size ? -1 : (a.size > b.size ? 1 : 0));
                break;
            case SortKey::Date:
                primary = CompareFileTime(&a.lastWriteTime, &b.lastWriteTime);
                break;
            case SortKey::Name:
                break;
        }
        if (primary != 0) {
            return descending ? primary > 0 : primary < 0;
        }
        int byName = _wcsicmp(a.name.c_str(), b.name.c_str());
        if (key == SortKey::Name && descending) {
            return byName > 0;
        }
        return byName < 0;
    });

    return entries;
}

std::wstring FormatFileSize(ULONGLONG bytes, bool isDirectory) {
    if (isDirectory) {
        return L"";
    }

    const wchar_t* unit = L"B";
    double value = static_cast<double>(bytes);

    if (bytes >= 1024ULL * 1024 * 1024) {
        value = bytes / (1024.0 * 1024.0 * 1024.0);
        unit = L"GB";
    } else if (bytes >= 1024ULL * 1024) {
        value = bytes / (1024.0 * 1024.0);
        unit = L"MB";
    } else if (bytes >= 1024ULL) {
        value = bytes / 1024.0;
        unit = L"KB";
    } else {
        wchar_t buf[32];
        swprintf(buf, 32, L"%llu %s", static_cast<unsigned long long>(bytes), unit);
        return buf;
    }

    wchar_t buf[32];
    swprintf(buf, 32, L"%.2f%s", value, unit);
    return buf;
}

std::wstring FormatFileTime(const FILETIME& ft) {
    FILETIME localFt;
    if (!FileTimeToLocalFileTime(&ft, &localFt)) {
        return L"";
    }

    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&localFt, &st)) {
        return L"";
    }

    wchar_t buf[64];
    swprintf(buf, 64, L"%04u/%02u/%02u %02u:%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

std::wstring GetFileTypeName(const std::wstring& fullPath, bool isDirectory) {
    SHFILEINFOW info{};
    DWORD attrs = isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    UINT flags = SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES;

    if (SHGetFileInfoW(fullPath.c_str(), attrs, &info, sizeof(info), flags)) {
        return info.szTypeName;
    }
    return isDirectory ? Tr(L"ファイル フォルダー") : L"";
}

int GetFileIconIndex(const std::wstring& fullPath, bool isDirectory) {
    SHFILEINFOW info{};
    DWORD attrs = isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    UINT flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;

    if (SHGetFileInfoW(fullPath.c_str(), attrs, &info, sizeof(info), flags)) {
        return info.iIcon;
    }
    return -1;
}
