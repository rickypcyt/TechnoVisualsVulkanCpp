#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$SCRIPT_DIR/build"
shaders_dir="$SCRIPT_DIR/shaders"

if ! command -v glslangValidator >/dev/null 2>&1; then
    echo "[build_and_run] error: glslangValidator not found, shader compilation is required" >&2
    exit 1
fi

glslangValidator -V "$shaders_dir/triangle.vert" -o "$shaders_dir/triangle.vert.spv"
glslangValidator -V "$shaders_dir/triangle.frag" -o "$shaders_dir/triangle.frag.spv"
glslangValidator -V "$shaders_dir/present.vert" -o "$shaders_dir/present.vert.spv"
glslangValidator -V "$shaders_dir/present.frag" -o "$shaders_dir/present.frag.spv"
glslangValidator -V "$shaders_dir/fullscreen.vert" -o "$shaders_dir/fullscreen.vert.spv"
glslangValidator -V "$shaders_dir/fullscreen.frag" -o "$shaders_dir/fullscreen.frag.spv"
rm -rf "$SCRIPT_DIR/build/CMakeCache.txt" "$SCRIPT_DIR/build/CMakeFiles"
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build"
cmake --build "$SCRIPT_DIR/build"
SDL_VIDEODRIVER=wayland "$SCRIPT_DIR/build/app"
