# filM

**English** | [日本語](#日本語)

A keyboard-driven two-in-one file manager for Windows 11, written in C++17 / Win32 — a tribute to the classic Japanese file managers *filmtn* and *SpringM* (hence **fil**mtn + Spring**M**).

- Single-key commands (`C` copy, `M` move, `D` delete, `N` rename ...) and a live **key list** that switches to the Shift+key commands while you hold Shift
- Tabs, **tab sets** (like virtual desktops), folder **aliases** with TAB completion, marks (multi-select), unlimited-ish **undo** (including the Recycle Bin)
- **Viewer** below the file list (`V` on / off, follows the cursor, `Esc` closes): images (WIC: JPEG / PNG / GIF / BMP / TIFF, HEIC / WebP when Windows has the codec; EXIF rotation), Markdown (WebView2 + marked / KaTeX / mermaid) and text (view only; `E` opens your text editor)
- **Settings dialog** (`Ctrl+,`): key bindings editor, text editor, theme (system / light / dark), language
- English / Japanese UI (switch at the bottom right)
- Portable: config files (`keymap.json`, `favorites.json`, `session.json`, `settings.json`) live next to the exe

See [docs/USAGE.en.md](docs/USAGE.en.md) for the full key list and features.

## Requirements
- Windows 10 / 11, x64
- Markdown rendering only: Microsoft Edge WebView2 Runtime (preinstalled on Windows 11) and `WebView2Loader.dll` + `viewer\` next to the exe (without them Markdown is shown as plain text)

## Build
Install [MSYS2](https://www.msys2.org/) with the MinGW-w64 toolchain (`pacman -S mingw-w64-x86_64-gcc make`), then:

```powershell
.\build.ps1          # builds bin\filM.exe and copies WebView2Loader.dll and viewer\ into bin\
.\build.ps1 test     # runs the logic tests
.\build.ps1 clean
.\tools\package.ps1  # builds and creates dist\filM-<date>.zip
```

The exe is linked statically; no runtime DLLs are needed apart from the optional `WebView2Loader.dll`.

## Layout
- `src/` — sources (`main.cpp` UI and commands, `keyconfig` key map, `settings*` settings and dialog, `preview_pane` + `image_view` + `markdown_view` + `text_codec` viewer, `theme`, `i18n` + `i18n_table.inc` translations ...)
- `res/` — icon (`filM.ico`; `tools\make_icon.ps1` makes a placeholder or converts your PNG), manifest, version info
- `viewer/` — HTML / JS of the Markdown viewer and bundled libraries (`viewer/lib`)
- `third_party/webview2/` — WebView2 SDK header and loader DLL
- `docs/` — usage guides, `springm_clone_design_memo.md` — design decisions

## Adding a language string
UI strings are written in Japanese in the source as `Tr(L"日本語")` (or `Fmt(L"{0} 件", {...})`); English is looked up in `src/i18n_table.inc`. A string missing from the table is shown in Japanese. `tests/test_core.cpp` checks that every command label has an English translation.

## License
[MIT](LICENSE). Bundled third-party components keep their own licenses (see below).

## Security
See [SECURITY.md](SECURITY.md) for how documents, links and files are handled and how to report a problem.

## Third-party components
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## 日本語

Windows 11 向けの、キーボード操作中心のファイラーです (C++17 / Win32)。かつてのファイラー *filmtn* と *SpringM* へのリスペクトから、2 つの名前を合わせて **fil**mtn + Spring**M** = **filM** と名付けました。

- 1キー操作 (`C` コピー、`M` 移動、`D` 削除、`N` リネーム ...) と、Shift を押している間 Shift+キーの内容に切り替わる **キー一覧**
- タブ、**タブセット** (仮想デスクトップのようなタブの組)、TAB 補完つきの **エイリアス**、マーク (複数選択)、**元に戻す** (ごみ箱の復元まで)
- ファイル一覧の下の **ビューワー** (`V` で表示/非表示、`Esc` で閉じる。カーソルに追従): 画像 (WIC。JPEG / PNG / GIF / BMP / TIFF、コーデックがあれば HEIC / WebP。EXIF の向きに対応)・Markdown (WebView2 + marked / KaTeX / mermaid)・テキスト (見るだけ。`E` で設定したテキストエディタが開く)
- **設定ダイアログ** (`Ctrl+,`): キー割り当ての編集、テキストエディタ、テーマ (システム / ライト / ダーク)、言語
- 日本語 / 英語の表示切り替え (右下のボタン)
- 設定ファイルは exe と同じフォルダ (持ち運び可能)

使い方の詳細は [docs/USAGE.md](docs/USAGE.md) を、セキュリティの考え方は [SECURITY.md](SECURITY.md) を見てください。ビルド方法は上の Build の節と同じです。Markdown の整形表示には Microsoft Edge WebView2 Runtime (Windows 11 は標準搭載) と、exe の隣の `WebView2Loader.dll`・`viewer\` が要ります (無いときは文字のまま表示します)。

### ライセンス
[MIT](LICENSE) です。同梱の第三者ライブラリはそれぞれのライセンスに従います ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md))。
