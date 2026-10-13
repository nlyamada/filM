# Security / セキュリティ

**English** | [日本語](#日本語)

filM opens and previews files that may come from untrusted places (downloads, zip files, cloned repositories). This page describes what filM does and does not do about that.

## What filM does not do
- No network access of its own: no telemetry, no update check, no listening port or named pipe. Command-line arguments are ignored.
- It only reads and writes the files you ask it to, plus its own config files (`keymap.json`, `favorites.json`, `session.json`, `settings.json`, next to the exe) and, for Markdown, the WebView2 cache in `%LOCALAPPDATA%\filM\WebView2`.

## Markdown preview (WebView2)
- A Content-Security-Policy in `viewer\viewer.html` forbids all script except the bundled viewer scripts, and allows images only from the same drive (and `data:`). **Images from the Internet are not loaded by default**, so a document cannot track you or leak your IP address by an image; an opt-in setting ("Show images from the Internet in Markdown") loads them from a second page whose policy also allows `https:` images.
- HTML written in the document is cleaned before display (script, iframe, object, embed, form, link, meta, base, style and `on...` attributes are removed). Mermaid runs in `strict` mode; KaTeX does not trust input.
- Navigation: only links you click are followed (a page cannot open the browser or a file by itself). `http(s)` / `mailto:` links go to your default browser. Links to local files: programs and scripts (`.exe`, `.bat`, `.ps1`, `.js`, `.lnk`, ...) are never opened from a link, UNC paths and `name:stream` paths are refused, and every type other than images / text / PDF / audio / video asks first.
- WebView2 is configured without dev tools, context menu, host objects or web messaging. The WebView2 Runtime itself is Microsoft's and is updated by Windows.

## Other previews and the editor
- Images are decoded by the Windows Imaging Component with size limits (very large images are refused or shrunk while loading).
- Text is shown read-only in a plain-text RichEdit (RTF is **not** interpreted, no OLE objects, no automatic link detection). Files over 10 MB, and binary files, are not opened. filM does not write to the files it previews.

## Programs filM starts
- PowerShell, `cmd`, Notepad and `tar.exe` are started by full path under `System32`, not through the search path. The current folder is removed from the DLL search path, `WebView2Loader.dll` is loaded by full path from the exe's folder, and RichEdit and uxtheme come from `System32`.
- Zip / unzip use Windows' `tar.exe`. It refuses absolute paths and `..` in archives. Extraction goes to a temporary folder first and existing files are never overwritten (a number is added). File names starting with `@` or `-` and paths ending in `\` are passed safely.
- `Enter` on a file runs it with the Windows shell (like Explorer). That is by design; the shell context menu (right click) loads the shell extensions installed on your PC.

## Things to know
- The exe is **not code-signed**; Windows SmartScreen may warn. Download releases from the official repository only and compare the SHA-256 checksum if one is published.
- Config files next to the exe can be edited by anyone who can write to that folder (for example the `editor` path in `settings.json` is started by `E`). Keep filM in a folder only you can write to.
- Bundled libraries (see `THIRD_PARTY_NOTICES.md`): marked 12.0.2, KaTeX 0.16.11, mermaid 10.9.1, WebView2 SDK 1.0.3485.44. They are updated when a security advisory affects how filM uses them.
- The "Browse..." button in Settings uses the Windows file dialog, which does not follow the dark theme.

## Reporting a problem
Please report security problems privately (GitHub: Security → Report a vulnerability) rather than in a public issue.

---

## 日本語

filM は、ダウンロードしたファイル・zip・クローンしたリポジトリなど、信頼できない場所から来たファイルを開いたりプレビューしたりします。そのときの考え方をまとめます。

### やらないこと
- 自分からはネットワークに接続しません (テレメトリなし・更新確認なし・待ち受けポートや名前付きパイプなし)。コマンドライン引数も使いません。
- 読み書きするのは、操作したファイルと、自分の設定ファイル (exe の隣の `keymap.json` `favorites.json` `session.json` `settings.json`)、Markdown 表示用の WebView2 キャッシュ (`%LOCALAPPDATA%\filM\WebView2`) だけです。

### Markdown のプレビュー (WebView2)
- `viewer\viewer.html` の Content-Security-Policy で、同梱の表示用スクリプト以外のスクリプトを禁止し、画像は同じドライブ内 (と `data:`) だけに限っています。**インターネット上の画像は既定では読み込まない**ので、画像による追跡や IP アドレスの漏れは起きません (設定で「Markdown でインターネット上の画像も表示する」をオンにすると、https の画像も許可する別のページで表示します)。
- 文書内の HTML は、表示前に危険なもの (script・iframe・object・embed・form・link・meta・base・style・`on...` 属性) を取り除きます。mermaid は strict モード、KaTeX は入力を信用しない設定です。
- リンク: クリックしたものだけ開きます (ページが勝手にブラウザやファイルを開くことはできません)。`http(s)` / `mailto:` は既定のブラウザへ。ローカルファイルへのリンクは、プログラムやスクリプト (`.exe` `.bat` `.ps1` `.js` `.lnk` など) は開かず、UNC パスや `名前:ストリーム` 形式は拒否し、画像・テキスト・PDF・音声・動画以外は確認してから開きます。
- WebView2 は、開発者ツール・右クリックメニュー・ホストオブジェクト・Web メッセージなしで使います。WebView2 ランタイム自体は Microsoft のもので、Windows が更新します。

### その他のプレビューとエディタ
- 画像は Windows Imaging Component で読み込み、大きさに上限があります (巨大な画像は断るか、読み込みながら縮小します)。
- テキストはプレーンテキストの RichEdit で読み取り専用に表示します (RTF は解釈せず、OLE オブジェクトも URL の自動検出もありません)。10 MB を超えるファイルとバイナリは開きません。プレビューしたファイルへは書き込みません。

### filM が起動するプログラム
- PowerShell・`cmd`・メモ帳・`tar.exe` は、検索パスではなく `System32` 以下のフルパスで起動します。DLL の検索先からカレントフォルダを外し、`WebView2Loader.dll` は exe のフォルダからフルパスで、RichEdit と uxtheme は `System32` から読み込みます。
- zip の圧縮・解凍は Windows の `tar.exe` を使います。アーカイブ内の絶対パスと `..` は tar が拒否します。展開はいったん作業用フォルダへ行い、既存のファイルは上書きしません (番号を付けます)。`@` や `-` で始まる名前、末尾が `\` のパスも安全に渡します。
- ファイルでの `Enter` は Windows のシェルで実行します (エクスプローラーと同じ)。これは仕様です。右クリックメニューには、PC に入っているシェル拡張が読み込まれます。

### 知っておいてほしいこと
- exe は **コード署名していません**。Windows SmartScreen の警告が出ることがあります。入手は公式のリポジトリからにして、SHA-256 が公開されていれば照合してください。
- exe の隣の設定ファイルは、そのフォルダに書き込める人なら誰でも編集できます (たとえば `settings.json` の `editor` のパスは `E` で起動されます)。filM は自分だけが書き込めるフォルダに置いてください。
- 同梱ライブラリ (`THIRD_PARTY_NOTICES.md`): marked 12.0.2、KaTeX 0.16.11、mermaid 10.9.1、WebView2 SDK 1.0.3485.44。filM の使い方に影響する脆弱性が出たら更新します。
- 設定の「参照...」は Windows 標準のファイル選択ダイアログで、ダークテーマには従いません。

### 問題の報告
セキュリティの問題は、公開の issue ではなく、非公開で (GitHub の Security → Report a vulnerability) 報告してください。
