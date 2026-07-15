$shadersDir = "$PSScriptRoot\shaders"
$postEffectDir = "$shadersDir\post_effects"
$glslc = $null
if ($env:VULKAN_SDK) {
    $glslc = "$env:VULKAN_SDK\Bin\glslc.exe"
    if (-not (Test-Path $glslc)) { $glslc = $null }
}
if (-not $glslc) {
    $glslc = (Get-Command glslc -ErrorAction SilentlyContinue).Source
}
if (-not $glslc) {
    Write-Host "ERROR: glslc not found" -ForegroundColor Red
    exit 1
}

$spvFiles = Get-ChildItem "$postEffectDir\procedural_*.comp.spv" -ErrorAction SilentlyContinue
foreach ($spv in $spvFiles) {
    $compPath = $spv.FullName -replace '\.spv$', ''
    if (-not (Test-Path $compPath)) {
        Remove-Item $spv.FullName -Force
        Write-Host "Deleted (no source): $($spv.Name)"
        continue
    }
    $tmpOut = [System.IO.Path]::GetTempFileName()
    $tmpErr = [System.IO.Path]::GetTempFileName()
    $proc = Start-Process -FilePath $glslc -ArgumentList @("-I", $shadersDir, "-I", $postEffectDir, $compPath, "-o", $spv.FullName) -NoNewWindow -PassThru -Wait -RedirectStandardOutput $tmpOut -RedirectStandardError $tmpErr
    Remove-Item $tmpOut, $tmpErr -Force -ErrorAction SilentlyContinue
    if ($proc.ExitCode -ne 0) {
        Remove-Item $spv.FullName -Force
        Write-Host "Deleted stale: $($spv.Name)"
    } else {
        Write-Host "OK: $($spv.Name)"
    }
}
