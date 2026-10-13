#include "keyconfig.h"
#include "i18n.h"

#include <map>

#include "json.h"
#include "paths.h"
#include "text_file_io.h"

namespace {

constexpr int kKeymapVersion = 2;

// キー名 <-> 仮想キーコードの対応表 (英数字1文字はそのまま VK コードと一致するので表には含めない)。
const std::map<std::wstring, WORD>& NamedKeyTable() {
    static const std::map<std::wstring, WORD> table = {
        {L"Delete", VK_DELETE},   {L"Insert", VK_INSERT},  {L"Enter", VK_RETURN},
        {L"Return", VK_RETURN},   {L"Multiply", VK_MULTIPLY},
        {L"Backspace", VK_BACK},  {L"Escape", VK_ESCAPE},  {L"Esc", VK_ESCAPE},
        {L"Tab", VK_TAB},         {L"Space", VK_SPACE},    {L"Home", VK_HOME},
        {L"End", VK_END},         {L"PageUp", VK_PRIOR},   {L"PageDown", VK_NEXT},
        {L"Left", VK_LEFT},       {L"Right", VK_RIGHT},    {L"Up", VK_UP},
        {L"Down", VK_DOWN},       {L"F1", VK_F1},          {L"F2", VK_F2},
        {L"F3", VK_F3},           {L"F4", VK_F4},          {L"F5", VK_F5},
        {L"F6", VK_F6},           {L"F7", VK_F7},          {L"F8", VK_F8},
        {L"F9", VK_F9},           {L"F10", VK_F10},        {L"F11", VK_F11},
        {L"F12", VK_F12},         {L",", VK_OEM_COMMA},    {L".", VK_OEM_PERIOD},
    };
    return table;
}

struct CommandInfo {
    const wchar_t* name;
    const wchar_t* label;
};

// コマンド名と画面表示用の説明。
const CommandInfo kCommandInfos[] = {
    {L"Attributes", L"属性変更"},
    {L"Duplicate", L"複製"},
    {L"CopyToTab", L"他のタブへコピー"},
    {L"CopyToFolder", L"フォルダを指定してコピー"},
    {L"Delete", L"ごみ箱へ"},
    {L"DeletePermanent", L"完全に削除"},
    {L"Edit", L"テキストエディタで編集"},
    {L"EditAliases", L"エイリアスの編集"},
    {L"CopyPath", L"フルパスのコピー"},
    {L"GotoFolder", L"フォルダ移動(指定)"},
    {L"ToggleMarkAll", L"全選択/解除"},
    {L"Properties", L"プロパティ"},
    {L"FocusAliasList", L"エイリアス一覧へ"},
    {L"RegisterAlias", L"エイリアスに登録"},
    {L"MakeFolder", L"フォルダ作成"},
    {L"MoveToTab", L"他のタブへ移動"},
    {L"MoveToFolder", L"フォルダを指定して移動"},
    {L"Rename", L"リネーム"},
    {L"OpenTabAt", L"タブを開く(指定)"},
    {L"Zip", L"zip圧縮"},
    {L"CloseTab", L"タブを閉じる"},
    {L"Refresh", L"最新の情報に更新"},
    {L"CreateShortcut", L"ショートカット作成"},
    {L"NewTab", L"現在のフォルダーで新タブ"},
    {L"Unzip", L"zip解凍"},
    {L"UnzipToFolder", L"zip解凍(フォルダを作る)"},
    {L"TogglePreview", L"プレビュー表示の切替"},
    {L"MaximizePreview", L"プレビューを広げる/戻す"},
    {L"FocusPreview", L"プレビューへ移動"},
    {L"Settings", L"設定"},
    {L"OpenPowerShell", L"PowerShellを起動"},
    {L"OpenCmd", L"コマンドプロンプトを起動"},
    {L"Undo", L"元に戻す"},
    {L"NavigateToParent", L"上位フォルダーへ"},
    {L"NavigateToRoot", L"ルートフォルダーへ"},
    {L"PrevTab", L"前のタブ"},
    {L"NextTab", L"次のタブ"},
    {L"MoveTabLeft", L"タブを左へ移動"},
    {L"MoveTabRight", L"タブを右へ移動"},
    {L"TabSetMenu", L"タブセットの切替"},
    {L"NewTabSet", L"タブセットを追加"},
    {L"ToggleMark", L"選択して次へ"},
    {L"FocusList", L"ファイルリストへ"},
    {L"NavigateInto", L"開く/実行"},
    {L"OpenWith", L"プログラムを選択して実行"},
    {L"ToggleFocus", L"一覧とエイリアス欄を切替"},
    {L"ToggleFavoritesPanel", L"エイリアス欄の表示切替"},
    {L"ToggleKeyPanel", L"キー一覧の表示切替"},
    {L"ClipCopy", L"クリップボードへコピー"},
    {L"ClipCut", L"クリップボードへ切り取り"},
    {L"Paste", L"貼り付け"},
    {L"SortName", L"名前順"},
    {L"SortExt", L"種類順"},
    {L"SortSize", L"サイズ順"},
    {L"SortDate", L"日付順"},
    {L"ToggleView", L"表示モード切替"},
    {L"MarkAll", L"全件マーク"},
    {L"ClearMarks", L"マーク解除"},
    {L"InvertMarks", L"マーク反転"},
};

}  // namespace

