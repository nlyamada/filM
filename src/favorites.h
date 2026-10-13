// エイリアス機能 (旧「お気に入り」と「ディレクトリエイリアス」を統合したもの)
//
// エイリアスはフォルダだけを登録する。名前 (例: HOME) を入力すると、そのフォルダへの
// 移動や、パス入力での先頭部分の読み替え (HOME\lib → C:\home\lib) に使える。
// 各項目は任意の1文字のショートカットキー (呼び出しキー) を持てる。
//
// 永続化: exe と同じフォルダ\favorites.json (変更のたびに保存)。
// 旧バージョンの aliases.json (コマンドのエイリアス) は使わなくなった (読み込まない)。
// favorites.json はテキストエディタで直接編集でき、アプリへの復帰時に変更を検出して読み直す。

#pragma once

#include <string>
#include <vector>

struct FavoriteEntry {
    std::wstring name;
    std::wstring path;
    wchar_t key = 0;  // 呼び出しキー (英数字1文字を大文字で保持)。0 なら未設定
};

enum class AddResult { Added, DuplicatePath, DuplicateName };

// path を name という名前でエイリアスに追加する。key は省略可 (0)。
// 同じ path、または同じ name (大文字小文字を区別しない) が登録済みの場合は何もしない。
AddResult AddFavorite(const std::wstring& name, const std::wstring& path, wchar_t key = 0);

// index 番目の項目を削除する。
void RemoveFavoriteAt(size_t index);

// index 番目の呼び出しキーを設定する (0 で解除)。他の項目が同じキーを持っていればそちらを解除する。
void SetFavoriteKey(size_t index, wchar_t key);

// index 番目の名前・呼び出しキー・フォルダを更新する。他の項目と名前が重なる場合は false。
bool UpdateFavorite(size_t index, const std::wstring& name, wchar_t key, const std::wstring& path);

// 登録済みの一覧を取得する。
const std::vector<FavoriteEntry>& GetFavorites();

// 呼び出しキー key (大文字小文字は区別しない) を持つ項目のインデックスを返す。無ければ -1。
int FindFavoriteByKey(wchar_t key);

// 名前 (大文字小文字を区別しない) で項目を探す。無ければ nullptr。
const FavoriteEntry* FindFavoriteByName(const std::wstring& name);

// 入力の先頭がエイリアス名 (単独、または "名前\..." "名前/...") なら、その部分をフォルダのパスに
// 置き換えて返す。エイリアスでなければ入力をそのまま返す (expanded が非 null なら結果を入れる)。
std::wstring ExpandAliasPath(const std::wstring& input, bool* expanded = nullptr);

// favorites.json から読み込む。ファイルが無い/壊れている場合は現在の内容を保つ (false)。
bool LoadFavorites();

// 保存されている favorites.json が自分の最後の読み書きから変わっていれば (テキストエディタでの
// 編集など) 読み直して true を返す。
bool ReloadFavoritesIfChanged();

// favorites.json のフルパス。
std::wstring GetFavoritesFilePath();
