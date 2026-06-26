#!/usr/bin/env python3
"""
Convert procedural shaders from procedural/ folder to compute shaders
for the post-effects system.
"""

import os
import re
import subprocess
from pathlib import Path

# Source and destination directories
PROCEDURAL_DIR = Path("procedural")
POST_EFFECTS_DIR = Path("shaders/post_effects")

# Common helper functions
HELPER_FUNCTIONS = """
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

vec2 rotate(vec2 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

vec2 rotate2D(vec2 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    mat2 m = mat2(1.7, 1.2, -1.2, 1.7);
    for (int i = 0; i < 5; ++i) {
        value += amp * noise(p);
        p = m * p + vec2(0.21, 0.17);
        amp *= 0.5;
    }
    return value;
}
"""

# Compute shader template
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

{HELPER_FUNCTIONS}

{FUNCTIONS}

void main() {{
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSize = imageSize(outputImage);
    
    if (pixelCoords.x >= imageSize.x || pixelCoords.y >= imageSize.y) {{
        return;
    }}
    
    vec2 uv = vec2(pixelCoords) / vec2(imageSize);
    vec3 original = texelFetch(inputImage, pixelCoords, 0).rgb;
    
    {RENDER_CALL}
    
    imageStore(outputImage, pixelCoords, vec4(color, 1.0));
}}
"""

def extract_functions(glsl_content):
    """Extract helper functions from GLSL content (excluding main)."""
    lines = glsl_content.split('\n')
    functions = []
    in_function = False
    function_lines = []
    brace_count = 0
    
    for line in lines:
        # Skip comments and metadata
        if line.strip().startswith('//') or line.strip().startswith('@'):
            continue
        if line.strip().startswith('#version') or line.strip().startswith('in ') or line.strip().startswith('out '):
            continue
        if 'uniform' in line and 'sampler2D' not in line:
            continue
        
        # Detect function definition
        if re.match(r'^\s*(vec[234]|float|void)\s+\w+\s*\(', line):
            if not line.strip().startswith('void main'):
                in_function = True
                brace_count = line.count('{') - line.count('}')
                function_lines = [line]
                continue
        
        if in_function:
            function_lines.append(line)
            brace_count += line.count('{') - line.count('}')
            
            if brace_count <= 0:
                functions.append('\n'.join(function_lines))
                in_function = False
                function_lines = []
    
    return '\n'.join(functions)

def convert_procedural_to_compute(source_file):
    """Convert a procedural shader to compute shader format."""
    with open(source_file, 'r') as f:
        content = f.read()
    
    # Extract functions
    functions = extract_functions(content)
    
    # Extract render function call from main
    render_match = re.search(r'fragColor\s*=\s*(\w+)\(([^)]+)\);', content)
    if render_match:
        render_func = render_match.group(1)
        render_args = render_match.group(2)
        
        # Convert arguments to use ubo
        render_args = render_args.replace('uTime', 'ubo.time')
        render_args = render_args.replace('uTempo', 'ubo.tempo')
        render_args = render_args.replace('uEnergy', 'ubo.energy')
        render_args = render_args.replace('uBass', 'ubo.bass')
        render_args = render_args.replace('uMid', 'ubo.mid')
        render_args = render_args.replace('uHigh', 'ubo.high')
        render_args = render_args.replace('uPrimaryColor', 'ubo.primaryColor')
        render_args = render_args.replace('uSecondaryColor', 'ubo.secondaryColor')
        render_args = render_args.replace('uColorBlend', 'ubo.colorBlend')
        render_args = render_args.replace('uResolution', 'ubo.resolution')
        render_args = render_args.replace('uIntensity', 'ubo.energy')  # Map uIntensity to energy
        
        # Replace .xy swizzles on uResolution
        render_args = re.sub(r'ubo\.resolution\.xy', 'ubo.resolution', render_args)
        
        # Check if function returns vec4 or vec3
        return_match = re.search(r'vec[34]\s+' + render_func, content)
        if return_match:
            render_call = f"vec4 result = {render_func}({render_args});\n    vec3 color = result.rgb;"
        else:
            render_call = f"vec3 color = {render_func}({render_args});"
    else:
        # Fallback: try to find a render function and create simple call
        render_func_match = re.search(r'vec[34]\s+(render\w+)\(', content)
        if render_func_match:
            render_func = render_func_match.group(1)
            render_call = f"vec3 color = {render_func}(uv, ubo.time, ubo.tempo, ubo.energy, ubo.bass, ubo.mid, ubo.high);"
        else:
            render_call = "vec3 color = vec3(0.5);"  # Fallback
    
    # Replace uniforms in extracted functions
    functions = functions.replace('uTime', 'ubo.time')
    functions = functions.replace('uTempo', 'ubo.tempo')
    functions = functions.replace('uEnergy', 'ubo.energy')
    functions = functions.replace('uBass', 'ubo.bass')
    functions = functions.replace('uMid', 'ubo.mid')
    functions = functions.replace('uHigh', 'ubo.high')
    functions = functions.replace('uPrimaryColor', 'ubo.primaryColor')
    functions = functions.replace('uSecondaryColor', 'ubo.secondaryColor')
    functions = functions.replace('uColorBlend', 'ubo.colorBlend')
    functions = functions.replace('uResolution', 'ubo.resolution')
    functions = functions.replace('uIntensity', 'ubo.energy')
    functions = re.sub(r'uResolution\.xy', 'ubo.resolution', functions)
    functions = re.sub(r'uResolution\.x', 'ubo.resolution.x', functions)
    functions = re.sub(r'uResolution\.y', 'ubo.resolution.y', functions)
    
    # Generate compute shader
    compute_shader = COMPUTE_TEMPLATE.format(
        HELPER_FUNCTIONS=HELPER_FUNCTIONS,
        FUNCTIONS=functions,
        RENDER_CALL=render_call
    )
    
    return compute_shader

def main():
    """Process all procedural shaders."""
    os.chdir('/home/ricky/coding/proyects/vulkancpp')
    
    # Get only individual procedural files (exclude packs and helpers)
    procedural_files = []
    for f in PROCEDURAL_DIR.glob("procedural_*.glsl"):
        # Exclude pack files and helpers
        if 'pack' in f.name or 'helper' in f.name or 'header' in f.name or 'main' in f.name:
            continue
        procedural_files.append(f)
    
    print(f"Found {len(procedural_files)} individual procedural shaders (excluding packs)")
    
    for source_file in procedural_files:
        print(f"Processing {source_file.name}...")
        
        try:
            compute_shader = convert_procedural_to_compute(source_file)
            
            # Write compute shader
            output_file = POST_EFFECTS_DIR / f"{source_file.stem}.comp"
            with open(output_file, 'w') as f:
                f.write(compute_shader)
            
            # Compile to SPIR-V
            spv_file = POST_EFFECTS_DIR / f"{source_file.stem}.comp.spv"
            result = subprocess.run(
                ['glslc', '-fshader-stage=compute', str(output_file), '-o', str(spv_file)],
                capture_output=True,
                text=True
            )
            
            if result.returncode == 0:
                print(f"  ✓ Compiled {source_file.name}")
            else:
                print(f"  ✗ Compilation failed for {source_file.name}")
                print(f"    Error: {result.stderr}")
                
        except Exception as e:
            print(f"  ✗ Error processing {source_file.name}: {e}")
    
    print("\nDone!")

if __name__ == "__main__":
    main()
