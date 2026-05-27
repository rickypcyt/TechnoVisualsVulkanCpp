#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
SHADERS_DIR="$SCRIPT_DIR/shaders"

mkdir -p "$BUILD_DIR"

# Detect glslc
if ! command -v glslc >/dev/null 2>&1; then
    echo "[build_and_run] error: glslc not found (Vulkan SDK required)" >&2
    exit 1
fi

echo "[build_and_run] Compiling shaders..."

# Shader list (single source of truth)
shaders=(
  "triangle.vert"
  "triangle.frag"
  "present.vert"
  "present.frag"
  "fullscreen.vert"
  "fullscreen.frag"
  "pass_a_base.frag"
  "pass_b_spatial.frag"
  "pass_c_detail.frag"
  "pass_d_temporal.frag"
  "pass_e_degradation.frag"
  "pass_f_color.frag"
  "pass_g_output.frag"
)

# Compile shaders only if needed
for shader in "${shaders[@]}"; do
    src="$SHADERS_DIR/$shader"
    out="$SHADERS_DIR/$shader.spv"

    if [[ ! -f "$src" ]]; then
        echo "[build_and_run] warning: missing shader $src"
        continue
    fi

    if [[ "$src" -nt "$out" ]]; then
        echo "[build_and_run] compiling $shader"
        glslc "$src" -o "$out"
    else
        echo "[build_and_run] up-to-date $shader"
    fi
done

echo "[build_and_run] Configuring CMake..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR"

echo "[build_and_run] Building..."
cmake --build "$BUILD_DIR" --parallel

# Optional execution control
RUN_APP="${RUN_APP:-1}"
SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-}"

# ── Interactive prompts ──────────────────────────────────────────────
USE_MANGOHUD=""

if [[ "$RUN_APP" == "1" && -t 0 ]]; then
    read -rp "Activar MangoHud? [s/N]: " mh
    if [[ "${mh,,}" == "s" ]]; then
        USE_MANGOHUD=1
        echo "[build_and_run] MangoHud activado"
    fi

    read -rp "Elegir GPU (0=iGPU / 1=dGPU / Enter=default): " gpu
    if [[ "$gpu" == "0" ]]; then
        export DRI_PRIME=0
        echo "[build_and_run] Forzando iGPU (DRI_PRIME=0)"
    elif [[ "$gpu" == "1" ]]; then
        export DRI_PRIME=1
        echo "[build_and_run] Forzando dGPU (DRI_PRIME=1)"
    fi
fi

if [[ "$RUN_APP" == "1" ]]; then
    echo "[build_and_run] Running application..."
    [[ -n "$SDL_VIDEODRIVER" ]] && export SDL_VIDEODRIVER="$SDL_VIDEODRIVER"
    if [[ "$USE_MANGOHUD" == "1" ]]; then
        mangohud "$BUILD_DIR/app"
    else
        "$BUILD_DIR/app"
    fi
fi