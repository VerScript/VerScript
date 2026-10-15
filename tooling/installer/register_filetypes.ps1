# ═══════════════════════════════════════════════════════════════════
#  VerScript File Associations & Windows Registry Setup
# ═══════════════════════════════════════════════════════════════════
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\VerScript"
)

$ErrorActionPreference = "Stop"

Write-Host "Registering VerScript File Associations (.vrs, .lib.vrs)..." -ForegroundColor Cyan

$ExePath = Join-Path $InstallDir "verscript.exe"
$IcoPath = Join-Path $InstallDir "verscript.ico"

# 1. Register .vrs extension
New-Item -Path "HKCU:\Software\Classes\.vrs" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\.vrs" -Name "(Default)" -Value "VerScript.File"
Set-ItemProperty -Path "HKCU:\Software\Classes\.vrs" -Name "Content Type" -Value "text/x-verscript"
Set-ItemProperty -Path "HKCU:\Software\Classes\.vrs" -Name "PerceivedType" -Value "text"

# 2. Register .lib.vrs extension
New-Item -Path "HKCU:\Software\Classes\.lib.vrs" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\.lib.vrs" -Name "(Default)" -Value "VerScript.Library"
Set-ItemProperty -Path "HKCU:\Software\Classes\.lib.vrs" -Name "Content Type" -Value "text/x-verscript"
Set-ItemProperty -Path "HKCU:\Software\Classes\.lib.vrs" -Name "PerceivedType" -Value "text"

# 3. Register ProgID VerScript.File
New-Item -Path "HKCU:\Software\Classes\VerScript.File" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\VerScript.File" -Name "(Default)" -Value "VerScript Source File"
Set-ItemProperty -Path "HKCU:\Software\Classes\VerScript.File" -Name "FriendlyTypeName" -Value "VerScript Source File"

New-Item -Path "HKCU:\Software\Classes\VerScript.File\DefaultIcon" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\VerScript.File\DefaultIcon" -Name "(Default)" -Value "`"$IcoPath`",0"

New-Item -Path "HKCU:\Software\Classes\VerScript.File\shell\open\command" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\VerScript.File\shell\open\command" -Name "(Default)" -Value "`"$ExePath`" `"%1`" %*"

# 4. Register ProgID VerScript.Library
New-Item -Path "HKCU:\Software\Classes\VerScript.Library" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\VerScript.Library" -Name "(Default)" -Value "VerScript Library File"
Set-ItemProperty -Path "HKCU:\Software\Classes\VerScript.Library" -Name "FriendlyTypeName" -Value "VerScript Library File"

New-Item -Path "HKCU:\Software\Classes\VerScript.Library\DefaultIcon" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\VerScript.Library\DefaultIcon" -Name "(Default)" -Value "`"$IcoPath`",0"

New-Item -Path "HKCU:\Software\Classes\VerScript.Library\shell\open\command" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\VerScript.Library\shell\open\command" -Name "(Default)" -Value "`"$ExePath`" `"%1`" %*"

Write-Host "✅ VerScript file associations successfully registered with icon!" -ForegroundColor Green
