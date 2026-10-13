// ディレクトリ列挙とファイル操作部への表示用データ

#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct FileEntry {
    std::wstring name;
    bool isDirectory = false;
    // OneDrive 等のクラウドプレースホルダー (開く・データ参照でダウンロードが走る) かどうか。
    // 一覧表示のために中身へアクセスしてはいけないことの目印として、種類欄に表示する。
    bool isCloudPlaceholder = false;
    ULONGLONG size = 0;
    FILETIME lastWriteTime{};
};

// ソートキー。フォルダは常にファイルより前に並べ、同順位は名前の昇順で決める。
enum class SortKey { Name = 0, Ext = 1, Size = 2, Date = 3 };

// 指定ディレクトリ直下のファイル・フォルダを列挙する。
// フォルダを先頭に、key/descending の順序で返す (名前は大文字小文字区別なし)。
std::vector<FileEntry> EnumerateDirectory(const std::wstring& directory,
                                          SortKey key = SortKey::Name, bool descending = false);

// バイト数を "123.45KB" のような表示用文字列に整形する。
// ディレクトリの場合は空文字列を返す。
std::wstring FormatFileSize(ULONGLONG bytes, bool isDirectory);

// FILETIME をローカル時刻の "yyyy/MM/dd HH:mm" 形式に整形する。
std::wstring FormatFileTime(const FILETIME& ft);

// ファイルの種類名 (「ファイル フォルダー」「テキスト ドキュメント」等) を取得する。
std::wstring GetFileTypeName(const std::wstring& fullPath, bool isDirectory);

// システムイメージリスト上のアイコンインデックスを取得する。取得失敗時は -1。
int GetFileIconIndex(const std::wstring& fullPath, bool isDirectory);
