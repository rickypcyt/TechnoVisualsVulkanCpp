$ErrorActionPreference = "Stop"

# Add MSYS2 to PATH for pkg-config, make, and runtime DLLs
$msys64Bin = "C:\msys64\usr\bin"
$msys64UcrtBin = "C:\msys64\ucrt64\bin"
if (Test-Path $msys64UcrtBin) {
    $env:PATH = "$msys64UcrtBin;$env:PATH"
}
if (Test-Path $msys64Bin) {
    $env:PATH += ";$msys64Bin"
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = "$scriptDir\build"
$shadersDir = "$scriptDir\shaders"

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
}

Write-Host "[build_and_run_windows] Detecting glslc..." -ForegroundColor Cyan

$glslcPath = $null
$vulkanSdk = $env:VULKAN_SDK

if (-not [string]::IsNullOrEmpty($vulkanSdk)) {
    $glslcPath = "$vulkanSdk\Bin\glslc.exe"
    if (Test-Path $glslcPath) {
        Write-Host "  [OK] glslc found at: $glslcPath" -ForegroundColor Green
    } else {
        $glslcPath = $null
    }
}

if ($null -eq $glslcPath) {
    try {
        $glslcPath = (Get-Command glslc -ErrorAction SilentlyContinue).Source
        if ($null -ne $glslcPath) {
            Write-Host "  [OK] glslc found in PATH: $glslcPath" -ForegroundColor Green
        }
    } catch {
    }
}

if ($null -eq $glslcPath) {
    Write-Host "  [ERROR] glslc not found. Please install Vulkan SDK and set VULKAN_SDK" -ForegroundColor Red
    exit 1
}

Write-Host "[build_and_run_windows] Compiling shaders..." -ForegroundColor Cyan

$postEffectDir = "$shadersDir\post_effects"

$shaders = @(
    "triangle.vert",
    "triangle.frag",
    "present.vert",
    "present.frag",
    "fullscreen.vert",
    "fullscreen.frag",
    "pass_a_base.frag",
    "pass_b_spatial.frag",
    "pass_c_detail.frag",
    "pass_d_temporal.frag",
    "pass_e_degradation.frag",
    "pass_f_color.frag",
    "pass_g_output.frag"
)

# Per-shader include dependencies (only pass_a_base pulls in procedural code)
$commonGlsl = @(Get-ChildItem -Path "$shadersDir\*.glsl" -ErrorAction SilentlyContinue)
$proceduralGlsl = @(Get-ChildItem -Path "$scriptDir\procedural\*.glsl" -ErrorAction SilentlyContinue)
function Get-ShaderDeps([string]$name) {
    if ($name -eq "pass_a_base.frag") { return $commonGlsl + $proceduralGlsl }
    if ($name -like "pass_*.frag")    { return @(Get-Item "$shadersDir\shared_ubo.glsl" -ErrorAction SilentlyContinue) }
    return @()
}

foreach ($shader in $shaders) {
    $src = "$shadersDir\$shader"
    $out = "$shadersDir\$shader.spv"

    if (-not (Test-Path $src)) {
        Write-Host "  [WARN] shader not found: $src" -ForegroundColor Yellow
        continue
    }

    $needsCompile = $false
    if (-not (Test-Path $out)) {
        $needsCompile = $true
    } else {
        $outTime = (Get-Item $out).LastWriteTime
        if ((Get-Item $src).LastWriteTime -gt $outTime) {
            $needsCompile = $true
        } else {
            foreach ($dep in (Get-ShaderDeps $shader)) {
                if ($dep.LastWriteTime -gt $outTime) { $needsCompile = $true; break }
            }
        }
    }

    if ($needsCompile) {
        Write-Host "  Compiling $shader..." -ForegroundColor Cyan
        & $glslcPath -I $shadersDir $src -o $out
        if ($LASTEXITCODE -eq 0) {
            Write-Host "    [OK] $shader compiled" -ForegroundColor Green
        } else {
            Write-Host "    [ERROR] Failed to compile $shader" -ForegroundColor Red
        }
    } else {
        Write-Host "  [OK] $shader is up-to-date" -ForegroundColor Green
    }
}

