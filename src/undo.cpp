#include "undo.h"
#include "i18n.h"

namespace {

enum class Kind { Created, Moved, Trashed, Attributes };

struct Record {
    Kind kind;
    std::wstring description;
    std::vector<std::wstring> paths;                      // Created / Trashed
    std::vector<OpPair> pairs;                            // Moved
    std::vector<std::pair<std::wstring, DWORD>> attrs;    // Attributes
};

constexpr size_t kMaxHistory = 50;
std::vector<Record> g_history;

void Push(Record r) {
    g_history.push_back(std::move(r));
    if (g_history.size() > kMaxHistory) {
        g_history.erase(g_history.begin());
    }
}

bool Exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring ParentOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L'\\');
    if (pos == std::wstring::npos) return L"";
    if (pos == 2 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, pos);
}

std::wstring NameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

bool EqualNoCase(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

}  // namespace

void PushUndoCreated(const std::wstring& description, const std::vector<std::wstring>& createdPaths) {
    if (createdPaths.empty()) return;
    Record r{Kind::Created, description, createdPaths, {}, {}};
    Push(std::move(r));
}

void PushUndoMoved(const std::wstring& description, const std::vector<OpPair>& movedPairs) {
    std::vector<OpPair> usable;
    for (const OpPair& p : movedPairs) {
        if (!p.from.empty() && !p.to.empty()) usable.push_back(p);
    }
    if (usable.empty()) return;
    Record r{Kind::Moved, description, {}, usable, {}};
    Push(std::move(r));
}

void PushUndoTrashed(const std::wstring& description, const std::vector<OpPair>& trashedPairs) {
    std::vector<OpPair> usable;
    for (const OpPair& p : trashedPairs) {
        if (!p.from.empty() && !p.to.empty()) usable.push_back(p);
    }
    if (usable.empty()) return;
    Record r{Kind::Trashed, description, {}, usable, {}};
    Push(std::move(r));
}

void PushUndoAttributes(const std::wstring& description,
                        const std::vector<std::pair<std::wstring, DWORD>>& oldAttributes) {
    if (oldAttributes.empty()) return;
    Record r{Kind::Attributes, description, {}, {}, oldAttributes};
    Push(std::move(r));
}

bool CanUndo() {
    return !g_history.empty();
}

bool UndoLast(HWND hwndOwner, std::wstring& message) {
    if (g_history.empty()) {
        message = Tr(L"元に戻せる操作がありません。");
        return false;
    }
    Record r = std::move(g_history.back());
    g_history.pop_back();

    size_t done = 0;
    size_t total = 0;
    std::wstring failure;

    switch (r.kind) {
        case Kind::Created: {
            std::vector<std::wstring> existing;
            for (const std::wstring& p : r.paths) {
                if (Exists(p)) existing.push_back(p);
            }
            total = r.paths.size();
            if (existing.empty()) {
                failure = Tr(L"作成したファイルが見つかりません。");
            } else if (DeleteItems(hwndOwner, existing, false, nullptr)) {
                done = existing.size();
            } else {
                failure = Tr(L"ごみ箱へ移せませんでした。");
            }
            break;
        }
        case Kind::Moved: {
            total = r.pairs.size();
            for (const OpPair& p : r.pairs) {
                if (!Exists(p.to)) {
                    failure = Fmt(L"移動後のファイルが見つかりません: {0}", {p.to});
                    continue;
                }
                std::wstring current = p.to;
                std::wstring origParent = ParentOf(p.from);
                if (!EqualNoCase(ParentOf(current), origParent)) {
                    std::vector<OpPair> moved;
                    if (!MoveItems(hwndOwner, {current}, origParent, &moved) || moved.empty() ||
                        moved[0].to.empty()) {
                        failure = Fmt(L"元の場所へ戻せませんでした: {0}", {p.from});
                        continue;
                    }
                    current = moved[0].to;
                }
                if (!EqualNoCase(NameOf(current), NameOf(p.from))) {
                    if (!RenameItem(hwndOwner, current, NameOf(p.from), nullptr)) {
                        failure = Fmt(L"元の名前へ戻せませんでした: {0}", {p.from});
                        continue;
                    }
                }
                ++done;
            }
            break;
        }
        case Kind::Trashed: {
            total = r.pairs.size();
            for (const OpPair& p : r.pairs) {
                if (Exists(p.from)) {
                    failure = Fmt(L"同じ名前のファイルが既にあります: {0}", {p.from});
                    continue;
                }
                if (RestoreFromRecycleBin(hwndOwner, p.from, p.to)) {
                    ++done;
                } else {
                    failure = Fmt(L"ごみ箱から復元できませんでした (空にされた可能性があります): {0}", {p.from});
                }
            }
            break;
        }
        case Kind::Attributes: {
            total = r.attrs.size();
            for (const auto& a : r.attrs) {
                if (SetFileAttributesW(a.first.c_str(), a.second)) {
                    ++done;
                } else {
                    failure = Fmt(L"属性を戻せませんでした: {0}", {a.first});
                }
            }
            break;
        }
    }

    message = Fmt(L"「{0}」を元に戻しました", {r.description});
    if (total > 1 || done != total) {
        message += L" (" + Fmt(L"{0} / {1} 件", {Num(done), Num(total)}) + L")";
    }
    message += L"。";
    if (done == 0) {
        message = Fmt(L"「{0}」を元に戻せませんでした。", {r.description});
    }
    if (!failure.empty()) {
        message += L"\n" + failure;
    }
    return done > 0;
}