std::wstring GetKeyConfigFilePath() {
    return GetConfigFilePath(L"keymap.json");
}

std::vector<KeyBinding> GetDefaultKeyBindings() {
    return {
        // ---- 1キー操作 (一覧にフォーカスがある時) ----
        {L"Attributes", L"A", false, false, false},
        {L"Duplicate", L"B", false, false, false},
        {L"CopyToTab", L"C", false, false, false},
        {L"CopyToFolder", L"C", false, false, true},
        {L"Delete", L"D", false, false, false},
        {L"DeletePermanent", L"D", false, false, true},
        {L"Edit", L"E", false, false, false},
        {L"EditAliases", L"E", false, false, true},
        {L"CopyPath", L"F", false, false, false},
        {L"GotoFolder", L"G", false, false, false},
        {L"ToggleMarkAll", L"H", false, false, false},
        {L"Properties", L"I", false, false, false},
        {L"FocusAliasList", L"J", false, false, false},
        {L"RegisterAlias", L"J", false, false, true},
        {L"MakeFolder", L"K", false, false, false},
        {L"TabSetMenu", L"L", false, false, false},
        {L"NewTabSet", L"L", false, false, true},
        {L"MoveToTab", L"M", false, false, false},
        {L"MoveToFolder", L"M", false, false, true},
        {L"Rename", L"N", false, false, false},
        {L"OpenTabAt", L"O", false, false, false},
        {L"Zip", L"P", false, false, false},
        {L"CloseTab", L"Q", false, false, false},
        {L"Refresh", L"R", false, false, false},
        {L"CreateShortcut", L"S", false, false, false},
        {L"NewTab", L"T", false, false, false},
        {L"Unzip", L"U", false, false, false},
        {L"UnzipToFolder", L"U", false, false, true},
        {L"TogglePreview", L"V", false, false, false},
        {L"MaximizePreview", L"V", false, false, true},
        {L"FocusPreview", L"Tab", false, false, false},
        {L"OpenPowerShell", L"X", false, false, false},
        {L"OpenCmd", L"X", false, false, true},
        {L"Undo", L"Z", false, false, false},
        {L"Delete", L"Delete", false, false, false},
        {L"DeletePermanent", L"Delete", false, false, true},
        {L"NavigateToParent", L"Backspace", false, false, false},
        {L"NavigateToRoot", L"Backspace", false, false, true},
        {L"PrevTab", L",", false, false, false},
        {L"NextTab", L".", false, false, false},
        {L"MoveTabLeft", L",", false, false, true},
        {L"MoveTabRight", L".", false, false, true},
        {L"ToggleMark", L"Space", false, false, false},
        {L"FocusList", L"Escape", false, false, false},
        {L"NavigateInto", L"Enter", false, false, false},
        {L"OpenWith", L"Enter", false, false, true},

        // ---- どこにフォーカスがあっても効くキー ----
        {L"ToggleFocus", L"Left", true, false, false},
        {L"ToggleFocus", L"Right", true, false, false},
        {L"ToggleFocus", L"Up", true, false, false},
        {L"ToggleFocus", L"Down", true, false, false},
        {L"NewTab", L"T", true, false, false},
        {L"CloseTab", L"W", true, false, false},
        {L"NextTab", L"Tab", true, false, false},
        {L"PrevTab", L"Tab", true, false, true},
        {L"RegisterAlias", L"D", true, false, false},
        {L"ToggleFavoritesPanel", L"B", true, false, false},
        {L"ToggleKeyPanel", L"F1", false, false, false},
        {L"Settings", L",", true, false, false},

        // ---- 従来のキー (CSV に無いもの。上と重ならない) ----
        {L"Rename", L"F2", false, false, false},
        {L"CopyToTab", L"F5", false, false, false},
        {L"MoveToTab", L"F6", false, false, false},
        {L"CopyPath", L"C", true, false, true},
        {L"ClipCopy", L"C", true, false, false},
        {L"ClipCut", L"X", true, false, false},
        {L"Paste", L"V", true, false, false},
        {L"SortName", L"1", true, false, false},
        {L"SortExt", L"2", true, false, false},
        {L"SortSize", L"3", true, false, false},
        {L"SortDate", L"4", true, false, false},
        {L"ToggleView", L"5", true, false, false},
        {L"ToggleMark", L"Insert", false, false, false},
        {L"MarkAll", L"A", true, false, false},
        {L"ClearMarks", L"A", true, false, true},
        {L"InvertMarks", L"Multiply", false, false, false},
    };
}

