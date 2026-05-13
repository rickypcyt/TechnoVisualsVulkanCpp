// @EFFECT name="Noise Dot Grid" index=76 desc="Grid of circles with Perlin noise-controlled sizes" author="p5.js port"

vec4 renderNoiseDotGrid(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Noise scales from p5.js
    float xScale = 0.015;
    float yScale = 0.02;
    
    // Gap between dots (can be modulated by audio)
    float gap = 0.1 + bass * 0.05;
    
    // Offset for noise animation
    float offset = time * 0.5;
    
    vec3 color = vec3(0.0);
    
    // Calculate grid position
    vec2 gridPos = st * 2.0; // Scale to cover -1 to 1 range
    
    // Calculate grid cell
    vec2 cell = floor(gridPos / gap);
    vec2 cellUV = mod(gridPos, gap) / gap - 0.5;
    
    // Calculate noise value using scaled and offset coordinates (use vec2)
    vec2 noiseCoords = vec2((cell.x + offset) * xScale, (cell.y + offset) * yScale);
    float noiseValue = noise(noiseCoords);
    
    // Calculate diameter based on noise value (increased size multiplier)
    float diameter = noiseValue * gap * 2.5;
    
    // Calculate distance from cell center
    float dist = length(cellUV);
    
    // Create circle
    float circle = smoothstep(diameter, diameter * 0.8, dist);
    
    // Color based on audio and noise
    vec3 dotColor = vec3(
        0.5 + 0.5 * sin(noiseValue * 6.28 + time),
        0.5 + 0.5 * sin(noiseValue * 6.28 + time * 1.3),
        0.5 + 0.5 * sin(noiseValue * 6.28 + time * 1.7)
    );
    
    // Audio reactivity
    dotColor *= 0.8 + bass * 0.4;
    
    // Add glow
    float glow = smoothstep(diameter * 1.5, diameter * 0.5, dist);
    dotColor += glow * 0.3 * vec3(0.2, 0.4, 0.8);
    
    color = dotColor * circle;
    
    return vec4(color, 1.0);
}
