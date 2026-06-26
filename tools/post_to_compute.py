#!/usr/bin/env python3
"""Convert post_effects/*.glsl (OpenGL 3.3 style) into Vulkan compute shaders.

Usage: run from the repo root:
    python3 tools/post_to_compute.py
"""
import re
from pathlib import Path

POST_EFFECTS_DIR = Path("post_effects")
OUTPUT_DIR = Path("shaders/post_effects")


def convert(glsl_path: Path) -> Path:
    comp_path = OUTPUT_DIR / f"{glsl_path.stem}.comp"
    source = glsl_path.read_text()

    # Switch to the Vulkan-compatible common header
    source = source.replace(
        '#include "post_common.glsl"',
        '#include "../post_common_vulkan.glsl"',
    )
    source = source.replace(
        '#include "mirror_common.glsl"',
        '#include "mirror_common_vulkan.glsl"',
    )

    # Ensure the main file declares the version and include extension before the include
    if not source.startswith("#version"):
        source = '#version 450\n#extension GL_GOOGLE_include_directive : require\n' + source

    # Remove legacy OpenGL I/O declarations if the effect declared them itself
    source = source.replace("in vec2 vUV;\n", "")
    source = source.replace("out vec4 FragColor;\n", "")

    # Remove non-opaque standalone uniforms (their values now come from the UBO via macros)
    source = re.sub(r"^uniform\s+(int|float|vec2|vec3|vec4|bool)\s+\w+[^;]*;.*\n", "", source, flags=re.MULTILINE)

    # Find main() and wrap the body
    main_match = re.search(r"void main\(\) \{\n", source)
    if not main_match:
        raise RuntimeError(f"Could not find main() in {glsl_path}")
    main_start = main_match.end()

    depth = 1
    i = main_start
    while i < len(source) and depth > 0:
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
        i += 1
    main_end = i - 1

    main_body = source[main_start:main_end]

    # Compute vUV from global invocation ID
    uv_line = "    vUV = vec2(gl_GlobalInvocationID.xy) / vec2(imageSize(outputImage));\n"
    main_body = uv_line + main_body

    # Replace all FragColor assignments with imageStore writes
    main_body = re.sub(
        r"FragColor\s*=\s*([^;]+);",
        r"imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), \1);",
        main_body,
    )

    source = source[:main_start] + main_body + source[main_end:]

    comp_path.parent.mkdir(parents=True, exist_ok=True)
    comp_path.write_text(source)
    return comp_path


if __name__ == "__main__":
    for glsl_path in sorted(POST_EFFECTS_DIR.glob("*.glsl")):
        if glsl_path.name == "post_common.glsl":
            continue
        if glsl_path.name == "mirror_common.glsl":
            continue
        comp_path = convert(glsl_path)
        print(f"Created {comp_path}")
