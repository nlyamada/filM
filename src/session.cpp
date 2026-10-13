#include "session.h"

#include "json.h"
#include "paths.h"
#include "text_file_io.h"

namespace {

HANDLE g_primaryMutex = nullptr;
bool g_isPrimary = false;

}  // namespace

bool AcquirePrimaryInstance() {
    // 名前付きミューテックスを最初に作れたプロセスだけがセッションの所有者になる。
    // ハンドルはプロセス終了まで保持する (終了すれば OS が解放し、次の起動が所有者になれる)。
    g_primaryMutex = CreateMutexW(nullptr, FALSE, L"Local\\filM.PrimaryInstance");
    g_isPrimary = g_primaryMutex != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
    return g_isPrimary;
}

bool IsPrimaryInstance() {
    return g_isPrimary;
}

std::wstring GetSessionFilePath() {
    return GetConfigFilePath(L"session.json");
}

SessionState LoadSession() {
    SessionState state;
    if (!g_isPrimary) {
        return state;  // 2つ目以降のインスタンスは先行インスタンスのセッションを読み込まない
    }

    std::wstring path = GetSessionFilePath();
    std::wstring text;
    if (path.empty() || !ReadUtf16File(path, text)) {
        return state;
    }

    JsonValue root;
    if (!ParseJson(text, root) || root.type != JsonType::Object) {
        return state;
    }

    if (const JsonValue* win = root.Get(L"window")) {
        state.window.left = static_cast<int>(win->Get(L"left") ? win->Get(L"left")->AsNumber() : 0);
        state.window.top = static_cast<int>(win->Get(L"top") ? win->Get(L"top")->AsNumber() : 0);
        state.window.width = static_cast<int>(win->Get(L"width") ? win->Get(L"width")->AsNumber(1024) : 1024);
        state.window.height = static_cast<int>(win->Get(L"height") ? win->Get(L"height")->AsNumber(768) : 768);
        state.window.maximized = win->Get(L"maximized") ? win->Get(L"maximized")->AsBool() : false;
        state.window.valid = true;
    }

    if (const JsonValue* v = root.Get(L"favoritesWidth")) state.favoritesWidth = static_cast<int>(v->AsNumber(0));
    if (const JsonValue* v = root.Get(L"keyPanelWidth")) state.keyPanelWidth = static_cast<int>(v->AsNumber(0));
    if (const JsonValue* v = root.Get(L"previewHeight")) state.previewHeight = static_cast<int>(v->AsNumber(0));

    auto parseTabs = [](const JsonValue& tabs, std::vector<SessionTabState>& out) {
        if (tabs.type != JsonType::Array) return;
        for (const JsonValue& tab : tabs.arrayValue) {
            SessionTabState t;
            if (const JsonValue* dir = tab.Get(L"directory")) {
                t.directory = dir->AsString();
            }
            if (const JsonValue* cursor = tab.Get(L"cursorItem")) {
                t.cursorItem = cursor->AsString();
            }
            if (const JsonValue* v = tab.Get(L"sortKey")) t.sortKey = static_cast<int>(v->AsNumber(0));
            if (const JsonValue* v = tab.Get(L"sortDescending")) t.sortDescending = v->AsBool();
            if (const JsonValue* v = tab.Get(L"viewMode")) t.viewMode = static_cast<int>(v->AsNumber(0));
            if (!t.directory.empty()) {
                out.push_back(std::move(t));
            }
        }
    };

    if (const JsonValue* sets = root.Get(L"tabSets"); sets && sets->type == JsonType::Array) {
        for (const JsonValue& s : sets->arrayValue) {
            SessionTabSet set;
            if (const JsonValue* v = s.Get(L"name")) set.name = v->AsString();
            if (const JsonValue* v = s.Get(L"activeTabIndex")) set.activeTabIndex = static_cast<int>(v->AsNumber(0));
            if (const JsonValue* tabs = s.Get(L"tabs")) parseTabs(*tabs, set.tabs);
            state.sets.push_back(std::move(set));
        }
        if (const JsonValue* v = root.Get(L"activeSet")) state.activeSet = static_cast<int>(v->AsNumber(0));
    } else if (const JsonValue* tabs = root.Get(L"tabs")) {
        // 旧形式: タブの組が 1 つだけ。
        SessionTabSet set;
        if (const JsonValue* v = root.Get(L"activeTabIndex")) set.activeTabIndex = static_cast<int>(v->AsNumber(0));
        parseTabs(*tabs, set.tabs);
        state.sets.push_back(std::move(set));
    }

    return state;
}

void SaveSession(const SessionState& state) {
    if (!g_isPrimary) {
        return;  // 先行インスタンスのセッションを上書きしない
    }
    std::wstring path = GetSessionFilePath();
    if (path.empty()) {
        return;
    }

    JsonValue root = JsonValue::MakeObject();

    JsonValue win = JsonValue::MakeObject();
    win.Set(L"left", JsonValue::MakeNumber(state.window.left));
    win.Set(L"top", JsonValue::MakeNumber(state.window.top));
    win.Set(L"width", JsonValue::MakeNumber(state.window.width));
    win.Set(L"height", JsonValue::MakeNumber(state.window.height));
    win.Set(L"maximized", JsonValue::MakeBool(state.window.maximized));
    root.Set(L"window", std::move(win));

    root.Set(L"favoritesWidth", JsonValue::MakeNumber(state.favoritesWidth));
    root.Set(L"keyPanelWidth", JsonValue::MakeNumber(state.keyPanelWidth));
    root.Set(L"previewHeight", JsonValue::MakeNumber(state.previewHeight));
    root.Set(L"activeSet", JsonValue::MakeNumber(state.activeSet));

    JsonValue sets = JsonValue::MakeArray();
    for (const SessionTabSet& s : state.sets) {
        JsonValue set = JsonValue::MakeObject();
        set.Set(L"name", JsonValue::MakeString(s.name));
        set.Set(L"activeTabIndex", JsonValue::MakeNumber(s.activeTabIndex));
        JsonValue tabs = JsonValue::MakeArray();
        for (const SessionTabState& t : s.tabs) {
            JsonValue tab = JsonValue::MakeObject();
            tab.Set(L"directory", JsonValue::MakeString(t.directory));
            tab.Set(L"cursorItem", JsonValue::MakeString(t.cursorItem));
            tab.Set(L"sortKey", JsonValue::MakeNumber(t.sortKey));
            tab.Set(L"sortDescending", JsonValue::MakeBool(t.sortDescending));
            tab.Set(L"viewMode", JsonValue::MakeNumber(t.viewMode));
            tabs.arrayValue.push_back(std::move(tab));
        }
        set.Set(L"tabs", std::move(tabs));
        sets.arrayValue.push_back(std::move(set));
    }
    root.Set(L"tabSets", std::move(sets));

    // アトミック書き込み: 一時ファイルに書いてからリネームする。
    WriteUtf16FileAtomic(path, DumpJson(root));
}
