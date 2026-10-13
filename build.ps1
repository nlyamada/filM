# MSYS2 (MinGW-w64) でビルドする。使い方: .\build.ps1 [clean]
param([string]$Target = "")
$bash = "C:\msys64\usr\bin\bash.exe"
$dir = ($PSScriptRoot -replace '\\', '/') -replace '^([A-Za-z]):', '/$1'
& $bash -lc "export PATH=/mingw64/bin:`$PATH; cd '$dir' && mingw32-make $Target"
exit $LASTEXITCODE