WORD KeyNameToVirtualKey(const std::wstring& name) {
    const auto& table = NamedKeyTable();
    auto it = table.find(name);
    if (it != table.end()) {
        return it->second;
    }
    if (name.size() == 1) {
        wchar_t c = towupper(name[0]);
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
            return static_cast<WORD>(c);
        }
    }
    return 0;
}

std::wstring VirtualKeyToKeyName(WORD vk) {
    if ((vk >= L'A' && vk <= L'Z') || (vk >= L'0' && vk <= L'9')) return std::wstring(1, static_cast<wchar_t>(vk));
    static const std::pair<WORD, const wchar_t*> kNames[] = {
        {VK_DELETE, L"Delete"}, {VK_INSERT, L"Insert"}, {VK_RETURN, L"Enter"}, {VK_MULTIPLY, L"Multiply"},
        {VK_BACK, L"Backspace"}, {VK_ESCAPE, L"Escape"}, {VK_TAB, L"Tab"}, {VK_SPACE, L"Space"},
        {VK_HOME, L"Home"}, {VK_END, L"End"}, {VK_PRIOR, L"PageUp"}, {VK_NEXT, L"PageDown"},
        {VK_LEFT, L"Left"}, {VK_RIGHT, L"Right"}, {VK_UP, L"Up"}, {VK_DOWN, L"Down"},
        {VK_OEM_COMMA, L","}, {VK_OEM_PERIOD, L"."},
    };
    for (const auto& n : kNames) {
        if (n.first == vk) return n.second;
    }
    if (vk >= VK_F1 && vk <= VK_F12) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    return L"";
}

std::wstring FormatKeyCombo(const KeyBinding& b) {
    std::wstring s;
    if (b.ctrl) s += L"Ctrl+";
    if (b.alt) s += L"Alt+";
    if (b.shift) s += L"Shift+";
    return s + b.key;
}

std::wstring GetCommandLabel(const std::wstring& command) {
    for (const CommandInfo& c : kCommandInfos) {
        if (command == c.name) return Tr(c.label);
    }
    return L"";
}

const std::vector<PanelKey>& GetPanelKeys() {
    static const std::vector<PanelKey> keys = {
        {L"A", L"A"}, {L"B", L"B"}, {L"C", L"C"}, {L"D", L"D"}, {L"E", L"E"}, {L"F", L"F"}, {L"G", L"G"},
        {L"H", L"H"}, {L"I", L"I"}, {L"J", L"J"}, {L"K", L"K"}, {L"L", L"L"}, {L"M", L"M"}, {L"N", L"N"},
        {L"O", L"O"}, {L"P", L"P"}, {L"Q", L"Q"}, {L"R", L"R"}, {L"S", L"S"}, {L"T", L"T"}, {L"U", L"U"},
        {L"V", L"V"}, {L"W", L"W"}, {L"X", L"X"}, {L"Y", L"Y"}, {L"Z", L"Z"},
        {L"Delete", L"Del"}, {L"Backspace", L"BS"}, {L",", L","}, {L".", L"."},
        {L"Space", L"Spc"}, {L"Escape", L"Esc"}, {L"Enter", L"Ent"},
    };
    return keys;
}

std::wstring FindCommandForKey(const std::vector<KeyBinding>& bindings, const std::wstring& key, bool shift) {
    WORD vk = KeyNameToVirtualKey(key);
    for (const KeyBinding& b : bindings) {
        if (!b.ctrl && !b.alt && b.shift == shift && vk != 0 && KeyNameToVirtualKey(b.key) == vk) {
            return b.command;
        }
    }
    return L"";
}

