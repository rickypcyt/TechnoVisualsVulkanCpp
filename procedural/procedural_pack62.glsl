// @EFFECT name="Rotating Circle" index=88 desc="Rotating circular pattern with noise-based line connections" author="p5.js port"



vec4 renderRotatingCircle(
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
    float radius = 250.0;
    float numPoints = 63.0; // TWO_PI / 0.1
    float step = 0.1;
    float rotationSpeed = 0.04;
    
    // Audio-reactive parameters
    radius *= (0.8 + energy * 0.4);
    rotationSpeed *= (0.5 + bass * 0.5);
    
    // Center coordinates
    vec2 center = vec2(0.5);
    vec2 fromCenter = st - center;
    
    // Periodic reset (every 750 frames ~ 12.5 seconds)
    float cycleTime = 12.5;
    float cycle = floor(time / cycleTime);
    float localTime = mod(time, cycleTime);
    
    // Random rotation angle for this cycle
    float a = 3.14159 / floor(hash21(vec2(cycle, 0.0)) * 4.0 + 2.0);
    
    // Overall rotation
    float overallRotation = rotationSpeed * time * 60.0;
    
    // Initialize points on circle
    float brightness = 0.0;
    
    for (float i = 0.0; i < 6.28318; i += step) {
        int idx = int(i / step);
        
        // Initial position on circle
        vec2 pos = vec2(cos(i), sin(i)) * radius;
        
        // Apply individual rotation
        pos = rotate2D(pos, a * idx);
        
        // Apply noise-based movement
        float t = localTime * 0.01 * 60.0;
        pos.x += noise(vec2(t, float(idx) * 0.1)) * 2.0;
        pos.y += noise(vec2(t + 10.0, float(idx) * 0.1)) * 2.0;
        
        // Apply overall rotation
        pos = rotate2D(pos, overallRotation);
        
        // Find mapped index (reverse order)
        float j = mix(numPoints - 1.0, 0.0, i / 6.28318);
        int jIdx = int(j);
        
        // Get mapped position
        vec2 mappedPos = vec2(cos(j * step), sin(j * step)) * radius;
        mappedPos = rotate2D(mappedPos, a * jIdx);
        mappedPos.x += noise(vec2(t, float(jIdx) * 0.1)) * 2.0;
        mappedPos.y += noise(vec2(t + 10.0, float(jIdx) * 0.1)) * 2.0;
        mappedPos = rotate2D(mappedPos, overallRotation);
        
        // Draw line between points
        vec2 lineStart = pos * 0.001; // Scale to UV space
        vec2 lineEnd = mappedPos * 0.001;
        
        // Calculate distance from sample point to line
        vec2 lineDir = lineEnd - lineStart;
        float lineLen = length(lineDir);
        if (lineLen > 0.001) {
            vec2 lineNorm = lineDir / lineLen;
            vec2 toSample = fromCenter - lineStart;
            float proj = clamp(dot(toSample, lineNorm), 0.0, lineLen);
            vec2 closest = lineStart + lineNorm * proj;
            float dist = length(fromCenter - closest);
            
            if (dist < 0.003) {
                // Add curve effect with random offset
                float curveOffset = (noise(vec2(i, time)) - 0.5) * 0.006;
                dist += abs(curveOffset);
                
                if (dist < 0.003) {
                    brightness += (1.0 - smoothstep(0.0, 0.003, dist)) * 0.8;
                }
            }
        }
        
        // Draw points
        float pointDist = length(fromCenter - pos * 0.001);
        if (pointDist < 0.005) {
            brightness += (1.0 - smoothstep(0.0, 0.005, pointDist)) * 0.5;
        }
    }
    
    brightness = clamp(brightness, 0.0, 1.0);
    
    // Color (white with subtle tint)
    color = vec3(brightness);
    
    // Audio-reactive color modulation
    color *= (0.8 + energy * 0.4);
    
    // Add subtle hue shift based on bass
    color = mix(color, vec3(brightness * 0.5, brightness * 0.3, brightness * 0.8), bass * 0.3);
    
    alpha = brightness + 0.1;
    
    // Background
    vec3 bgColor = vec3(0.0);
    color = mix(bgColor, color, clamp(alpha, 0.0, 1.0));
    
    return vec4(color, clamp(alpha + 0.1, 0.0, 1.0));
}
