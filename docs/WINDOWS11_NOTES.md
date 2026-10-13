# Windows 11 での運用メモ

設計メモ「保留・将来検討」の項目への対応状況と、利用者向けの注意点。

## 対応済み

| 項目 | 内容 |
|---|---|
| OneDrive プレースホルダー | 一覧の「項目の種類」に ` (クラウド)` を付ける (`FILE_ATTRIBUTE_RECALL_ON_OPEN` / `RECALL_ON_DATA_ACCESS` / `OFFLINE`)。一覧表示ではファイルの中身に触れない (アイコン・種類名は `SHGFI_USEFILEATTRIBUTES` で取得し、ダウンロードを誘発しない)。開く・コピーする操作で初めてダウンロードが走る。 |
| `CFSTR_PREFERREDDROPEFFECT` | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` で Explorer と同じ形式 (`CF_HDROP` + "Preferred DropEffect") のファイルのコピー・切り取り・貼り付け。Explorer でコピー → 本アプリで貼り付け、およびその逆が可能。切り取りの貼り付けは移動になる。 |
| グローバルホットキー | 本アプリは `RegisterHotKey` を使わない。ショートカットはすべてウィンドウ内のアクセラレータ / キー処理のため、`Win+Tab` などの OS 予約キーとは衝突しない (対応不要)。 |
| ロングパス | マニフェストに `longPathAware` を設定済み。**OS 側の「Win32 の長いパスを有効にする」設定 (`HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled`) が 1 のときだけ有効**。0 のままだと 260 文字を超えるパスは開けない (開発 PC では 0 だったため未検証)。有効化は管理者権限で `Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -Value 1`。 |
| ダークモード | OS の「アプリのモード」に追従 (タイトルバー・一覧・パネル・入力欄)。切替にも追従する。タブストリップ (標準のタブコントロール) は OS 標準の配色のまま。 |
| 起動位置 | 保存位置を現在のモニター構成の作業領域内へ補正して復元する。 |

## 利用者側で必要な設定

### Windows Defender「コントロールされたフォルダー アクセス」

有効にしていると、本アプリのファイル操作 (コピー・移動・削除・リネーム) が「未知のアプリ」として
ブロックされることがある。保護対象フォルダー (ドキュメント・ピクチャ等) で操作する場合は、
Windows セキュリティ → ウイルスと脅威の防止 → ランサムウェアの防止 →
「コントロールされたフォルダー アクセス」→「アプリのコントロールされたフォルダー アクセスの許可」に
`springm.exe` を追加する。

### SmartScreen / Smart App Control

- 署名の無い exe は、初回起動時に SmartScreen の警告が出ることがある (「詳細情報」→「実行」)。
- Smart App Control が「オン」の PC では、**署名の無い exe は実行がブロックされうる**。
  - 自分の PC だけで使う場合: `tools\sign-selfsigned.ps1 -TrustOnThisPc` で自己署名して信頼登録する
    (ただし Smart App Control は「信頼された CA 発行の署名」を見るため、自己署名では通らない場合がある。
    その場合は SAC をオフにするか、正規のコード署名証明書で署名する)。
  - 配布する場合: コード署名証明書 (EV / OV) での署名が必要。

## ビルド上の注意

- `Makefile` は `-static -static-libgcc -static-libstdc++` で MinGW ランタイムを exe に取り込む。
  動的リンクのままだと、PATH に MinGW が無い環境で `libstdc++-6.dll` が見つからず
  「システム エラー」ダイアログが出て起動できない。
- ビルドは `.\build.ps1` (MSYS2 の MinGW-w64 を使用)。`.\build.ps1 test` でロジックの単体テスト。
