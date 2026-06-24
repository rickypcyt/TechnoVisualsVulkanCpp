#!/usr/bin/env python3
"""Convert pass_*.frag shaders to compute shader equivalents.

Usage: run from the repo root:
    python3 tools/frag_to_compute.py
"""
import re
from pathlib import Path

PASSES = [
    ("pass_a_base", 4),
    ("pass_b_spatial", 1),
    ("pass_c_detail", 1),
    ("pass_d_temporal", 2),
    ("pass_e_degradation", 1),
    ("pass_f_color", 1),
    ("pass_g_output", 2),
]


def convert(name: str, output_binding: int) -> None:
    frag_path = Path("shaders") / f"{name}.frag"
    comp_path = Path("shaders") / f"{name}.comp"

    source = frag_path.read_text()

    # Add compute local size after the version line.
    source = source.replace(
        "#version 450\n",
        "#version 450\nlayout(local_size_x = 8, local_size_y = 8) in;\n",
    )

    # Remove fragment input uv (will be computed inside main).
    source = source.replace("layout(location = 0) in vec2 uv;\n", "")

    # Replace fragment output with storage image.
    source = source.replace(
        "layout(location = 0) out vec4 outColor;\n",
        f"layout(set = 1, binding = {output_binding}, rgba8) uniform writeonly image2D outputImage;\n",
    )

    # Find main() and inject uv calculation and final imageStore.
    main_match = re.search(r"void main\(\) \{\n", source)
    if not main_match:
        raise RuntimeError(f"Could not find main() in {frag_path}")
    main_start = main_match.end()

    # Find matching closing brace for main().
    depth = 1
    i = main_start
    while i < len(source) and depth > 0:
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
        i += 1
    main_end = i - 1  # Points at the closing '}'.

    main_body = source[main_start:main_end]

    # Compute uv from global invocation ID and image size.
    uv_line = "    vec2 uv = vec2(gl_GlobalInvocationID.xy) / vec2(imageSize(outputImage));\n"
    main_body = uv_line + main_body

    # Replace all outColor assignments with imageStore writes.
    # Handles both final writes and early-return writes.
    main_body = re.sub(
        r"outColor\s*=\s*([^;]+);",
        r"imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), \1);",
        main_body,
    )

    source = source[:main_start] + main_body + source[main_end:]

    comp_path.write_text(source)
    print(f"Created {comp_path}")


if __name__ == "__main__":
    for name, binding in PASSES:
        convert(name, binding)
