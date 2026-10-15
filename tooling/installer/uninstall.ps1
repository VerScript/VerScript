# ═══════════════════════════════════════════════════════════════════
#  VerScript Toolchain Uninstaller (Windows)
# ═══════════════════════════════════════════════════════════════════
param(
    [string]$TargetDir = "$env:LOCALAPPDATA\VerScript"
)

Write-Host "Uninstalling VerScript Toolchain..." -ForegroundColor Yellow

# Remove registry associations
Remove-Item -Path "HKCU:\Software\Classes\.vrs" -Recurse -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\.lib.vrs" -Recurse -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\VerScript.File" -Recurse -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\VerScript.Library" -Recurse -ErrorAction SilentlyContinue

# Remove extensions
Remove-Item -Path "$env:USERPROFILE\.antigravity-ide\extensions\verscript-lang-0.1.0" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$env:USERPROFILE\.vscode\extensions\verscript-lang-0.1.0" -Recurse -Force -ErrorAction SilentlyContinue

# Remove from PATH
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
$CleanPath = ($UserPath -split ';' | Where-Object { $_ -ne $TargetDir -and $_ -ne "" }) -join ';'
[Environment]::SetEnvironmentVariable("Path", $CleanPath, "User")

# Remove install directory
Remove-Item -Path $TargetDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "✅ VerScript Toolchain completely uninstalled." -ForegroundColor Green
