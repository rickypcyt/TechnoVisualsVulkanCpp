// @EFFECT name="Bezier Petals" index=91 desc="Rotating petal pattern with bezier curves" author="p5.js port"



// Cubic bezier curve distance estimation
float bezierDistance(vec2 p, vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    // Simple approach: sample along bezier curve and find closest point
    float minDist = 1000.0;
    const int samples = 20;
    
    for (int i = 0; i <= samples; i++) {
        float t = float(i) / float(samples);
        
        // Cubic bezier formula
        float mt = 1.0 - t;
        vec2 bp = mt * mt * mt * p0 + 
                  3.0 * mt * mt * t * p1 + 
                  3.0 * mt * t * t * p2 + 
                  t * t * t * p3;
        
        float d = length(p - bp);
        minDist = min(minDist, d);
    }
    
    return minDist;
}

vec4 renderBezierPetals(
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
    int petals = 8;
    float maxJ = 200.0;
    float jStep = 25.0;
    
    // Audio-reactive parameters
    petals = int(8.0 + bass * 4.0);
    maxJ *= (0.8 + energy * 0.4);
    
    // Center coordinates
    vec2 center = vec2(0.5);
    vec2 fromCenter = st - center;
    
    // Frame count equivalent (60 fps)
    float frameCount = time * 60.0;
    
    // Calculate bezier control points
    float x = sin(radians(frameCount)) * 0.5;
    float y = cos(radians(frameCount * 0.7)) * 0.5;
    float x2 = sin(radians(frameCount * 0.3)) * 0.5;
    float y2 = cos(radians(frameCount * 1.1)) * 0.5;
    float x3 = sin(radians(frameCount * 0.2)) * 0.5;
    
    // Audio-reactive modulation
    x *= (0.5 + mid * 0.5);
    y *= (0.5 + mid * 0.5);
    x2 *= (0.5 + high * 0.5);
    y2 *= (0.5 + high * 0.5);
    x3 *= (0.5 + bass * 0.5);
    
    float brightness = 0.0;
    
    // Iterate through petals
    for (int d = 0; d < 360; d += (360 / 8)) {
        float angle = radians(float(d));
        
        // Rotate sample point into petal space
        vec2 rotatedPos = rotate2D(fromCenter, -angle);
        
        // Iterate through bezier curves
        for (float j = 0.0; j < maxJ; j += jStep) {
            // Bezier control points in UV space
            vec2 p0 = vec2(0.0);
            vec2 p1 = vec2(x, y);
            vec2 p2 = vec2(x3, j * 0.001);
            vec2 p3 = vec2(x2, y2);
            
            // Calculate distance to bezier curve
            float dist = bezierDistance(rotatedPos, p0, p1, p2, p3);
            
            // Add glow based on distance
            if (dist < 0.005) {
                float glow = (1.0 - smoothstep(0.0, 0.005, dist)) * 0.3;
                brightness += glow;
            }
        }
    }
    
    brightness = clamp(brightness, 0.0, 1.0);

    // Bright vibrant colors using user palette
    vec3 palette = mix(uPrimaryColor, uSecondaryColor, uColorBlend);

    // Base color with increased brightness
    color = mix(vec3(brightness), palette * 1.5, 0.6);

    // Audio-reactive color modulation - much brighter
    color *= (1.2 + energy * 0.6);

    // Add vibrant hue shift based on audio
    color = mix(color, vec3(brightness * 1.2, brightness * 0.9, brightness * 1.5) * palette, bass * 0.5);

    // Add extra glow on high frequencies
    color += vec3(brightness * 0.3, brightness * 0.2, brightness * 0.4) * high * 0.8;

    alpha = brightness + 0.15;
    
    // Background
    vec3 bgColor = vec3(0.0);
    color = mix(bgColor, color, clamp(alpha, 0.0, 1.0));
    
    return vec4(color, clamp(alpha + 0.1, 0.0, 1.0));
}
