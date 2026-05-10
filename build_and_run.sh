#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$SCRIPT_DIR/build"
shaders_dir="$SCRIPT_DIR/shaders"

if ! command -v glslc >/dev/null 2>&1; then
    echo "[build_and_run] error: glslc not found, shader compilation is required" >&2
    exit 1
fi

glslc "$shaders_dir/triangle.vert" -o "$shaders_dir/triangle.vert.spv"
glslc "$shaders_dir/triangle.frag" -o "$shaders_dir/triangle.frag.spv"
glslc "$shaders_dir/present.vert" -o "$shaders_dir/present.vert.spv"
glslc "$shaders_dir/present.frag" -o "$shaders_dir/present.frag.spv"
glslc "$shaders_dir/fullscreen.vert" -o "$shaders_dir/fullscreen.vert.spv"
glslc "$shaders_dir/fullscreen.frag" -o "$shaders_dir/fullscreen.frag.spv"
glslc "$shaders_dir/pass_a_base.frag" -o "$shaders_dir/pass_a_base.frag.spv"
glslc "$shaders_dir/pass_b_spatial.frag" -o "$shaders_dir/pass_b_spatial.frag.spv"
glslc "$shaders_dir/pass_c_detail.frag" -o "$shaders_dir/pass_c_detail.frag.spv"
glslc "$shaders_dir/pass_d_temporal.frag" -o "$shaders_dir/pass_d_temporal.frag.spv"
glslc "$shaders_dir/pass_e_degradation.frag" -o "$shaders_dir/pass_e_degradation.frag.spv"
glslc "$shaders_dir/pass_f_color.frag" -o "$shaders_dir/pass_f_color.frag.spv"
glslc "$shaders_dir/pass_g_output.frag" -o "$shaders_dir/pass_g_output.frag.spv"
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build"
cmake --build "$SCRIPT_DIR/build"
SDL_VIDEODRIVER=wayland "$SCRIPT_DIR/build/app"
