# 仮のアイコン (res\filM.ico) を作る。正式なアイコンができたら、res\filM.ico を差し替えるだけでよい
# (16 / 24 / 32 / 48 / 64 / 128 / 256 px を含む .ico を推奨)。
#   使い方: .\tools\make_icon.ps1                 仮のアイコンを作る
#           .\tools\make_icon.ps1 -Png logo.png   正方形の PNG (512px 以上推奨・透過可) から .ico を作る
param([string]$Png = "")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "res\filM.ico"
$sizes = 16, 24, 32, 48, 64, 128, 256

function New-PlaceholderBitmap([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $r = [int]($s * 0.22)
    $rect = New-Object System.Drawing.Rectangle 0, 0, ($s - 1), ($s - 1)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
    $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, ([System.Drawing.Color]::FromArgb(255, 52, 120, 214)), ([System.Drawing.Color]::FromArgb(255, 24, 58, 118)), 90
    $g.FillPath($brush, $path)
    # フォルダのつまみ (上の黄色い帯)
    $tab = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 183, 40))
    $g.FillRectangle($tab, [int]($s * 0.16), [int]($s * 0.17), [int]($s * 0.30), [int]($s * 0.08))
    $g.FillRectangle($tab, [int]($s * 0.16), [int]($s * 0.25), [int]($s * 0.68), [int]($s * 0.05))
    $font = New-Object System.Drawing.Font "Segoe UI", ([single]($s * 0.46)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $textRect = New-Object System.Drawing.RectangleF 0, ([single]($s * 0.20)), $s, ([single]($s * 0.75))
    $g.DrawString("fM", $font, [System.Drawing.Brushes]::White, $textRect, $fmt)
    $g.Dispose()
    return $bmp
}

function Resize-Bitmap($src, [int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($src, 0, 0, $s, $s)
    $g.Dispose()
    return $bmp
}

$source = $null
if ($Png) {
    $source = [System.Drawing.Image]::FromFile((Resolve-Path $Png))
    if ($source.Width -ne $source.Height) { Write-Warning "正方形でない画像です。縦横比が崩れます。" }
}

$frames = @()
foreach ($s in $sizes) {
    $bmp = if ($source) { Resize-Bitmap $source $s } else { New-PlaceholderBitmap $s }
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += , @($s, $ms.ToArray())
    $bmp.Dispose()
}
if ($source) { $source.Dispose() }

$fs = [System.IO.File]::Create($out)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $s = $f[0]; $data = $f[1]
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s }))); $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]0); $bw.Write([byte]0); $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$data.Length); $bw.Write([uint32]$offset)
    $offset += $data.Length
}
foreach ($f in $frames) { $bw.Write([byte[]]$f[1]) }
$bw.Close(); $fs.Close()
Write-Host "created $out ($($frames.Count) sizes)"
