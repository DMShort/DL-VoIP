#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Downloads the latest DadLink server and client releases from GitHub.
.DESCRIPTION
    - Fetches the latest server release, updates the binary, restarts the service
    - Fetches the latest client release, places the zip in the downloads/ folder
    - Updates client_version in server.yaml so connected clients are notified
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

    [string]$RepoName = "DL-VoIP",
    [string]$InstallDir = "C:\DadLink",
    [string]$ServiceName = "DadLinkServer",
    [string]$GitHubToken = ""
)

$ErrorActionPreference = "Stop"

Write-Host "=== DadLink Updater ===" -ForegroundColor Cyan

# 1. Resolve GitHub token
if (-not $GitHubToken) { $GitHubToken = $env:GITHUB_TOKEN }
$headers = @{ "User-Agent" = "DadLink-Updater" }
if ($GitHubToken) {
    $headers["Authorization"] = "token $GitHubToken"
    Write-Host "Using authenticated GitHub access."
}

$dlHeaders = @{ "User-Agent" = "DadLink-Updater"; "Accept" = "application/octet-stream" }
if ($GitHubToken) { $dlHeaders["Authorization"] = "token $GitHubToken" }

# Helper: download a GitHub release asset
function Save-Asset($asset, $outPath) {
    $dlUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/releases/assets/$($asset.id)"
    Invoke-WebRequest -Uri $dlUrl -Headers $dlHeaders -OutFile $outPath -UseBasicParsing
}

# ============================================================
# SERVER UPDATE
# ============================================================
Write-Host ""
Write-Host "--- Server Update ---" -ForegroundColor Yellow

# Get all releases, find latest server release (tag starts with "v")
$releasesUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/releases?per_page=20"
try {
    $releases = Invoke-RestMethod -Uri $releasesUrl -Headers $headers
} catch {
    Write-Host "ERROR: Could not fetch releases. Check repo owner/name." -ForegroundColor Red
    exit 1
}

$serverRelease = $releases | Where-Object { $_.tag_name -like "v*" -and $_.tag_name -notlike "client-*" } | Select-Object -First 1
$serverUpdated = $false

if ($serverRelease) {
    $serverTag = $serverRelease.tag_name
    $serverAsset = $serverRelease.assets | Where-Object { $_.name -like "voip-server-*.zip" } | Select-Object -First 1

    if (-not $serverAsset) {
        Write-Host "No server zip in release $serverTag" -ForegroundColor Gray
    } else {
        # Check if already up to date
        $versionFile = Join-Path $InstallDir "version.txt"
        $currentVersion = ""
        if (Test-Path $versionFile) { $currentVersion = (Get-Content $versionFile -Raw).Trim() }

        if ($currentVersion -eq $serverTag) {
            Write-Host "Server already up to date ($serverTag)." -ForegroundColor Green
        } else {
            Write-Host "Updating server: $currentVersion -> $serverTag"
            Write-Host "Downloading $($serverAsset.name)..."

            $tempDir = Join-Path $env:TEMP "dadlink-server-update"
            if (Test-Path $tempDir) { Remove-Item $tempDir -Recurse -Force }
            New-Item -ItemType Directory -Path $tempDir | Out-Null

            $zipPath = Join-Path $tempDir $serverAsset.name
            Save-Asset $serverAsset $zipPath

            Write-Host "Extracting..."
            Expand-Archive -Path $zipPath -DestinationPath $tempDir -Force

            # Stop service
            $serviceExists = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
            if ($serviceExists -and $serviceExists.Status -eq "Running") {
                Write-Host "Stopping $ServiceName service..."
                Stop-Service -Name $ServiceName -Force
                Start-Sleep -Seconds 2
            }

            # Replace binary
            if (-not (Test-Path $InstallDir)) {
                New-Item -ItemType Directory -Path $InstallDir | Out-Null
            }
            Copy-Item (Join-Path $tempDir "voip-server.exe") $InstallDir -Force

            # Copy config only if it doesn't exist yet
            $configDir = Join-Path $InstallDir "config"
            if (-not (Test-Path (Join-Path $configDir "server.yaml"))) {
                $configSrc = Join-Path $tempDir "server.yaml"
                if (Test-Path $configSrc) {
                    if (-not (Test-Path $configDir)) { New-Item -ItemType Directory -Path $configDir | Out-Null }
                    Copy-Item $configSrc $configDir
                    Write-Host "Default config copied." -ForegroundColor Yellow
                }
            }

            # Save version
            $serverTag | Out-File $versionFile -Encoding utf8 -NoNewline

            # Start service
            if ($serviceExists) {
                Write-Host "Starting $ServiceName service..."
                Start-Service -Name $ServiceName
                Start-Sleep -Seconds 1
                $svc = Get-Service -Name $ServiceName
                if ($svc.Status -eq "Running") {
                    Write-Host "Server updated and running ($serverTag)." -ForegroundColor Green
                } else {
                    Write-Host "WARNING: Service status is $($svc.Status)" -ForegroundColor Yellow
                }
            }

            Remove-Item $tempDir -Recurse -Force
            $serverUpdated = $true
        }
    }
} else {
    Write-Host "No server releases found." -ForegroundColor Gray
}

