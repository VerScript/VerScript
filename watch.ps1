$lastWrite = (Get-Date).AddSeconds(-1)

while ($true) {
    $files = @()
    if (Test-Path "src") { $files += Get-ChildItem -Path "src" -Recurse -File | Where-Object { $_.LastWriteTime -gt $lastWrite } }
    if (Test-Path "include") { $files += Get-ChildItem -Path "include" -Recurse -File | Where-Object { $_.LastWriteTime -gt $lastWrite } }
    if (Test-Path "tests") { $files += Get-ChildItem -Path "tests" -Recurse -File | Where-Object { $_.LastWriteTime -gt $lastWrite } }
    
    $makefile = $null
    if (Test-Path "Makefile") {
        $makefile = Get-Item -Path "Makefile" | Where-Object { $_.LastWriteTime -gt $lastWrite }
    }
    
    if ($files.Count -gt 0 -or $makefile) {
        Clear-Host
        Write-Host "=========================================="
        Write-Host "Changes detected. Rebuilding via WSL..."
        Write-Host "=========================================="
        wsl make
        if ($LASTEXITCODE -eq 0 -or $?) {
            Write-Host "`nRunning tests/test.vrs via WSL..."
            wsl ./verscript tests/test.vrs
        }
        $lastWrite = Get-Date
    }
    Start-Sleep -Seconds 2
}
