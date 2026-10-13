// 複数ファイルのマーク状態 (タブごとに1つ持つ)
//
// 名前の大文字小文字は区別しない (Windows のファイル名の扱いに合わせる)。
// 表示は選択行とは別の背景色で行う (main.cpp の NM_CUSTOMDRAW 参照)。

#pragma once

#include <cwctype>
#include <iterator>
#include <map>
#include <string>
#include <vector>

class MarkSet {
public:
    void Toggle(const std::wstring& name) {
        std::wstring key = Fold(name);
        auto it = names_.find(key);
        if (it != names_.end()) {
            names_.erase(it);
        } else {
            names_[key] = name;
        }
    }
    void Add(const std::wstring& name) { names_[Fold(name)] = name; }
    bool Contains(const std::wstring& name) const { return names_.count(Fold(name)) > 0; }
    size_t Count() const { return names_.size(); }
    void Clear() { names_.clear(); }
    // 登録された名前 (元の表記) の一覧。
    std::vector<std::wstring> Names() const {
        std::vector<std::wstring> result;
        for (const auto& kv : names_) result.push_back(kv.second);
        return result;
    }
    // 存在しなくなった名前を取り除く (一覧の再読み込み後に呼ぶ)。
    template <typename Pred>
    void RemoveIf(Pred notExists) {
        for (auto it = names_.begin(); it != names_.end();) {
            it = notExists(it->second) ? names_.erase(it) : std::next(it);
        }
    }

private:
    static std::wstring Fold(const std::wstring& s) {
        std::wstring r = s;
        for (wchar_t& c : r) c = static_cast<wchar_t>(towupper(c));
        return r;
    }
    std::map<std::wstring, std::wstring> names_;
};
