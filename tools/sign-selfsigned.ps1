# filM.exe を自己署名証明書で署名する (SmartScreen / Smart App Control 対策の入口)。
#
# 注意:
# - 自己署名のままでは、他の PC では「発行元不明」のままです。署名の効果が出るのは、
#   この証明書を信頼済みルート/信頼された発行元に登録した PC (= 自分の PC) だけです。
#   一般公開する場合は、コード署名証明書 (EV/OV) を購入して署名してください。
# - -TrustOnThisPc を付けると、証明書を現在のユーザーの「信頼されたルート証明機関」と
#   「信頼された発行元」に登録します (ユーザー単位。管理者権限は不要ですが、確認ダイアログが出ます)。
#   付けなければ署名だけ行います。
#
# 使い方: .\tools\sign-selfsigned.ps1 [-Exe bin\filM.exe] [-TrustOnThisPc]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\bin\filM.exe'),
    [switch]$TrustOnThisPc
)
$ErrorActionPreference = 'Stop'
$subject = 'CN=SpringM Clone Self-Signed'

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation Cert:\CurrentUser\My `
        -KeyUsage DigitalSignature -NotAfter (Get-Date).AddYears(3)
    Write-Host "証明書を作成しました: $($cert.Thumbprint)"
}

if ($TrustOnThisPc) {
    foreach ($storeName in 'Root', 'TrustedPublisher') {
        $store = New-Object System.Security.Cryptography.X509Certificates.X509Store($storeName, 'CurrentUser')
        $store.Open('ReadWrite')
        $store.Add($cert)
        $store.Close()
    }
    Write-Host '証明書を現在のユーザーの信頼ストアに登録しました。'
}

$result = Set-AuthenticodeSignature -FilePath $Exe -Certificate $cert -HashAlgorithm SHA256 `
    -TimestampServer 'http://timestamp.digicert.com'
Write-Host "署名結果: $($result.Status) ($Exe)"
