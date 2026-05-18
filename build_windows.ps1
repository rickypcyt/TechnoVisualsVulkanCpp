# ============================================================================
# SCRIPT DE COMPILACIÓN PARA WINDOWS - VulkanApp
# ============================================================================
# Este script automatiza el proceso de instalación de dependencias y compilación
# del proyecto VulkanCpp en Windows.
#
# REQUISITOS PREVIOS:
# 1. Visual Studio 2019 o superior con "Desktop development with C++"
# 2. Git instalado (https://git-scm.com/download/win)
# 3. CMake instalado (https://cmake.org/download/)
# 4. Vulkan SDK instalado (https://vulkan.lunarg.com/)
#
# NOTA: Ejecuta este script desde la raíz del proyecto (donde está CMakeLists.txt)
# Puedes ejecutarlo como: .\build_windows.ps1
# Si PowerShell bloquea scripts, ejecuta: Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
# ============================================================================

$ErrorActionPreference = "Stop"

# ============================================================================
# CONFIGURACIÓN DE RUTAS Y VARIABLES
# ============================================================================
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = $scriptDir
$vcpkgDir = "$projectRoot\vcpkg"
$buildDir = "$projectRoot\build"
$vcpkgTriplet = "x64-windows"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "VulkanApp - Script de Compilación Windows" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ============================================================================
# VERIFICACIÓN DE REQUISITOS PREVIOS
# ============================================================================
Write-Host "[1/7] Verificando requisitos previos..." -ForegroundColor Yellow

# Verificar Visual Studio
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
    if ($vsPath) {
        Write-Host "  ✓ Visual Studio encontrado en: $vsPath" -ForegroundColor Green
    } else {
        Write-Host "  ✗ Visual Studio no encontrado. Por favor instala Visual Studio 2019+ con 'Desktop development with C++'" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "  ✗ Visual Studio Installer no encontrado. Por favor instala Visual Studio 2019+" -ForegroundColor Red
    exit 1
}

# Verificar CMake
try {
    $cmakeVersion = cmake --version 2>&1
    Write-Host "  ✓ CMake encontrado: $cmakeVersion" -ForegroundColor Green
} catch {
    Write-Host "  ✗ CMake no encontrado. Por favor instala CMake desde https://cmake.org/download/" -ForegroundColor Red
    exit 1
}

# Verificar Git
try {
    $gitVersion = git --version 2>&1
    Write-Host "  ✓ Git encontrado: $gitVersion" -ForegroundColor Green
} catch {
    Write-Host "  ✗ Git no encontrado. Por favor instala Git desde https://git-scm.com/download/win" -ForegroundColor Red
    exit 1
}

# Verificar Vulkan SDK
$vulkanSdk = $env:VULKAN_SDK
if ([string]::IsNullOrEmpty($vulkanSdk)) {
    Write-Host "  ⚠ VULKAN_SDK no está configurado en variables de entorno" -ForegroundColor Yellow
    Write-Host "    Por favor instala Vulkan SDK desde https://vulkan.lunarg.com/" -ForegroundColor Yellow
    Write-Host "    Y configura la variable de entorno VULKAN_SDK apuntando a la instalación" -ForegroundColor Yellow
    Write-Host "    Continuando de todos modos..." -ForegroundColor Yellow
} else {
    Write-Host "  ✓ Vulkan SDK encontrado en: $vulkanSdk" -ForegroundColor Green
}

Write-Host ""

# ============================================================================
# INSTALACIÓN DE VCPKG (GESTOR DE PAQUETES)
# ============================================================================
Write-Host "[2/7] Configurando vcpkg..." -ForegroundColor Yellow

