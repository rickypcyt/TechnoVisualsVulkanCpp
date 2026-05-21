#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$SCRIPT_DIR/build"
shaders_dir="$SCRIPT_DIR/shaders"

if ! command -v glslc >/dev/null 2>&1; then
    echo "[build_and_run] error: glslc not found, shader compilation is required" >&2
    exit 1
fi

echo "[build_and_run] Compiling triangle.vert..."
glslc "$shaders_dir/triangle.vert" -o "$shaders_dir/triangle.vert.spv"
echo "[build_and_run] Compiling triangle.frag..."
glslc "$shaders_dir/triangle.frag" -o "$shaders_dir/triangle.frag.spv"
echo "[build_and_run] Compiling present.vert..."
glslc "$shaders_dir/present.vert" -o "$shaders_dir/present.vert.spv"
echo "[build_and_run] Compiling present.frag..."
glslc "$shaders_dir/present.frag" -o "$shaders_dir/present.frag.spv"
echo "[build_and_run] Compiling fullscreen.vert..."
glslc "$shaders_dir/fullscreen.vert" -o "$shaders_dir/fullscreen.vert.spv"
echo "[build_and_run] Compiling fullscreen.frag..."
glslc "$shaders_dir/fullscreen.frag" -o "$shaders_dir/fullscreen.frag.spv"
echo "[build_and_run] Compiling pass_a_base.frag..."
glslc "$shaders_dir/pass_a_base.frag" -o "$shaders_dir/pass_a_base.frag.spv"
echo "[build_and_run] Compiling pass_b_spatial.frag..."
glslc "$shaders_dir/pass_b_spatial.frag" -o "$shaders_dir/pass_b_spatial.frag.spv"
echo "[build_and_run] Compiling pass_c_detail.frag..."
glslc "$shaders_dir/pass_c_detail.frag" -o "$shaders_dir/pass_c_detail.frag.spv"
echo "[build_and_run] Compiling pass_d_temporal.frag..."
glslc "$shaders_dir/pass_d_temporal.frag" -o "$shaders_dir/pass_d_temporal.frag.spv"
echo "[build_and_run] Compiling pass_e_degradation.frag..."
glslc "$shaders_dir/pass_e_degradation.frag" -o "$shaders_dir/pass_e_degradation.frag.spv"
echo "[build_and_run] Compiling pass_f_color.frag..."
glslc "$shaders_dir/pass_f_color.frag" -o "$shaders_dir/pass_f_color.frag.spv"
echo "[build_and_run] Compiling pass_g_output.frag..."
glslc "$shaders_dir/pass_g_output.frag" -o "$shaders_dir/pass_g_output.frag.spv"
echo "[build_and_run] All shaders compiled successfully."
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build"
cmake --build "$SCRIPT_DIR/build" -j$(nproc)
SDL_VIDEODRIVER=wayland "$SCRIPT_DIR/build/app"
