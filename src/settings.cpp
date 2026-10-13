#include "settings.h"

#include "json.h"
#include "paths.h"
#include "text_file_io.h"

namespace {
AppSettings g_settings;
}

AppSettings& GetSettings() {
    return g_settings;
}

void LoadSettings() {
    g_settings = AppSettings();
    g_settings.language = DetectSystemLanguage();

    std::wstring path = GetConfigFilePath(L"settings.json");
    std::wstring text;
    JsonValue root;
    if (!path.empty() && ReadUtf16File(path, text) && ParseJson(text, root) && root.type == JsonType::Object) {
        if (const JsonValue* v = root.Get(L"language")) {
            g_settings.language = v->AsString() == L"en" ? Language::English : Language::Japanese;
        }
        if (const JsonValue* v = root.Get(L"theme")) {
            std::wstring t = v->AsString();
            g_settings.theme = t == L"dark" ? ThemeMode::Dark : t == L"light" ? ThemeMode::Light : ThemeMode::System;
        }
        if (const JsonValue* v = root.Get(L"markdownRemoteImages")) g_settings.markdownRemoteImages = v->AsBool(false);
        if (const JsonValue* v = root.Get(L"editor")) {
            g_settings.editor = v->AsString();
            if (g_settings.editor == L"builtin") g_settings.editor.clear();  // 内蔵エディタは廃止した (試作版の設定)
        }
    }
    SetLanguage(g_settings.language);
}

void SaveSettings() {
    std::wstring path = GetConfigFilePath(L"settings.json");
    if (path.empty()) return;
    JsonValue root = JsonValue::MakeObject();
    root.Set(L"version", JsonValue::MakeNumber(2));
    root.Set(L"language", JsonValue::MakeString(g_settings.language == Language::English ? L"en" : L"ja"));
    root.Set(L"theme", JsonValue::MakeString(g_settings.theme == ThemeMode::Dark    ? L"dark"
                                             : g_settings.theme == ThemeMode::Light ? L"light"
                                                                                    : L"system"));
    root.Set(L"editor", JsonValue::MakeString(g_settings.editor));
    root.Set(L"markdownRemoteImages", JsonValue::MakeBool(g_settings.markdownRemoteImages));
    WriteUtf16FileAtomic(path, DumpJson(root));
}
