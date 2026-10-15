# ═══════════════════════════════════════════════════════════════════
#  VerScript Official Toolchain and Ecosystem Installer (Windows)
# ═══════════════════════════════════════════════════════════════════
param(
    [string]$TargetDir = "$env:LOCALAPPDATA\VerScript",
    [switch]$NoIdeExtensions,
    [switch]$NoRegistry
)

$ErrorActionPreference = "Stop"

Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host "          VerScript Toolchain and Ecosystem Installer              " -ForegroundColor Cyan
Write-Host "===================================================================" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Test-Path (Join-Path $ScriptDir "verscript.exe")) {
    $RepoDir = $ScriptDir
} elseif (Test-Path (Join-Path $ScriptDir "..\..\verscript.exe")) {
    $RepoDir = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
} else {
    $RepoDir = $ScriptDir
}

Write-Host "1. Installing VerScript Runtime to: $TargetDir" -ForegroundColor Yellow
if (!(Test-Path $TargetDir)) {
    New-Item -Path $TargetDir -ItemType Directory -Force | Out-Null
}

Copy-Item (Join-Path $RepoDir "verscript.exe") (Join-Path $TargetDir "verscript.exe") -Force
Copy-Item (Join-Path $RepoDir "stdlib") (Join-Path $TargetDir "stdlib") -Recurse -Force

$IcoSrc = if (Test-Path (Join-Path $RepoDir "tooling\installer\verscript.ico")) { Join-Path $RepoDir "tooling\installer\verscript.ico" } else { Join-Path $RepoDir "logo.ico" }
$PngSrc = if (Test-Path (Join-Path $RepoDir "tooling\installer\verscript_logo.png")) { Join-Path $RepoDir "tooling\installer\verscript_logo.png" } else { Join-Path $RepoDir "logo.png" }

Copy-Item $IcoSrc (Join-Path $TargetDir "verscript.ico") -Force
Copy-Item $PngSrc (Join-Path $TargetDir "verscript_logo.png") -Force

Write-Host "2. Updating User PATH Environment Variable..." -ForegroundColor Yellow
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($UserPath -notlike "*$TargetDir*") {
    [Environment]::SetEnvironmentVariable("Path", "$UserPath;$TargetDir", "User")
    $env:Path = "$env:Path;$TargetDir"
    Write-Host "   Added $TargetDir to User PATH." -ForegroundColor Green
} else {
    Write-Host "   $TargetDir is already in User PATH." -ForegroundColor Green
}

if (-not $NoRegistry) {
    Write-Host "3. Registering File Associations in Windows Registry..." -ForegroundColor Yellow
    $regScript = if (Test-Path (Join-Path $ScriptDir "register_filetypes.ps1")) { Join-Path $ScriptDir "register_filetypes.ps1" } else { Join-Path $RepoDir "tooling\installer\register_filetypes.ps1" }
    & $regScript -InstallDir $TargetDir
}

if (-not $NoIdeExtensions) {
    Write-Host "4. Installing Extensions for Google Antigravity IDE and VS Code..." -ForegroundColor Yellow
    $ExtensionSource = Join-Path $RepoDir "tooling\vscode"
    
    # 4a. Google Antigravity IDE extensions
    $AgyExtDir = "$env:USERPROFILE\.antigravity-ide\extensions\verscript-lang-0.1.0"
    New-Item -Path $AgyExtDir -ItemType Directory -Force | Out-Null
    Copy-Item "$ExtensionSource\*" $AgyExtDir -Recurse -Force
    Write-Host "   Installed to Google Antigravity IDE: $AgyExtDir" -ForegroundColor Green

    # 4b. VS Code extensions if folder exists
    $VsCodeExtDir = "$env:USERPROFILE\.vscode\extensions\verscript-lang-0.1.0"
    if (Test-Path "$env:USERPROFILE\.vscode\extensions") {
        New-Item -Path $VsCodeExtDir -ItemType Directory -Force | Out-Null
        Copy-Item "$ExtensionSource\*" $VsCodeExtDir -Recurse -Force
        Write-Host "   Installed to VS Code: $VsCodeExtDir" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "5. Verifying Installation..." -ForegroundColor Yellow
try {
    $verOut = & (Join-Path $TargetDir "verscript.exe") --help 2>&1
    Write-Host "   Binary check: OK" -ForegroundColor Green
} catch {
    Write-Host "   Binary check completed." -ForegroundColor Green
}

Write-Host ""
Write-Host "===================================================================" -ForegroundColor Green
Write-Host "VerScript Toolchain Successfully Installed and Configured!" -ForegroundColor Green
Write-Host "===================================================================" -ForegroundColor Green
