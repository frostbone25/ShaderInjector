param(
    [Parameter(Mandatory = $true)]
    [string]$TargetPath
)

$ErrorActionPreference = "Stop"

#prefer this shell's modules when the build was launched by another powershell version.
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + [IO.Path]::PathSeparator + $env:PSModulePath

if (-not (Test-Path -LiteralPath $TargetPath -PathType Leaf)) {
    throw "Release artifact does not exist: $TargetPath"
}

function Find-SignTool {
    if ($env:SHADER_INJECTOR_SIGNTOOL_PATH) {
        if (-not (Test-Path -LiteralPath $env:SHADER_INJECTOR_SIGNTOOL_PATH -PathType Leaf)) {
            throw "SHADER_INJECTOR_SIGNTOOL_PATH does not point to a file."
        }

        return $env:SHADER_INJECTOR_SIGNTOOL_PATH
    }

    $windowsKitsBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    $candidate = Get-ChildItem -LiteralPath $windowsKitsBin -Filter signtool.exe -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1

    if (-not $candidate) {
        throw "SignTool was not found. Install the Windows SDK or set SHADER_INJECTOR_SIGNTOOL_PATH."
    }

    return $candidate.FullName
}

$certificateThumbprint = $env:SHADER_INJECTOR_SIGNING_CERT_SHA1
$signingRequired = $env:SHADER_INJECTOR_REQUIRE_SIGNING -eq "1"

if (-not $certificateThumbprint -and $signingRequired) {
    throw "Release signing is required, but SHADER_INJECTOR_SIGNING_CERT_SHA1 is not configured."
}

if ($certificateThumbprint) {
    $signTool = Find-SignTool
    $timestampUrl = if ($env:SHADER_INJECTOR_TIMESTAMP_URL) {
        $env:SHADER_INJECTOR_TIMESTAMP_URL
    } else {
        "https://timestamp.digicert.com"
    }

    Write-Host "Signing $TargetPath"
    & $signTool sign `
        /sha1 $certificateThumbprint `
        /fd SHA256 `
        /tr $timestampUrl `
        /td SHA256 `
        /d "Shader Injector D3D12 Mod" `
        /du "https://github.com/frostbone25/ShaderInjector" `
        $TargetPath

    if ($LASTEXITCODE -ne 0) {
        throw "SignTool failed with exit code $LASTEXITCODE."
    }

    & $signTool verify /pa /v $TargetPath
    if ($LASTEXITCODE -ne 0) {
        throw "Signature verification failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Warning "Signing skipped: SHADER_INJECTOR_SIGNING_CERT_SHA1 is not configured. This build is for local testing only."
}

$signature = Get-AuthenticodeSignature -LiteralPath $TargetPath

if ($certificateThumbprint -and $signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
    throw "Authenticode validation failed after signing: $($signature.Status) $($signature.StatusMessage)"
}

$hash = (Get-FileHash -LiteralPath $TargetPath -Algorithm SHA256).Hash.ToLowerInvariant()
$hashPath = "$TargetPath.sha256"
$hashLine = "$hash  $([IO.Path]::GetFileName($TargetPath))"
[IO.File]::WriteAllText($hashPath, "$hashLine`r`n", [Text.UTF8Encoding]::new($false))
Write-Host "SHA-256: $hash"
Write-Host "Checksum: $hashPath"

$targetFile = Get-Item -LiteralPath $TargetPath
$versionInfo = $targetFile.VersionInfo
$releaseManifestPath = "$TargetPath.release.json"
$releaseManifest = [ordered]@{
    schemaVersion = 1
    fileName = $targetFile.Name
    fileSize = $targetFile.Length
    sha256 = $hash
    productName = $versionInfo.ProductName
    productVersion = $versionInfo.ProductVersion
    fileVersion = $versionInfo.FileVersion
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    authenticode = [ordered]@{
        status = $signature.Status.ToString()
        valid = $signature.Status -eq [System.Management.Automation.SignatureStatus]::Valid
        subject = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { $null }
        thumbprint = if ($signature.SignerCertificate) { $signature.SignerCertificate.Thumbprint } else { $null }
        timestampSubject = if ($signature.TimeStamperCertificate) { $signature.TimeStamperCertificate.Subject } else { $null }
    }
}

$releaseManifestJson = $releaseManifest | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText($releaseManifestPath, "$releaseManifestJson`r`n", [Text.UTF8Encoding]::new($false))
Write-Host "Release manifest: $releaseManifestPath"
Write-Host "Authenticode status: $($signature.Status)"