# ============================================================
# CLIENT UPDATE
# ============================================================
Write-Host ""
Write-Host "--- Client Update ---" -ForegroundColor Yellow

$clientRelease = $releases | Where-Object { $_.tag_name -like "client-v*" } | Select-Object -First 1

if ($clientRelease) {
    $clientTag = $clientRelease.tag_name
    $clientAsset = $clientRelease.assets | Where-Object { $_.name -like "DadLink-*.zip" } | Select-Object -First 1

    if (-not $clientAsset) {
        Write-Host "No client zip in release $clientTag" -ForegroundColor Gray
    } else {
        # Check if already up to date
        $clientVersionFile = Join-Path $InstallDir "client-version.txt"
        $currentClientVersion = ""
        if (Test-Path $clientVersionFile) { $currentClientVersion = (Get-Content $clientVersionFile -Raw).Trim() }

        if ($currentClientVersion -eq $clientTag) {
            Write-Host "Client already up to date ($clientTag)." -ForegroundColor Green
        } else {
            Write-Host "Updating client: $currentClientVersion -> $clientTag"

            # Ensure downloads directory exists
            $downloadDir = Join-Path $InstallDir "downloads"
            if (-not (Test-Path $downloadDir)) {
                New-Item -ItemType Directory -Path $downloadDir | Out-Null
            }

            # Remove old client zips
            Get-ChildItem $downloadDir -Filter "DadLink-*.zip" | Remove-Item -Force

            # Download new client zip directly to downloads folder
            $clientZipPath = Join-Path $downloadDir $clientAsset.name
            Write-Host "Downloading $($clientAsset.name)..."
            Save-Asset $clientAsset $clientZipPath

            # Extract version from tag (client-v2.0.1-abc1234 -> 2.0.1)
            $clientVersion = $clientTag -replace '^client-v', '' -replace '-[a-f0-9]+$', ''

            # Update client_version in server.yaml
            $configPath = Join-Path $InstallDir "config\server.yaml"
            if (Test-Path $configPath) {
                $yaml = Get-Content $configPath -Raw
                if ($yaml -match 'client_version:\s*"[^"]*"') {
                    $yaml = $yaml -replace 'client_version:\s*"[^"]*"', "client_version: `"$clientVersion`""
                } elseif ($yaml -match 'client_version:') {
                    $yaml = $yaml -replace 'client_version:.*', "client_version: `"$clientVersion`""
                }
                Set-Content $configPath $yaml -NoNewline
                Write-Host "Updated client_version in server.yaml to $clientVersion"

                # Restart server to pick up new config (if not already restarted)
                if (-not $serverUpdated) {
                    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
                    if ($svc -and $svc.Status -eq "Running") {
                        Write-Host "Restarting $ServiceName to apply new client version..."
                        Restart-Service -Name $ServiceName
                    }
                }
            }

            # Save client version
            $clientTag | Out-File $clientVersionFile -Encoding utf8 -NoNewline
            Write-Host "Client updated ($clientTag). Zip available at: $clientZipPath" -ForegroundColor Green
        }
    }
} else {
    Write-Host "No client releases found." -ForegroundColor Gray
}

Write-Host ""
Write-Host "=== Update check complete ===" -ForegroundColor Cyan