if (-not (Test-Path $vcpkgDir)) {
    Write-Host "  vcpkg no encontrado. Clonando desde GitHub..." -ForegroundColor Cyan
    try {
        git clone https://github.com/Microsoft/vcpkg.git $vcpkgDir
        Write-Host "  ✓ vcpkg clonado exitosamente" -ForegroundColor Green
    } catch {
        Write-Host "  ✗ Error al clonar vcpkg: $_" -ForegroundColor Red
        exit 1
    }

    Write-Host "  Inicializando vcpkg (esto puede tardar varios minutos)..." -ForegroundColor Cyan
    Push-Location $vcpkgDir
    try {
        .\bootstrap-vcpkg.bat
        Write-Host "  ✓ vcpkg inicializado exitosamente" -ForegroundColor Green
    } catch {
        Write-Host "  ✗ Error al inicializar vcpkg: $_" -ForegroundColor Red
        Pop-Location
        exit 1
    }
    Pop-Location
} else {
    Write-Host "  ✓ vcpkg ya existe en: $vcpkgDir" -ForegroundColor Green
}

# Integrar vcpkg con el sistema de build
Write-Host "  Integrando vcpkg con el sistema de build..." -ForegroundColor Cyan
Push-Location $vcpkgDir
try {
    .\vcpkg integrate install
    Write-Host "  ✓ vcpkg integrado exitosamente" -ForegroundColor Green
} catch {
    Write-Host "  ✗ Error al integrar vcpkg: $_" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location

Write-Host ""

# ============================================================================
# INSTALACIÓN DE DEPENDENCIAS CON VCPKG
# ============================================================================
Write-Host "[3/7] Instalando dependencias con vcpkg..." -ForegroundColor Yellow
Write-Host "  NOTA: Este proceso puede tardar 10-30 minutos dependiendo de tu conexión y CPU" -ForegroundColor Cyan

$dependencies = @(
    "sdl2",
    "glm",
    "ffmpeg",
    "portaudio",
    "liblo"
)

foreach ($dep in $dependencies) {
    Write-Host "  Instalando $dep:$vcpkgTriplet..." -ForegroundColor Cyan
    Push-Location $vcpkgDir
    try {
        .\vcpkg install $dep:$vcpkgTriplet
        Write-Host "  ✓ $dep instalado exitosamente" -ForegroundColor Green
    } catch {
        Write-Host "  ✗ Error al instalar $dep: $_" -ForegroundColor Red
        Write-Host "    Intentando continuar..." -ForegroundColor Yellow
    }
    Pop-Location
}

Write-Host ""

# ============================================================================
# PREPARACIÓN DEL DIRECTORIO DE BUILD
# ============================================================================
Write-Host "[4/7] Preparando directorio de build..." -ForegroundColor Yellow

if (Test-Path $buildDir) {
    Write-Host "  Limpiando directorio de build existente..." -ForegroundColor Cyan
    Remove-Item -Path $buildDir -Recurse -Force
}

New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
Write-Host "  ✓ Directorio de build creado: $buildDir" -ForegroundColor Green

Write-Host ""

# ============================================================================
# CONFIGURACIÓN DE CMAKE
# ============================================================================
Write-Host "[5/7] Configurando CMake..." -ForegroundColor Yellow

$cmakeToolchain = "$vcpkgDir\scripts\buildsystems\vcpkg.cmake"
$cmakeGenerator = "Visual Studio 17 2022"

# Intentar detectar la versión de Visual Studio instalada
try {
    $vsYear = & $vsWhere -latest -property installationVersion
    if ($vsYear -like "17*") {
        $cmakeGenerator = "Visual Studio 17 2022"
    } elseif ($vsYear -like "16*") {
        $cmakeGenerator = "Visual Studio 16 2019"
    }
    Write-Host "  Usando generator: $cmakeGenerator" -ForegroundColor Cyan
} catch {
    Write-Host "  Usando generator por defecto: Visual Studio 17 2022" -ForegroundColor Cyan
}

Push-Location $buildDir
try {
    $cmakeArgs = @(
        "..",
        "-DCMAKE_TOOLCHAIN_FILE=$cmakeToolchain",
        "-DVCPKG_TARGET_TRIPLET=$vcpkgTriplet",
        "-G", "$cmakeGenerator",
        "-A", "x64"
    )
    
    Write-Host "  Ejecutando: cmake $cmakeArgs" -ForegroundColor Cyan
    cmake @cmakeArgs
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  ✓ CMake configurado exitosamente" -ForegroundColor Green
    } else {
        Write-Host "  ✗ Error al configurar CMake" -ForegroundColor Red
        Pop-Location
        exit 1
    }
} catch {
    Write-Host "  ✗ Error al configurar CMake: $_" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location

Write-Host ""

# ============================================================================
# COMPILACIÓN DEL PROYECTO
# ============================================================================
Write-Host "[6/7] Compilando el proyecto..." -ForegroundColor Yellow
Write-Host "  NOTA: Este proceso puede tardar varios minutos" -ForegroundColor Cyan

Push-Location $buildDir
try {
    cmake --build . --config Release
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  ✓ Proyecto compilado exitosamente" -ForegroundColor Green
    } else {
        Write-Host "  ✗ Error al compilar el proyecto" -ForegroundColor Red
        Pop-Location
        exit 1
    }
} catch {
    Write-Host "  ✗ Error al compilar: $_" -ForegroundColor Red
    Pop-Location
    exit 1
}
Pop-Location

