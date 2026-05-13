// @EFFECT name="Curved Lines" index=89 desc="Circular pattern with rotating ellipses based on tangent angles" author="p5.js port"



vec4 renderCurvedLines(
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
    float radius = 200.0;
    float step = 0.1;
    float ellipseWidth = 400.0;
    float ellipseHeight = 1.0;
    
    // Audio-reactive parameters
    radius *= (0.8 + energy * 0.4);
    ellipseWidth *= (0.8 + bass * 0.4);
    
    // Center coordinates
    vec2 center = vec2(0.5);
    vec2 fromCenter = st - center;
    
    // Time variable
    float t = time * 0.01;
    
    // Sine wave modulation (0.9 to 2)
    float s = mix(0.9, 2.0, (sin(t) + 1.0) * 0.5);
    s *= (0.5 + mid * 0.5);
    
    // Initialize points on circle
    float brightness = 0.0;
    const int numPoints = 63; // TWO_PI / 0.1
    
    for (int i = 0; i < numPoints; i++) {
        float angle = float(i) * step;
        
        // Current point on circle
        vec2 pos = vec2(cos(angle), sin(angle)) * radius;
        
        // Next point on circle
        float nextAngle = float(i + 1) * step;
        vec2 nextPos = vec2(cos(nextAngle), sin(nextAngle)) * radius;
        
        // Calculate angle to next point (atan2)
        float a = atan(nextPos.y - pos.y, nextPos.x - pos.x);
        
        // Add sine modulation
        a += s;
        
        // Draw ellipse (400x1, essentially a line)
        // The ellipse is centered at pos, rotated by angle a
        vec2 ellipseCenter = pos * 0.001; // Scale to UV space
        vec2 ellipseSize = vec2(ellipseWidth, ellipseHeight) * 0.001;
        
        // Rotate the sample point into ellipse local space
        vec2 localPos = rotate2D(fromCenter - ellipseCenter, -a);
        
        // Check if point is within ellipse
        vec2 normalizedPos = localPos / ellipseSize;
        float ellipseDist = length(normalizedPos);
        
        if (ellipseDist < 1.0) {
            // Add soft edge
            float edge = 1.0 - smoothstep(0.8, 1.0, ellipseDist);
            brightness += edge * 0.5;
        }
        
        // Also draw line representation for thin ellipses
        vec2 lineDir = vec2(cos(a), sin(a));
        float lineLen = ellipseWidth * 0.001;
        vec2 lineStart = ellipseCenter - lineDir * lineLen * 0.5;
        vec2 lineEnd = ellipseCenter + lineDir * lineLen * 0.5;
        
        vec2 lineVec = lineEnd - lineStart;
        float lineLenActual = length(lineVec);
        if (lineLenActual > 0.001) {
            vec2 lineNorm = lineVec / lineLenActual;
            vec2 toSample = fromCenter - lineStart;
            float proj = clamp(dot(toSample, lineNorm), 0.0, lineLenActual);
            vec2 closest = lineStart + lineNorm * proj;
            float lineDist = length(fromCenter - closest);
            
            if (lineDist < 0.002) {
                brightness += (1.0 - smoothstep(0.0, 0.002, lineDist)) * 0.8;
            }
        }
    }
    
    brightness = clamp(brightness, 0.0, 1.0);
    
    // Color (white)
    color = vec3(brightness);
    
    // Audio-reactive color modulation
    color *= (0.8 + energy * 0.4);
    
    // Add subtle hue shift based on high frequencies
    color = mix(color, vec3(brightness * 0.8, brightness * 0.5, brightness * 1.0), high * 0.3);
    
    alpha = brightness + 0.1;
    
    // Background
    vec3 bgColor = vec3(0.0);
    color = mix(bgColor, color, clamp(alpha, 0.0, 1.0));
    
    return vec4(color, clamp(alpha + 0.1, 0.0, 1.0));
}