# ── Compile post-effect compute shaders ──────────────────────────────────────
Write-Host "[build_and_run_windows] Compiling post-effect compute shaders..." -ForegroundColor Cyan
$postEffectDir = "$shadersDir\post_effects"
if (Test-Path $postEffectDir) {
    # Newest shared include: a .spv older than this must be rebuilt (per file, not all-or-nothing)
    $incFiles = @(Get-ChildItem -Path "$shadersDir\shared_ubo.glsl","$shadersDir\post_common_vulkan.glsl" -ErrorAction SilentlyContinue) +
                @(Get-ChildItem -Path "$postEffectDir\*_common*.glsl" -ErrorAction SilentlyContinue)
    $incNewest = [datetime]::MinValue
    foreach ($inc in $incFiles) { if ($inc.LastWriteTime -gt $incNewest) { $incNewest = $inc.LastWriteTime } }

    $compShaders = Get-ChildItem -Path $postEffectDir -Filter "*.glsl" -ErrorAction SilentlyContinue
    $peOk = 0
    $peSkip = 0
    foreach ($comp in $compShaders) {
        # Skip include files (not standalone shaders)
        if ($comp.Name -match "_common.*\.glsl$") {
            $peSkip++
            continue
        }
        $spvOut = $comp.FullName + ".spv"
        $needsCompile = $false
        if (-not (Test-Path $spvOut)) {
            $needsCompile = $true
        } else {
            $spvTime = (Get-Item $spvOut).LastWriteTime
            if ($comp.LastWriteTime -gt $spvTime -or $incNewest -gt $spvTime) { $needsCompile = $true }
        }
        if ($needsCompile) {
            $tmpOut = [System.IO.Path]::GetTempFileName()
            $tmpErr = [System.IO.Path]::GetTempFileName()
            $proc = Start-Process -FilePath $glslcPath -ArgumentList @("-I", $shadersDir, "-I", $postEffectDir, $comp.FullName, "-o", $spvOut) -NoNewWindow -PassThru -RedirectStandardOutput $tmpOut -RedirectStandardError $tmpErr -Wait
            Remove-Item $tmpOut, $tmpErr -Force -ErrorAction SilentlyContinue
            if ($proc.ExitCode -eq 0) {
                Write-Host "  [OK] $($comp.Name)" -ForegroundColor Green
                $peOk++
            } else {
                Write-Host "  [SKIP] $($comp.Name) (compile errors)" -ForegroundColor Yellow
                if (Test-Path $spvOut) { Remove-Item $spvOut -Force }
                $peSkip++
            }
        }
    }
    Write-Host "  Post-effects: $peOk compiled, $peSkip skipped" -ForegroundColor Cyan
}

Write-Host "[build_and_run_windows] Configuring CMake..." -ForegroundColor Cyan

$vcpkgDir = "$scriptDir\vcpkg"
$cmakeToolchain = ""
$vcpkgTriplet = "x64-windows"

if (Test-Path "$vcpkgDir\scripts\buildsystems\vcpkg.cmake") {
    $cmakeToolchain = "$vcpkgDir\scripts\buildsystems\vcpkg.cmake"
    Write-Host "  [OK] vcpkg detected, using toolchain" -ForegroundColor Green
}

$cmakeGenerator = "MinGW Makefiles"
$useMake = $false

$useNinja = $false
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    $cmakeGenerator = "Ninja"
    $useMake = $true
    $useNinja = $true
    Write-Host "  [OK] Ninja found, using Ninja generator" -ForegroundColor Green
}

try {
    if ($useNinja) { throw "skip" }
    $null = gcc --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [OK] GCC found, using MinGW Makefiles" -ForegroundColor Green
        $cmakeGenerator = "MinGW Makefiles"
        $useMake = $true
    }
} catch {
}

if (-not $useMake) {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        try {
            $vsYear = & $vsWhere -latest -property installationVersion
            if ($vsYear -like "17*") {
                $cmakeGenerator = "Visual Studio 17 2022"
            } elseif ($vsYear -like "16*") {
                $cmakeGenerator = "Visual Studio 16 2019"
            }
            Write-Host "  Using generator: $cmakeGenerator" -ForegroundColor Cyan
        } catch {
            Write-Host "  Using default generator: $cmakeGenerator" -ForegroundColor Cyan
        }
    }
}

