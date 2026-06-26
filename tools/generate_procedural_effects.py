#!/usr/bin/env python3
"""
Generate simplified procedural post-effects inspired by the procedural shaders.
Instead of converting complex code, create inspired effects that work as post-effects.
"""

import os
import re
import subprocess
from pathlib import Path

# Source directory
PROCEDURAL_DIR = Path("procedural")
POST_EFFECTS_DIR = Path("shaders/post_effects")

# Effect templates based on procedural concepts
EFFECT_TEMPLATES = {
    "breathing": {
        "name": "Procedural Breathing",
        "description": "Rhythmic expansion effect with audio reactivity",
        "code": """
vec2 p = (uv - 0.5) * 2.0;
p *= ubo.cameraZoom;
float l = length(p);
float t = 1.0 - pow(abs(fract(ubo.time * 0.5) - 1.0), 5.0);
float breath = sin(t * 6.28 - l * 3.0);
breath *= 1.0 - clamp(l, 0.0, 1.0);
float audioReactivity = ubo.energy * 0.3 + ubo.bass * 0.4;
breath *= (1.0 + audioReactivity * 2.0);
vec3 color = mix(ubo.primaryColor, ubo.secondaryColor, ubo.colorBlend);
color *= breath;
"""
    },
    "cylinder_repeat": {
        "name": "Cylinder Repeat",
        "description": "Volumetric cylinder pattern with glow",
        "code": """
vec2 p = (uv - 0.5) * 2.0;
p *= ubo.cameraZoom;
float angle = atan(p.y, p.x);
float radius = length(p);
float repeat = sin(radius * 10.0 - ubo.time * 2.0 + angle * 3.0);
repeat += sin(radius * 15.0 + ubo.time * 1.5 - angle * 2.0);
repeat *= (1.0 + ubo.bass * 0.5 + ubo.energy * 0.3);
vec3 color = mix(ubo.primaryColor, ubo.secondaryColor, repeat * 0.5 + 0.5);
float pattern = smoothstep(-0.5, 0.5, repeat);
color = mix(original, color, pattern * 0.6);
float glow = pow(abs(repeat), 2.0) * ubo.high * 0.5;
color += ubo.secondaryColor * glow * 0.3;
"""
    },
    "kaleidoscope": {
        "name": "Kaleidoscope",
        "description": "Symmetric kaleidoscope pattern",
        "code": """
vec2 p = (uv - 0.5) * 2.0;
p *= ubo.cameraZoom;
float angle = atan(p.y, p.x);
float segments = 8.0;
angle = mod(angle, 6.28 / segments);
angle = abs(angle - 3.14 / segments);
p = length(p) * vec2(cos(angle), sin(angle));
p += ubo.time * 0.2;
float pattern = sin(p.x * 10.0 + ubo.time) * sin(p.y * 10.0 - ubo.time);
pattern *= (1.0 + ubo.mid * 0.5);
vec3 color = mix(ubo.primaryColor, ubo.secondaryColor, pattern * 0.5 + 0.5);
color = mix(original, color, 0.5 + ubo.energy * 0.3);
"""
    },
    "pouet": {
        "name": "Pouet Grid",
        "description": "Retro grid effect",
        "code": """
vec2 p = (uv - 0.5) * 2.0;
p *= ubo.cameraZoom;
float gridX = sin(p.x * 20.0 + ubo.time * 2.0);
float gridY = sin(p.y * 20.0 - ubo.time * 1.5);
float grid = gridX * gridY;
grid *= (1.0 + ubo.bass * 0.3);
vec3 color = mix(ubo.primaryColor, ubo.secondaryColor, grid * 0.5 + 0.5);
float scanline = sin(p.y * 100.0 + ubo.time * 5.0) * 0.05;
color += scanline;
color = mix(original, color, 0.4 + ubo.energy * 0.2);
"""
    },
    "eiyeron": {
        "name": "Eiyeron Deform",
        "description": "Deformation effect with audio reactivity",
        "code": """
vec2 p = (uv - 0.5) * 2.0;
p *= ubo.cameraZoom;
float deform = sin(p.x * 5.0 + ubo.time) * sin(p.y * 5.0 - ubo.time);
p += deform * 0.1 * (1.0 + ubo.energy);
float pattern = length(p) + deform;
vec3 color = mix(ubo.primaryColor, ubo.secondaryColor, pattern * 0.5 + 0.5);
color = mix(original, color, 0.5 + ubo.bass * 0.2);
"""
    },
    "head": {
        "name": "Head Effect",
        "description": "Abstract head-like visualization",
        "code": """
vec2 p = (uv - 0.5) * 2.0;
p *= ubo.cameraZoom;
float head = 1.0 - length(p);
head = smoothstep(0.0, 0.5, head);
float eyes = sin(p.x * 10.0 + ubo.time) * sin(p.y * 10.0);
eyes *= (1.0 + ubo.bass * 0.5);
vec3 color = mix(ubo.primaryColor, ubo.secondaryColor, head);
color += ubo.secondaryColor * eyes * 0.3;
color = mix(original, color, 0.6 + ubo.energy * 0.2);
"""
    },
    "power_particle": {
        "name": "Power Particle",
        "description": "Particle system effect",
        "code": """
vec2 p = (uv - 0.5) * 2.0;
p *= ubo.cameraZoom;
float particle = 0.0;
for (int i = 0; i < 5; i++) {
    vec2 offset = vec2(float(i) * 0.2 - 0.4, sin(ubo.time + float(i)) * 0.3);
    float d = length(p - offset);
    particle += 0.02 / (d + 0.01);
}
particle *= (1.0 + ubo.energy * 0.5);
vec3 color = mix(ubo.primaryColor, ubo.secondaryColor, particle);
color = mix(original, color, 0.5);
"""
    },
    "message": {
        "name": "Message Tunnel",
        "description": "Tunnel message effect",
        "code": """
vec2 p = (uv - 0.5) * 2.0;
p *= ubo.cameraZoom;
float tunnel = 1.0 / length(p);
tunnel *= sin(length(p) * 10.0 - ubo.time * 3.0);
tunnel *= (1.0 + ubo.bass * 0.4);
vec3 color = mix(ubo.primaryColor, ubo.secondaryColor, tunnel * 0.5 + 0.5);
color = mix(original, color, 0.4 + ubo.energy * 0.3);
"""
    }
}

