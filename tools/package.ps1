# ビルドして、配布用の zip (dist\filM-<日付>.zip) を作る。 使い方: .\tools\package.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

& "$root\build.ps1"
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$stage = Join-Path $root "dist\filM"
if (Test-Path $stage) { [IO.Directory]::Delete($stage, $true) }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item bin\filM.exe $stage
Copy-Item bin\WebView2Loader.dll $stage
Copy-Item bin\viewer (Join-Path $stage "viewer") -Recurse
New-Item -ItemType Directory -Force (Join-Path $stage "docs") | Out-Null
Copy-Item docs\USAGE.md, docs\USAGE.en.md, docs\WINDOWS11_NOTES.md (Join-Path $stage "docs")
Copy-Item README.md, THIRD_PARTY_NOTICES.md, SECURITY.md $stage
Copy-Item third_party\webview2\LICENSE.txt (Join-Path $stage "LICENSE-WebView2.txt")

$zip = Join-Path $root ("dist\filM-" + (Get-Date -Format "yyyyMMdd") + ".zip")
if (Test-Path $zip) { [IO.File]::Delete($zip) }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
# 項目名の区切りは "/" にする (CreateFromDirectory は "\" で書くため、Explorer 以外の展開ツールで崩れることがある)。
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    $prefix = (Resolve-Path $stage).Path.TrimEnd('\') + '\'
    Get-ChildItem $stage -Recurse -File | ForEach-Object {
        $name = $_.FullName.Substring($prefix.Length).Replace('\', '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $name, [IO.Compression.CompressionLevel]::Optimal)
    }
} finally { $archive.Dispose() }
Write-Host "created $zip"
