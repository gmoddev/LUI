# Synthetic public test credentials. Never use these keys for production.
param([string]$Root = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $Root -Force | Out-Null
$AuthorityKey = [System.Security.Cryptography.RSA]::Create(2048)
$AuthorityRequest = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
    'CN=LUI Test Authority', $AuthorityKey, [System.Security.Cryptography.HashAlgorithmName]::SHA256,
    [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
$AuthorityRequest.CertificateExtensions.Add([System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($true,$true,0,$true))
$AuthorityRequest.CertificateExtensions.Add([System.Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
    [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyCertSign, $true))
$Authority = $AuthorityRequest.CreateSelfSigned([DateTimeOffset]'2025-01-01Z', [DateTimeOffset]'2036-01-01Z')
[IO.File]::WriteAllText((Join-Path $Root 'Root.pem'), $Authority.ExportCertificatePem() + "`n", [Text.UTF8Encoding]::new($false))
foreach ($Name in @('Valid','WrongName','Expired')) {
    $Key = [System.Security.Cryptography.RSA]::Create(2048)
    $Request = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=LUI Test Server', $Key, [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $Request.CertificateExtensions.Add([System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false,$false,0,$true))
    $Request.CertificateExtensions.Add([System.Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
        ([System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature -bor
         [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment), $true))
    $Uses = [System.Security.Cryptography.OidCollection]::new()
    $Uses.Add([System.Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1')) | Out-Null
    $Request.CertificateExtensions.Add([System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($Uses,$true))
    $Names = [System.Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    if ($Name -eq 'WrongName') { $Names.AddDnsName('wrong.example.test') }
    else {
        $Names.AddDnsName('localhost')
        $Names.AddIpAddress([Net.IPAddress]::Parse('127.0.0.1'))
        $Names.AddIpAddress([Net.IPAddress]::Parse('::1'))
    }
    $Request.CertificateExtensions.Add($Names.Build())
    $Serial = [byte[]]::new(16)
    [System.Security.Cryptography.RandomNumberGenerator]::Fill($Serial)
    $Expiry = if ($Name -eq 'Expired') { [DateTimeOffset]'2026-01-01Z' } else { [DateTimeOffset]'2035-01-01Z' }
    $Certificate = $Request.Create($Authority, [DateTimeOffset]'2025-01-02Z', $Expiry, $Serial)
    $WithKey = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::CopyWithPrivateKey($Certificate, $Key)
    $Bytes = $WithKey.ExportPkcs12([System.Security.Cryptography.X509Certificates.Pkcs12ExportPbeParameters]::Pbes2Aes256Sha256, 'test-only')
    [IO.File]::WriteAllBytes((Join-Path $Root "$Name.p12"), $Bytes)
    $WithKey.Dispose()
    $Certificate.Dispose()
    $Key.Dispose()
}
$Authority.Dispose()
$AuthorityKey.Dispose()
Write-Host '[LUI:TlsTest] Synthetic fixtures generated; no certificate store was modified'
