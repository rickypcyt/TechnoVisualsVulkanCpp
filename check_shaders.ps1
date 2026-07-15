$shaders = Get-ChildItem "$PSScriptRoot\shaders\*.comp"
foreach ($s in $shaders) {
    $spvPath = $s.FullName + ".spv"
    if (Test-Path $spvPath) {
        $spvItem = Get-Item $spvPath
        if ($s.LastWriteTime -gt $spvItem.LastWriteTime) {
            Write-Host "STALE: $($s.Name) (src: $($s.LastWriteTime), spv: $($spvItem.LastWriteTime))"
        } else {
            Write-Host "OK: $($s.Name)"
        }
    } else {
        Write-Host "MISSING: $($s.Name)"
    }
}
