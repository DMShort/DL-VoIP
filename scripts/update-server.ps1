#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Downloads the latest DadLink server release from GitHub and restarts the service.
.DESCRIPTION
    - Fetches the latest release from your GitHub repo
    - Downloads and extracts the server binary
    - Stops the NSSM service, replaces the binary, restarts it
.PARAMETER RepoOwner
    GitHub username or org (e.g. "yourusername")
.PARAMETER RepoName
    GitHub repo name (e.g. "Dad-Link-V2")
.PARAMETER InstallDir
    Where the server binary lives (default: C:\DadLink)
.PARAMETER ServiceName
    NSSM service name (default: DadLinkServer)
.PARAMETER GitHubToken
    GitHub personal access token (required for private repos). Can also be set via GITHUB_TOKEN env var.
#>
param(
    [Parameter(Mandatory=$true)]
    [string]$RepoOwner,

    [string]$RepoName = "Dad-Link-V2",
    [string]$InstallDir = "C:\DadLink",
    [string]$ServiceName = "DadLinkServer",
    [string]$GitHubToken = ""
)

$ErrorActionPreference = "Stop"

Write-Host "=== DadLink Server Updater ===" -ForegroundColor Cyan

# 1. Resolve GitHub token
if (-not $GitHubToken) { $GitHubToken = $env:GITHUB_TOKEN }
$headers = @{ "User-Agent" = "DadLink-Updater" }
if ($GitHubToken) {
    $headers["Authorization"] = "token $GitHubToken"
    Write-Host "Using authenticated GitHub access."
}

# 2. Get latest release info
Write-Host "Fetching latest release..."
$apiUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/releases/latest"
try {
    $release = Invoke-RestMethod -Uri $apiUrl -Headers $headers
} catch {
    Write-Host "ERROR: Could not fetch release info. Check repo owner/name and that releases exist." -ForegroundColor Red
    exit 1
}

$tag = $release.tag_name
$asset = $release.assets | Where-Object { $_.name -like "voip-server-v*.zip" } | Select-Object -First 1
# Fallback to old naming convention
if (-not $asset) {
    $asset = $release.assets | Where-Object { $_.name -like "voip-server-*.zip" } | Select-Object -First 1
}

if (-not $asset) {
    Write-Host "ERROR: No server zip found in release $tag" -ForegroundColor Red
    exit 1
}

Write-Host "Latest release: $tag"
Write-Host "Asset: $($asset.name)"

# 2. Check if already up to date
$versionFile = Join-Path $InstallDir "version.txt"
if (Test-Path $versionFile) {
    $currentVersion = Get-Content $versionFile -Raw
    if ($currentVersion.Trim() -eq $tag) {
        Write-Host "Already up to date ($tag). Nothing to do." -ForegroundColor Green
        exit 0
    }
}

# 3. Download
$tempDir = Join-Path $env:TEMP "dadlink-update"
if (Test-Path $tempDir) { Remove-Item $tempDir -Recurse -Force }
New-Item -ItemType Directory -Path $tempDir | Out-Null

$zipPath = Join-Path $tempDir $asset.name
Write-Host "Downloading $($asset.name)..."
$dlHeaders = @{ "User-Agent" = "DadLink-Updater"; "Accept" = "application/octet-stream" }
if ($GitHubToken) { $dlHeaders["Authorization"] = "token $GitHubToken" }
$dlUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/releases/assets/$($asset.id)"
Invoke-WebRequest -Uri $dlUrl -Headers $dlHeaders -OutFile $zipPath -UseBasicParsing

# 4. Extract
Write-Host "Extracting..."
Expand-Archive -Path $zipPath -DestinationPath $tempDir -Force

# 5. Stop service
$serviceExists = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($serviceExists -and $serviceExists.Status -eq "Running") {
    Write-Host "Stopping $ServiceName service..."
    Stop-Service -Name $ServiceName -Force
    Start-Sleep -Seconds 2
}

# 6. Replace binary
Write-Host "Updating binary..."
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir | Out-Null
}

Copy-Item (Join-Path $tempDir "voip-server.exe") $InstallDir -Force

# Copy config only if it doesn't exist yet (don't overwrite user config)
$configDest = Join-Path $InstallDir "server.yaml"
if (-not (Test-Path $configDest)) {
    $configSrc = Join-Path $tempDir "server.yaml"
    if (Test-Path $configSrc) {
        Copy-Item $configSrc $InstallDir
        Write-Host "Default config copied. Edit $configDest before starting." -ForegroundColor Yellow
    }
}

# 7. Save version
$tag | Out-File $versionFile -Encoding utf8 -NoNewline

# 8. Start service
if ($serviceExists) {
    Write-Host "Starting $ServiceName service..."
    Start-Service -Name $ServiceName
    Start-Sleep -Seconds 1
    $svc = Get-Service -Name $ServiceName
    if ($svc.Status -eq "Running") {
        Write-Host "Service is running." -ForegroundColor Green
    } else {
        Write-Host "WARNING: Service status is $($svc.Status)" -ForegroundColor Yellow
    }
} else {
    Write-Host "Service '$ServiceName' not found. Start the server manually or install it with NSSM:" -ForegroundColor Yellow
    Write-Host "  nssm install $ServiceName $InstallDir\voip-server.exe" -ForegroundColor Gray
}

# 9. Cleanup
Remove-Item $tempDir -Recurse -Force
Write-Host "Update complete: $tag" -ForegroundColor Green