# Generator changed? -> drop stale cache once (otherwise CMake errors out)
$cachePath = "$buildDir\CMakeCache.txt"
$needsConfigure = $true
if (Test-Path $cachePath) {
    $cachedGen = (Select-String -Path $cachePath -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$').Matches.Groups[1].Value
    if ($cachedGen -ne $cmakeGenerator) {
        Write-Host "  Generator changed ($cachedGen -> $cmakeGenerator), resetting CMake cache" -ForegroundColor Yellow
        Remove-Item $cachePath -Force
        Remove-Item "$buildDir\CMakeFiles" -Recurse -Force -ErrorAction SilentlyContinue
        # FetchContent sub-builds keep their own generator cache (sources in *-src are kept)
        Remove-Item "$buildDir\_deps\*-subbuild" -Recurse -Force -ErrorAction SilentlyContinue
    } else {
        # Already configured: `cmake --build` re-runs configure by itself if CMakeLists change
        $needsConfigure = $false
        Write-Host "  [OK] CMake already configured, skipping" -ForegroundColor Green
    }
}

Push-Location $buildDir
if ($needsConfigure) {
try {
    $cmakeArgs = @(
        "..",
        "-G", "$cmakeGenerator"
    )

    if ($cmakeGenerator -like "Visual Studio*") {
        $cmakeArgs += @("-A", "x64")
    }

    if (-not [string]::IsNullOrEmpty($cmakeToolchain)) {
        $cmakeArgs += @("-DCMAKE_TOOLCHAIN_FILE=$cmakeToolchain", "-DVCPKG_TARGET_TRIPLET=$vcpkgTriplet")
    }

    Write-Host "  Running: cmake $cmakeArgs" -ForegroundColor Cyan
    cmake @cmakeArgs

    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [OK] CMake configured successfully" -ForegroundColor Green
    } else {
        Write-Host "  [ERROR] Failed to configure CMake" -ForegroundColor Red
        Pop-Location
        exit 1
    }
} catch {
    Write-Host "  [ERROR] CMake configuration error: $_" -ForegroundColor Red
    Pop-Location
    exit 1
}
}
Pop-Location

Write-Host "[build_and_run_windows] Building..." -ForegroundColor Cyan

Push-Location $buildDir
try {
    $cpuCount = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
    Write-Host "  Running: cmake --build . --config Release --parallel $cpuCount" -ForegroundColor Cyan
    cmake --build . --config Release --parallel $cpuCount

    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [OK] Project built successfully" -ForegroundColor Green
    } else {
        Write-Host "  [ERROR] Failed to build project" -ForegroundColor Red
        Pop-Location
        exit 1
    }
} catch {
    Write-Host "  [ERROR] Build error: $_" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location

$runApp = "1"
$env:RUN_APP = if ($env:RUN_APP) { $env:RUN_APP } else { "1" }
$runApp = $env:RUN_APP

if ($runApp -eq "1") {
    Write-Host "[build_and_run_windows] Running application..." -ForegroundColor Cyan

    $appPath = ""
    if ($useMake) {
        $appPath = "$buildDir\app.exe"
    } else {
        $appPath = "$buildDir\Release\app.exe"
        if (-not (Test-Path $appPath)) {
            $appPath = "$buildDir\Debug\app.exe"
        }
    }

    if (Test-Path $appPath) {
        Write-Host "  Running: $appPath" -ForegroundColor Cyan
        Write-Host "  Working dir: $scriptDir" -ForegroundColor Cyan

        # Copy librtmidi.dll next to app.exe so it can be found at runtime
        $rtmidiDll = "$buildDir\rtmidi-build\librtmidi.dll"
        if (Test-Path $rtmidiDll) {
            Copy-Item $rtmidiDll (Split-Path $appPath) -Force
            Write-Host "  [OK] Copied librtmidi.dll to app directory" -ForegroundColor Green
        }

        Push-Location $scriptDir
        try {
            & $appPath
            $appExitCode = $LASTEXITCODE
            if ($appExitCode -ne 0) {
                Write-Host "  [WARN] Application exited with code $appExitCode" -ForegroundColor Yellow
            }
        } finally {
            Pop-Location
        }
    } else {
        Write-Host "  [ERROR] Executable not found" -ForegroundColor Red
        Write-Host "  Searched in:" -ForegroundColor Yellow
        if ($useMake) {
            Write-Host "    - $buildDir\app.exe" -ForegroundColor Yellow
        } else {
            Write-Host "    - $buildDir\Release\app.exe" -ForegroundColor Yellow
            Write-Host "    - $buildDir\Debug\app.exe" -ForegroundColor Yellow
        }
        exit 1
    }
}

Write-Host "[build_and_run_windows] Done!" -ForegroundColor Green