Write-Host ""

# ============================================================================
# COPIA DE DLLS NECESARIAS
# ============================================================================
Write-Host "[7/7] Copiando DLLs necesarias..." -ForegroundColor Yellow

$outputDir = "$buildDir\Release"
$binDir = "$vcpkgDir\installed\$vcpkgTriplet\bin"

if (Test-Path $outputDir) {
    # Copiar DLLs de vcpkg al directorio de salida
    $dlls = @(
        "SDL2.dll",
        "avcodec-*.dll",
        "avformat-*.dll",
        "avutil-*.dll",
        "swscale-*.dll",
        "swresample-*.dll",
        "portaudio.dll"
    )
    
    foreach ($dllPattern in $dlls) {
        $dllFiles = Get-ChildItem -Path $binDir -Filter $dllPattern -ErrorAction SilentlyContinue
        foreach ($dll in $dllFiles) {
            Copy-Item -Path $dll.FullName -Destination $outputDir -Force
            Write-Host "  ✓ Copiado: $($dll.Name)" -ForegroundColor Green
        }
    }
    
    Write-Host "  ✓ DLLs copiadas al directorio de salida" -ForegroundColor Green
} else {
    Write-Host "  ⚠ Directorio de salida no encontrado: $outputDir" -ForegroundColor Yellow
}

Write-Host ""

# ============================================================================
# INSTRUCCIONES FINALES
# ============================================================================
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "¡COMPILACIÓN COMPLETADA!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "El ejecutable se encuentra en:" -ForegroundColor White
Write-Host "  $outputDir\app.exe" -ForegroundColor Cyan
Write-Host ""
Write-Host "Para ejecutar la aplicación:" -ForegroundColor White
Write-Host "  cd $outputDir" -ForegroundColor Yellow
Write-Host "  .\app.exe" -ForegroundColor Yellow
Write-Host ""
Write-Host "Para habilitar Vulkan validation layers:" -ForegroundColor White
Write-Host "  $env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'; .\app.exe" -ForegroundColor Yellow
Write-Host ""
Write-Host "Si tienes problemas con DLLs faltantes, asegúrate de que:" -ForegroundColor White
Write-Host "  1. Vulkan SDK esté instalado y en el PATH" -ForegroundColor Yellow
Write-Host "  2. Las DLLs de FFmpeg estén en el mismo directorio que app.exe" -ForegroundColor Yellow
Write-Host "  3. Las DLLs de SDL2 estén accesibles" -ForegroundColor Yellow
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
