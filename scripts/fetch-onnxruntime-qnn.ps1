param(
    [string] $Version = "1.24.4",
    [string] $Destination = "runtime/npu"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$destinationPath = Join-Path $repoRoot $Destination
$downloadDir = Join-Path $repoRoot "build\nuget-cache"
$packageName = "Microsoft.ML.OnnxRuntime.QNN"
$packageId = "microsoft.ml.onnxruntime.qnn"
$packageFile = "$packageId.$Version.nupkg"
$packageUrl = "https://api.nuget.org/v3-flatcontainer/$packageId/$Version/$packageFile"
$packagePath = Join-Path $downloadDir $packageFile
$zipPath = Join-Path $downloadDir "$packageId.$Version.zip"
$extractPath = Join-Path $downloadDir "$packageName.$Version"
$nativePath = Join-Path $extractPath "runtimes\win-arm64\native"

Write-Host "Repo root: $repoRoot"
Write-Host "Destination: $destinationPath"
Write-Host "Package URL: $packageUrl"

New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null

if (-not (Test-Path $packagePath)) {
    Write-Host "Downloading package..."
    Invoke-WebRequest -Uri $packageUrl -OutFile $packagePath
} else {
    Write-Host "Using cached package: $packagePath"
}

if (Test-Path $extractPath) {
    Remove-Item -Recurse -Force $extractPath
}

Copy-Item -LiteralPath $packagePath -Destination $zipPath -Force
Expand-Archive -LiteralPath $zipPath -DestinationPath $extractPath -Force

if (-not (Test-Path $nativePath)) {
    throw "Native ARM64 payload not found at $nativePath"
}

Copy-Item -Path (Join-Path $nativePath "*") -Destination $destinationPath -Force

Write-Host "Staged files:"
Get-ChildItem -File $destinationPath | Select-Object Name, Length
