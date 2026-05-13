// @EFFECT name="Collatz Spiral" index=85 desc="3n+1 conjecture visualization with dual spiral cycles" author="p5.js port"

// Helper function to calculate distance from point to line segment
float distToLine(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    vec2 ap = p - a;
    float t = clamp(dot(ap, ab) / dot(ab, ab), 0.0, 1.0);
    vec2 closest = a + t * ab;
    return length(p - closest);
}

vec4 renderCollatzSpiral(
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
    float velocity = 12.0;
    float evenAngle = 0.24;
    float oddAngle = -0.47;
    float segmentLength = 2.0;
    
    // Audio-reactive parameters
    velocity *= (0.5 + bass * 0.5);
    segmentLength *= (0.8 + energy * 0.4);
    evenAngle *= (0.8 + mid * 0.4);
    oddAngle *= (0.8 + high * 0.4);
    
    // Center position
    vec2 center = vec2(0.5);
    vec2 fromCenter = st - center;
    float dist = length(fromCenter);
    
    // Calculate current number based on time
    float currentNumber = 3.0 + floor(time * 0.1);
    currentNumber = mod(currentNumber, 100.0) + 3.0;
    
    // Simulate Collatz sequence
    float hailstoneA = currentNumber;
    float hailstoneB = currentNumber;
    float angleA = 0.0;
    float angleB = 0.0;
    vec2 pos = vec2(0.0);
    
    // Trace the spiral
    float brightness = 0.0;
    const int maxSteps = 100;
    
    for (int i = 0; i < maxSteps; i++) {
        if (hailstoneA > 1.0 && hailstoneA < 1e6) {
            // Cycle A (white, outward)
            float parityA = mod(hailstoneA, 2.0);
            if (parityA == 0.0) {
                hailstoneA *= 0.5;
                angleA += evenAngle;
            } else {
                hailstoneA = 3.0 * hailstoneA + 1.0;
                angleA += oddAngle;
            }
            
            // Calculate line segment
            vec2 dirA = vec2(cos(angleA), -sin(angleA));
            vec2 lineStart = pos;
            vec2 lineEnd = pos + dirA * segmentLength * 0.01;
            
            // Check if our sample point is near this line
            float lineDist = distToLine(fromCenter, lineStart, lineEnd);
            if (lineDist < 0.005) {
                brightness += 1.0 - smoothstep(0.0, 0.005, lineDist);
            }
            
            pos += dirA * segmentLength * 0.01;
        }
    }
    
    // Reset for cycle B
    pos = vec2(0.0);
    for (int i = 0; i < maxSteps; i++) {
        if (hailstoneB > 1.0 && hailstoneB < 1e6) {
            // Cycle B (black, inward)
            float parityB = mod(hailstoneB, 2.0);
            if (parityB == 0.0) {
                hailstoneB *= 0.5;
                angleB -= evenAngle;
            } else {
                hailstoneB = 3.0 * hailstoneB + 1.0;
                angleB -= oddAngle;
            }
            
            // Calculate line segment (reverse direction)
            vec2 dirB = vec2(-cos(angleB), -sin(angleB));
            vec2 lineStart = pos;
            vec2 lineEnd = pos + dirB * segmentLength * 0.01;
            
            // Check if our sample point is near this line
            float lineDist = distToLine(fromCenter, lineStart, lineEnd);
            if (lineDist < 0.005) {
                brightness -= 0.5 - smoothstep(0.0, 0.005, lineDist);
            }
            
            pos += dirB * segmentLength * 0.01;
        }
    }
    
    brightness = clamp(brightness, -0.5, 1.0);
    
    // Background color (dark instead of white)
    vec3 bgColor = vec3(0.05, 0.05, 0.08);
    
    // Spiral color based on brightness (cyan/magenta for positive, dark for negative)
    vec3 spiralColor = mix(bgColor, vec3(0.2, 0.8, 1.0), smoothstep(0.0, 0.5, brightness));
    spiralColor = mix(spiralColor, vec3(1.0, 0.2, 0.8), smoothstep(0.5, 1.0, brightness));
    
    // Audio-reactive color modulation
    spiralColor *= (0.8 + energy * 0.4);
    
    // Add glow effect
    float glow = smoothstep(0.0, 0.15, abs(brightness));
    spiralColor += vec3(0.3, 0.1, 0.5) * energy * glow;
    
    // Mix with background
    color = mix(bgColor, spiralColor, 0.5 + 0.5 * abs(brightness));
    
    return vec4(color, 1.0);
}
