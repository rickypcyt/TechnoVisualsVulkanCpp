// @EFFECT name="Fibonacci Curl" index=84 desc="Fibonacci spiral with 3D rotation" author="p5.js port"





// Draw arc segment with line
float drawArc(vec2 p, float radius, float startAngle, float endAngle, float lineWidth) {
    vec2 toP = p;
    float dist = length(toP);
    float angle = atan(toP.y, toP.x);
    
    // Normalize angle to [0, 2PI]
    if (angle < 0.0) angle += 6.28318;
    
    // Normalize start/end angles
    float start = mod(startAngle, 6.28318);
    float end = mod(endAngle, 6.28318);
    
    // Check if angle is within arc
    bool inAngle = false;
    if (start < end) {
        inAngle = angle >= start && angle <= end;
    } else {
        inAngle = angle >= start || angle <= end;
    }
    
    // Distance from arc - increased line width
    float distFromArc = abs(dist - radius);
    
    // Arc stroke - thicker line
    if (inAngle && distFromArc < lineWidth) {
        return 1.0 - smoothstep(0.0, lineWidth, distFromArc);
    }
    
    return 0.0;
}

// Draw point at position
float drawPoint(vec2 p, vec2 pointPos, float radius) {
    float dist = length(p - pointPos);
    return smoothstep(radius, radius * 0.5, dist);
}

vec4 renderFibonacciCurl(
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
    const int fibCount = 15;
    const float halfPi = 1.5708;
    
    // Audio-reactive rotation
    float dim = 2.0 * 6.28318 * sin(time / 300.0);
    dim *= (0.5 + bass * 0.5);
    
    // Audio-reactive arc type cycling
    float typeCycle = mod(floor(time * 0.2), 3.0);
    int arcType = int(typeCycle);
    
    // Generate Fibonacci sequence
    float fib[15];
    fib[0] = 0.0;
    fib[1] = 1.0;
    for (int i = 2; i < fibCount; i++) {
        fib[i] = fib[i - 1] + fib[i - 2];
    }
    
    // Scale factor
    float scale = 0.005;
    scale *= (0.8 + energy * 0.4);
    
    // Calculate total arc contribution
    float arcBrightness = 0.0;
    
    // Transform screen position to 3D space
    vec3 p = vec3(st * 2.0, 0.0);
    
    // Apply 3D rotations from p5.js
    p = rotateZ(p, dim / 2.0);
    p = rotateY(p, dim / 2.0);
    p = rotateX(p, dim / 2.0);
    
    // Use 2D projection for arc drawing
    vec2 pos2D = p.xy;
    
    // Draw Fibonacci curl
    vec2 currentPos = vec2(0.0);
    float currentAngle = 0.0;
    
    for (int i = 0; i < fibCount; i++) {
        float r = fib[i] * abs(dim) * scale;
        
        // Translation offset (from p5.js)
        if (i > 2) {
            float offset = -(fib[i - 1] * abs(dim) * scale) / 2.0 + (fib[i - 3] * abs(dim) * scale) / 2.0;
            currentPos += vec2(offset, 0.0);
        }
        
        // Draw arc segment with line - emphasized
        float arc = drawArc(pos2D - currentPos, r, currentAngle, currentAngle + halfPi, 0.08 * scale);  // Increased line width
        arcBrightness += arc * 1.5;  // Increased line brightness

        // Removed point rendering to emphasize lines
        // vec2 arcStart = currentPos + vec2(cos(currentAngle), sin(currentAngle)) * r;
        // vec2 arcEnd = currentPos + vec2(cos(currentAngle + halfPi), sin(currentAngle + halfPi)) * r;
        // arcBrightness += drawPoint(pos2D, arcStart, 0.015 * scale);
        // arcBrightness += drawPoint(pos2D, arcEnd, 0.015 * scale);
        
        // Rotate for next segment
        currentAngle += halfPi;
        currentPos = rotate2D(currentPos, halfPi);
    }
    
    arcBrightness = clamp(arcBrightness, 0.0, 1.0);
    
    // White color from p5.js
    vec3 arcColor = vec3(1.0);
    
    // Audio-reactive color
    arcColor *= (0.8 + energy * 0.4);
    
    // Add subtle glow
    arcColor += vec3(0.2) * energy * arcBrightness;
    
    color = arcColor * arcBrightness;
    alpha = arcBrightness * 0.5 + 0.1;
    
    // Background
    vec3 bgColor = vec3(0.0);
    color = mix(bgColor, color, alpha);
    
    return vec4(color, alpha + 0.1);
}