COMPUTE_TEMPLATE = """#version 450
layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 1, rgba16f) uniform image2D outputImage;

layout(set = 1, binding = 0) uniform UniformBufferObject {{
    float time;
    float tempo;
    float energy;
    float bass;
    float mid;
    float high;
    vec3 primaryColor;
    vec3 secondaryColor;
    float colorBlend;
    vec2 resolution;
    float cameraZoom;
    float cameraPanX;
    float cameraPanY;
}} ubo;

void main() {{
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSize = imageSize(outputImage);
    
    if (pixelCoords.x >= imageSize.x || pixelCoords.y >= imageSize.y) {{
        return;
    }}
    
    vec2 uv = vec2(pixelCoords) / vec2(imageSize);
    vec3 original = texelFetch(inputImage, pixelCoords, 0).rgb;
    
{EFFECT_CODE}
    
    imageStore(outputImage, pixelCoords, vec4(color, 1.0));
}}
"""

def main():
    """Generate procedural-inspired post-effects."""
    os.chdir('/home/ricky/coding/proyects/vulkancpp')
    
    print(f"Generating {len(EFFECT_TEMPLATES)} procedural-inspired effects")
    
    for effect_name, effect_data in EFFECT_TEMPLATES.items():
        print(f"Generating {effect_data['name']}...")
        
        # Generate compute shader
        compute_shader = COMPUTE_TEMPLATE.format(EFFECT_CODE=effect_data['code'])
        
        # Write compute shader
        output_file = POST_EFFECTS_DIR / f"procedural_{effect_name}.comp"
        with open(output_file, 'w') as f:
            f.write(compute_shader)
        
        # Compile to SPIR-V
        spv_file = POST_EFFECTS_DIR / f"procedural_{effect_name}.comp.spv"
        result = subprocess.run(
            ['glslc', '-fshader-stage=compute', str(output_file), '-o', str(spv_file)],
            capture_output=True,
            text=True
        )
        
        if result.returncode == 0:
            print(f"  ✓ Compiled {effect_data['name']}")
        else:
            print(f"  ✗ Compilation failed for {effect_data['name']}")
            print(f"    Error: {result.stderr}")
    
    print("\nDone! Effects generated:")
    for name, data in EFFECT_TEMPLATES.items():
        print(f"  - {data['name']}")

if __name__ == "__main__":
    main()
