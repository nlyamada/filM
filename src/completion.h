// フォルダ入力ダイアログの TAB 補完 (エイリアス名 / フォルダ名)
//
// 入力全体を1つのパスとして扱う (空白を含んでよい。引用符は無視する)。
// 先頭の部分がエイリアス名なら、実際のフォルダに読み替えて候補を探す。
// 例: エイリアス HOME = C:\home のとき、"HOME\l" → C:\home 内の "l" で始まるフォルダを探し、
// 候補は入力の形を保って "HOME\lib\" のように返す。

#pragma once

#include <string>
#include <vector>

struct CompletionResult {
    size_t replaceStart = 0;  // text 中で置き換える範囲の先頭
    size_t replaceEnd = 0;    // 置き換える範囲の末尾 (= caret)
    std::vector<std::wstring> candidates;  // 置き換え後の文字列。空なら候補なし
};

// caret 位置までの入力 text を対象に補完候補を求める。baseDir は相対パスの基準 (カレントフォルダ)。
CompletionResult ComputeFolderCompletions(const std::wstring& text, size_t caret, const std::wstring& baseDir);
