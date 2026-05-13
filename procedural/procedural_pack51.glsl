// @EFFECT name="Recursive Grids" index=77 desc="Recursive square grids with mouse-reactive rotation" author="#WCCChallenge p5.js port"

vec4 renderRecursiveGrids(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec3 color = vec3(0.0);
    
    // Dark background
    vec3 bgColor = vec3(0.05, 0.05, 0.08);
    color = bgColor;
    
    // Use camera offset as mouse position equivalent
    vec2 shift = vec2(uCameraOffsetX, uCameraOffsetY) * 0.5;
    
    // Time-based rotation speed
    float rotationSpeed = 0.5 + bass * 0.5;
    float timeRotation = time * rotationSpeed;
    
    // Grid positions (3x3 grid at -0.3, 0, 0.3)
    for (int gy = -1; gy <= 1; gy++) {
        for (int gx = -1; gx <= 1; gx++) {
            vec2 gridPos = vec2(float(gx) * 0.3, float(gy) * 0.3);
            
            // Initial square size and position
            float d = 0.29;
            vec2 pos = gridPos;
            
            // Recursive/iterative square drawing
            const int maxIterations = 8;
            float s = 10.0;
            
            for (int i = 0; i < maxIterations; i++) {
                if (d < 0.016) break;
                
                // Calculate rotation with time-based animation
                vec2 toShift = pos - shift;
                float reach = atan(toShift.y, toShift.x) + timeRotation + float(i) * 0.5;
                
                // Color gradient based on iteration level (cyan to magenta)
                vec3 gridColor = mix(vec3(0.2, 0.8, 1.0), vec3(1.0, 0.2, 0.8), float(i) / float(maxIterations));
                
                // Draw square at current position
                vec2 rectSt = (st - pos) / d;
                float rect = smoothstep(0.5, 0.48, max(abs(rectSt.x), abs(rectSt.y)));
                
                // Audio-reactive brightness
                float brightness = (0.6 + energy * 0.4);
                color += gridColor * rect * brightness * 0.2;
                
                // Calculate next position and size
                pos = pos - vec2(cos(reach), sin(reach)) * s * 0.5 * d;
                d = (s - 1.0) * d / s;
            }
        }
    }
    
    // Add subtle glow in center
    float centerGlow = (1.0 - smoothstep(0.0, 0.3, length(st))) * energy * 0.2;
    color += vec3(0.3, 0.1, 0.5) * centerGlow;
    
    return vec4(color, 1.0);
}
