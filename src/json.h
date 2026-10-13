// 設定ファイル用の小さな JSON 実装。
//
// 外部ライブラリを使わない方針のため自前で用意する。文字列は wstring で扱い、
// ファイルへの読み書きは UTF-16LE (BOM付き) を前提とする。自分自身が書き出した
// ファイルを読み戻すだけの用途なので、外部相互運用は考慮しない。

#pragma once

#include <string>
#include <utility>
#include <vector>

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool boolValue = false;
    double numberValue = 0;
    std::wstring stringValue;
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::wstring, JsonValue>> objectValue;

    static JsonValue MakeObject();
    static JsonValue MakeArray();
    static JsonValue MakeString(const std::wstring& s);
    static JsonValue MakeNumber(double n);
    static JsonValue MakeBool(bool b);

    // オブジェクト用。同じキーが既にあれば上書きする。
    void Set(const std::wstring& key, JsonValue value);
    // オブジェクト用。無ければ nullptr。
    const JsonValue* Get(const std::wstring& key) const;

    std::wstring AsString(const std::wstring& defaultValue = L"") const;
    double AsNumber(double defaultValue = 0) const;
    bool AsBool(bool defaultValue = false) const;
};

// text をパースして out に格納する。成功したら true。
bool ParseJson(const std::wstring& text, JsonValue& out);

// value を整形済み (2スペースインデント) の JSON 文字列にする。
std::wstring DumpJson(const JsonValue& value);
