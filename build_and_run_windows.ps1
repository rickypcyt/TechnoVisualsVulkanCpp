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

# Delete all .spv files to force full recompile
Write-Host "  Cleaning old .spv files..." -ForegroundColor Cyan
$postEffectDir = "$shadersDir\post_effects"
Get-ChildItem -Path "$shadersDir\*.spv" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem -Path "$postEffectDir\*.spv" -ErrorAction SilentlyContinue | Remove-Item -Force

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

$oldestSpv = $null
foreach ($shader in $shaders) {
    $out = "$shadersDir\$shader.spv"
    if (Test-Path $out) {
        if ($null -eq $oldestSpv -or (Get-Item $out).LastWriteTime -lt (Get-Item $oldestSpv).LastWriteTime) {
            $oldestSpv = $out
        }
    }
}

$forceRecompile = $false
if ($null -ne $oldestSpv) {
    foreach ($glsl in Get-ChildItem -Path "$shadersDir\*.glsl" -ErrorAction SilentlyContinue) {
        if ($glsl.LastWriteTime -gt (Get-Item $oldestSpv).LastWriteTime) {
            $forceRecompile = $true
            break
        }
    }
    # Also check procedural/ directory for changes
    foreach ($glsl in Get-ChildItem -Path "$scriptDir\procedural\*.glsl" -ErrorAction SilentlyContinue) {
        if ($glsl.LastWriteTime -gt (Get-Item $oldestSpv).LastWriteTime) {
            $forceRecompile = $true
            # Also delete pass_a_base.frag.spv to force recompile
            $passASpv = "$shadersDir\pass_a_base.frag.spv"
            if (Test-Path $passASpv) {
                Remove-Item $passASpv -Force
            }
            break
        }
    }
} else {
    $forceRecompile = $true
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
    } elseif ($forceRecompile) {
        $needsCompile = $true
    } elseif ((Get-Item $src).LastWriteTime -gt (Get-Item $out).LastWriteTime) {
        $needsCompile = $true
    }

    if ($needsCompile) {
        Write-Host "  Compiling $shader..." -ForegroundColor Cyan
        & $glslcPath $src -o $out
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
    # Check if any shared include file changed — if so, force recompile all
    $forcePeRecompile = $false
    $includeFiles = Get-ChildItem -Path "$shadersDir\*.glsl" -ErrorAction SilentlyContinue
    foreach ($inc in $includeFiles) {
        $incSpvTime = [datetime]::MaxValue
        $anySpv = Get-ChildItem -Path $postEffectDir -Filter "*.comp.spv" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($anySpv) { $incSpvTime = $anySpv.LastWriteTime }
        if ($inc.LastWriteTime -gt $incSpvTime) { $forcePeRecompile = $true; break }
    }
    # Also check includes inside post_effects dir
    $peIncludes = Get-ChildItem -Path "$postEffectDir\*.glsl" -ErrorAction SilentlyContinue
    foreach ($inc in $peIncludes) {
        $incSpvTime = [datetime]::MaxValue
        $anySpv = Get-ChildItem -Path $postEffectDir -Filter "*.comp.spv" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($anySpv) { $incSpvTime = $anySpv.LastWriteTime }
        if ($inc.LastWriteTime -gt $incSpvTime) { $forcePeRecompile = $true; break }
    }

    $compShaders = Get-ChildItem -Path $postEffectDir -Filter "*.glsl" -ErrorAction SilentlyContinue
    $peOk = 0
    $peSkip = 0
    foreach ($comp in $compShaders) {
        # Skip include files (not standalone shaders)
        if ($comp.Name -match "_common\.glsl$") {
            $peSkip++
            continue
        }
        $spvOut = $comp.FullName + ".spv"
        $needsCompile = $forcePeRecompile
        if (-not $needsCompile) {
            if (-not (Test-Path $spvOut)) {
                $needsCompile = $true
            } elseif ($comp.LastWriteTime -gt (Get-Item $spvOut).LastWriteTime) {
                $needsCompile = $true
            }
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

try {
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

Push-Location $buildDir
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
