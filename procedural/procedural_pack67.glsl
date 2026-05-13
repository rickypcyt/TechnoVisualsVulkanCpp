// @EFFECT name="Flow Field" index=93 desc="Perlin noise flow field with triangular particles" author="p5.js port"

// Draw triangle
float drawTriangle(vec2 p, vec2 center, float angle, float base, float height) {
    // Triangle vertices in local space
    vec2 v0 = vec2(base, 0.0);
    vec2 v1 = vec2(-base, 0.0);
    vec2 v2 = vec2(0.0, height);
    
    // Rotate vertices
    v0 = rotate2D(v0, angle);
    v1 = rotate2D(v1, angle);
    v2 = rotate2D(v2, angle);
    
    // Transform to world space
    v0 += center;
    v1 += center;
    v2 += center;
    
    // Edge function for triangle
    vec2 e0 = v1 - v0;
    vec2 e1 = v2 - v1;
    vec2 e2 = v0 - v2;
    
    vec2 p0 = p - v0;
    vec2 p1 = p - v1;
    vec2 p2 = p - v2;
    
    float s0 = sign(e0.x * p0.y - e0.y * p0.x);
    float s1 = sign(e1.x * p1.y - e1.y * p1.x);
    float s2 = sign(e2.x * p2.y - e2.y * p2.x);
    
    // Inside triangle if all signs are same
    float inside = step(0.0, s0 * s1 * s2);
    
    return inside;
}

vec4 renderFlowField(
    vec2  st,
    float time,
    float tempo,
    float energy,
    float bass,
    float mid,
    float high)
{
    vec3 color = vec3(0.0);
    float alpha = 0.0;
    
    // Parameters from p5.js
    float inc = 0.02;
    float scl = 30.0;
    
    // Audio-reactive parameters
    inc *= (0.5 + energy * 0.5);
    scl *= (0.8 + bass * 0.4);
    
    // Calculate grid
    float cols = floor(1.0 / scl);
    float rows = floor(1.0 / scl);
    
    // Background color (dark blue from p5.js)
    vec3 bgColor = vec3(0.0, 0.0, 0.196);
    
    // Time offset for noise evolution
    float zoff = time * 0.5;
    zoff *= (0.5 + mid * 0.5);
    
    float brightness = 0.0;
    
    // Iterate through grid
    for (float y = 0.0; y < rows; y++) {
        for (float x = 0.0; x < cols; x++) {
            float xoff = x * inc;
            float yoff = y * inc;
            
            // Get noise angle
            float angle = snoise(vec2(xoff, yoff + zoff)) * 6.28318;
            
            // Grid cell center
            vec2 center = vec2(x * scl + scl * 0.5, y * scl + scl * 0.5);
            
            // Draw triangle
            float tri = drawTriangle(st, center, angle, 0.01, scl * 0.04);
            
            if (tri > 0.0) {
                // Color (white with transparency from p5.js)
                brightness += tri * 0.8;
            }
        }
    }
    
    brightness = clamp(brightness, 0.0, 1.0);
    
    // Color (white from p5.js)
    color = vec3(brightness);
    
    // Audio-reactive color modulation
    color *= (0.8 + energy * 0.4);
    
    // Add subtle hue shift based on audio
    color = mix(color, vec3(color.r, color.g * 0.8, color.b * 1.2), bass * 0.3);
    
    alpha = brightness + 0.1;
    
    // Background
    color = mix(bgColor, color, clamp(alpha, 0.0, 1.0));
    
    return vec4(color, clamp(alpha + 0.1, 0.0, 1.0));
}
