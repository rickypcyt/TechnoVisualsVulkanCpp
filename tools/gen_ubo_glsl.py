#!/usr/bin/env python3
"""Generate shared_ubo.glsl from the single-source GlobalParamsUBO layout."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER_PATH = ROOT / "src" / "ubo_layout.h"
OUTPUT_PATH = ROOT / "shaders" / "shared_ubo.glsl"

ALIGN_PATTERN = re.compile(r"alignas\(\d+\)\s+([\w:]+)\s+(\w+);")
CPP_TO_GLSL = {
    "glm::mat4": "mat4",
    "glm::vec4": "vec4",
    "glm::vec3": "vec3",
    "glm::vec2": "vec2",
    "float": "float",
    "int": "int",
    "uint32_t": "uint",
}

def parse_fields(header: str) -> list[tuple[str, str]]:
    matches = ALIGN_PATTERN.findall(header)
    if not matches:
        raise RuntimeError("No fields were parsed from GlobalParamsUBO. Did the layout change?")
    return matches

def generate_glsl(fields: list[tuple[str, str]]) -> str:
    lines = [
        "// AUTO-GENERATED FILE — do not edit.",
        "// Run tools/gen_ubo_glsl.py after editing src/ubo_layout.h.",
        "",
        "#ifndef SHARED_UBO_GLSL",
        "#define SHARED_UBO_GLSL",
        "",
        "layout(set = 0, binding = 0, std140) uniform GlobalParamsUBO {",
    ]

    for cpp_type, name in fields:
        if name.startswith("_pad"):
            continue
        glsl_type = CPP_TO_GLSL.get(cpp_type)
        if glsl_type is None:
            raise RuntimeError(f"Unsupported type '{cpp_type}' in GlobalParamsUBO")
        lines.append(f"    {glsl_type} {name};")

    lines.append("} ubo;")
    lines.append("")
    lines.append("#endif // SHARED_UBO_GLSL")
    lines.append("")
    return "\n".join(lines)

def main() -> int:
    if not HEADER_PATH.exists():
        raise FileNotFoundError(f"Cannot find {HEADER_PATH}")

    header_source = HEADER_PATH.read_text()
    fields = parse_fields(header_source)
    glsl_source = generate_glsl(fields)
    OUTPUT_PATH.write_text(glsl_source)
    print(f"[gen_ubo_glsl] Generated {OUTPUT_PATH.relative_to(ROOT)} with {len(fields)} fields")
    return 0

if __name__ == "__main__":
    sys.exit(main())
