#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$SCRIPT_DIR/build"
shaders_dir="$SCRIPT_DIR/shaders"

if command -v glslangValidator >/dev/null 2>&1; then
    glslangValidator -V "$shaders_dir/triangle.vert" -o "$shaders_dir/triangle.vert.spv"
    glslangValidator -V "$shaders_dir/triangle.frag" -o "$shaders_dir/triangle.frag.spv"
else
    echo "[build_and_run] warning: glslangValidator not found; shaders won't be rebuilt"
fi
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build"
cmake --build "$SCRIPT_DIR/build"
SDL_VIDEODRIVER=wayland "$SCRIPT_DIR/build/app"
