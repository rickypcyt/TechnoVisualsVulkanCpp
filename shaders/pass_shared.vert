#version 450

// Shared vertex shader for all multi-pass rendering stages
// Outputs fullscreen quad with UV coordinates using gl_VertexIndex

layout(location = 0) out vec2 uv;

void main() {
    // Generate fullscreen quad using gl_VertexIndex
    // 0: (-1, -1), (0, 0)
    // 1: ( 1, -1), (1, 0)
    // 2: (-1,  1), (0, 1)
    // 3: ( 1,  1), (1, 1)
    
    vec2 positions[4] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0,  1.0)
    );
    
    vec2 texCoords[4] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0)
    );
    
    vec2 pos = positions[gl_VertexIndex];
    uv = texCoords[gl_VertexIndex];
    
    gl_Position = vec4(pos, 0.0, 1.0);
}
