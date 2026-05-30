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

# Find the oldest .spv so .glsl changes are compared against it
oldest_spv=""
for shader in "${shaders[@]}"; do
    out="$SHADERS_DIR/$shader.spv"
    if [[ -f "$out" ]]; then
        if [[ -z "$oldest_spv" || "$out" -ot "$oldest_spv" ]]; then
            oldest_spv="$out"
        fi
    fi
done

# Detect if any .glsl include file changed (newer than the oldest .spv)
force_recompile=0
for glsl in "$SHADERS_DIR"/*.glsl; do
    if [[ ! -f "$glsl" ]]; then continue; fi
    if [[ -z "$oldest_spv" || "$glsl" -nt "$oldest_spv" ]]; then
        force_recompile=1
        break
    fi
done

# Compile shaders only if needed
for shader in "${shaders[@]}"; do
    src="$SHADERS_DIR/$shader"
    out="$SHADERS_DIR/$shader.spv"

    if [[ ! -f "$src" ]]; then
        echo "[build_and_run] warning: missing shader $src"
        continue
    fi

    if [[ "$force_recompile" -eq 1 || "$src" -nt "$out" ]]; then
        echo "[build_and_run] compiling $shader"
        glslc "$src" -o "$out"
    else
        echo "[build_and_run] up-to-date $shader"
    fi
done

echo "[build_and_run] Configuring CMake..."
cmake -G Ninja -S "$SCRIPT_DIR" -B "$BUILD_DIR"

echo "[build_and_run] Building..."
ninja -C "$BUILD_DIR"

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

    read -rp "Elegir GPU (0=AMD/iGPU / 1=NVIDIA/dGPU / Enter=auto): " gpu
    if [[ "$gpu" == "0" ]]; then
        export VULKAN_GPU_TYPE="integrated"
        echo "[build_and_run] Forzando iGPU/AMD (VULKAN_GPU_TYPE=integrated)"
    elif [[ "$gpu" == "1" ]]; then
        export VULKAN_GPU_TYPE="discrete"
        echo "[build_and_run] Forzando dGPU/NVIDIA (VULKAN_GPU_TYPE=discrete)"
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