namespace {

// 以前のコマンド名を、いまの名前に読み替える (フォトビューアーと Markdown ビューアーはプレビュー欄に一本化した)。
std::wstring CanonicalCommandName(const std::wstring& name) {
    if (name == L"PhotoViewer") return L"TogglePreview";
    if (name == L"MarkdownViewer") return L"MaximizePreview";
    return name;
}

std::vector<KeyBinding> ParseKeyBindings(const JsonValue& list) {
    std::vector<KeyBinding> bindings;
    if (list.type != JsonType::Array) {
        return bindings;
    }
    for (const JsonValue& item : list.arrayValue) {
        KeyBinding b;
        if (const JsonValue* v = item.Get(L"command")) b.command = CanonicalCommandName(v->AsString());
        if (const JsonValue* v = item.Get(L"key")) b.key = v->AsString();
        if (const JsonValue* v = item.Get(L"ctrl")) b.ctrl = v->AsBool();
        if (const JsonValue* v = item.Get(L"alt")) b.alt = v->AsBool();
        if (const JsonValue* v = item.Get(L"shift")) b.shift = v->AsBool();
        if (!b.command.empty() && !b.key.empty()) {
            bindings.push_back(std::move(b));
        }
    }
    return bindings;
}

JsonValue SerializeKeyBindings(const std::vector<KeyBinding>& bindings) {
    JsonValue list = JsonValue::MakeArray();
    for (const KeyBinding& b : bindings) {
        JsonValue item = JsonValue::MakeObject();
        item.Set(L"command", JsonValue::MakeString(b.command));
        item.Set(L"key", JsonValue::MakeString(b.key));
        item.Set(L"ctrl", JsonValue::MakeBool(b.ctrl));
        item.Set(L"alt", JsonValue::MakeBool(b.alt));
        item.Set(L"shift", JsonValue::MakeBool(b.shift));
        list.arrayValue.push_back(std::move(item));
    }
    JsonValue root = JsonValue::MakeObject();
    root.Set(L"version", JsonValue::MakeNumber(kKeymapVersion));
    // 書き出した時点で存在したコマンド名。これに無い新コマンドだけ、次回読み込み時に既定キーを補う。
    JsonValue known = JsonValue::MakeArray();
    for (const CommandInfo& c : kCommandInfos) known.arrayValue.push_back(JsonValue::MakeString(c.name));
    root.Set(L"commands", std::move(known));
    root.Set(L"bindings", std::move(list));
    return root;
}

void WriteKeyConfig(const std::wstring& path, const std::vector<KeyBinding>& bindings) {
    std::wstring dir = GetConfigDirectory();
    if (dir.empty()) {
        return;
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    WriteUtf16File(path, DumpJson(SerializeKeyBindings(bindings)));
}

}  // namespace

void SaveKeyConfig(const std::vector<KeyBinding>& bindings) {
    std::wstring path = GetKeyConfigFilePath();
    if (!path.empty()) WriteKeyConfig(path, bindings);
}

std::vector<std::pair<std::wstring, std::wstring>> GetAllCommands() {
    std::vector<std::pair<std::wstring, std::wstring>> list;
    for (const CommandInfo& c : kCommandInfos) list.emplace_back(c.name, Tr(c.label));
    return list;
}

std::vector<KeyBinding> LoadKeyConfig() {
    std::vector<KeyBinding> defaults = GetDefaultKeyBindings();

    std::wstring path = GetKeyConfigFilePath();
    if (path.empty()) {
        return defaults;
    }

    std::wstring text;
    if (!ReadUtf16File(path, text)) {
        // ファイルが無い (または読めない): デフォルトを書き出してユーザーが編集できるようにする。
        WriteKeyConfig(path, defaults);
        return defaults;
    }

    JsonValue root;
    if (!ParseJson(text, root)) {
        return defaults;  // 壊れている場合はファイルを上書きせずデフォルトにフォールバック
    }

    if (root.type == JsonType::Array) {
        // 旧形式 (version 1)。キーの意味が変わったもの (Z, Esc, Shift+Enter 等) があるため、
        // 内容を keymap.v1.json に退避して、新しい既定値で作り直す。
        std::wstring backup = GetConfigDirectory() + L"\\keymap.v1.json";
        MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);
        WriteKeyConfig(path, defaults);
        return defaults;
    }

    const JsonValue* list = root.type == JsonType::Object ? root.Get(L"bindings") : nullptr;
    std::vector<KeyBinding> bindings = list ? ParseKeyBindings(*list) : std::vector<KeyBinding>();
    if (bindings.empty()) {
        return defaults;
    }
    // バージョンアップで増えた新コマンドの既定キーを補う (既存ファイルに同名コマンドが
    // 1つも無い場合のみ。ユーザーが変えたキー割り当ては尊重する)。
    std::vector<std::wstring> knownCommands;
    if (const JsonValue* known = root.Get(L"commands"); known && known->type == JsonType::Array) {
        for (const JsonValue& c : known->arrayValue) knownCommands.push_back(CanonicalCommandName(c.AsString()));
    }
    std::vector<KeyBinding> merged;
    merged.reserve(bindings.size() + defaults.size());
    merged.assign(bindings.begin(), bindings.end());
    for (const KeyBinding& d : defaults) {
        bool found = false;
        for (const std::wstring& c : knownCommands) {
            if (c == d.command) found = true;
        }
        for (const KeyBinding& b : bindings) {
            if (b.command == d.command) {
                found = true;
                break;
            }
        }
        if (!found) {
            merged.push_back(d);
        }
    }
    return merged;
}
