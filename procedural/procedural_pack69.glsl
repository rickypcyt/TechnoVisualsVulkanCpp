// @EFFECT name="Wave Circles" index=95 desc="Concentric wave circles with boxes" author="p5.js port"

// Project 3D point to 2D with rotation
vec2 project3D(vec3 p, float rotation) {
    // Rotate around Y axis
    float c = cos(rotation);
    float s = sin(rotation);
    
    float x = p.x * c - p.z * s;
    float z = p.x * s + p.z * c;
    float y = p.y;
    
    // Simple perspective projection
    float fov = 2.0;
    float dist = 3.0;
    float scale = fov / (dist + z);
    
    return vec2(x * scale, y * scale);
}

// HSB to RGB conversion
vec3 hsbToRgb(vec3 hsb) {
    vec3 rgb = clamp(abs(mod(hsb.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    rgb = rgb * rgb * (3.0 - 2.0 * rgb);
    return hsb.z * mix(vec3(1.0), rgb, hsb.y);
}

vec4 renderWaveCircles(
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

    // Parameters from original code - optimized
    int layerNum = 15;  // Slightly increased for more detail
    float distance = 0.018;
    int boxNum = 24;  // Slightly increased
    float amplitude = 0.18;
    float waveSpeed = 1.0;

    // Audio-reactive parameters
    layerNum = int(float(layerNum) * (0.8 + energy * 0.5));
    distance *= (0.8 + bass * 0.5);
    amplitude *= (0.8 + mid * 0.5);
    waveSpeed *= (0.5 + high * 0.6);

    // Center coordinates
    vec2 center = vec2(0.5);
    vec2 uv = (st - center) * 2.0;

    float brightness = 0.0;
    float glowAccum = 0.0;

    // User palette for vibrant colors
    vec3 palette = mix(uPrimaryColor, uSecondaryColor, uColorBlend);

    // Scene rotation
    float rotation = time * waveSpeed * 12.0;
    rotation += bass * 6.0;

    // Draw boxes in concentric layers
    for (int layer = 1; layer <= 15; layer++) {
        if (layer > layerNum) continue;

        // Layer radius
        float r = float(layer) * distance;

        // Draw boxes on this layer
        for (int i = 0; i < 24; i++) {
            float theta = float(i) * (360.0 / float(boxNum));

            // Convert spherical to rectangular
            vec3 posi = sphericalToRectangular(r, theta, 90.0);

            // Enhanced wave motion with multiple frequencies
            float wavePhase1 = waveSpeed * time * 60.0 + float(layer) / float(layerNum) * 360.0;
            float wavePhase2 = waveSpeed * time * 40.0 + float(i) / float(boxNum) * 360.0;
            posi.y += amplitude * (sin(radians(wavePhase1)) * 0.7 + sin(radians(wavePhase2)) * 0.3);

            // Apply audio-reactive offset
            posi.x += sin(time * 1.5 + float(layer) * 0.5) * bass * 0.06;
            posi.z += cos(time * 1.2 + float(i) * 0.5) * mid * 0.06;
            posi.y += sin(time * 2.0 + float(layer) * 0.3) * high * 0.04;

            // Project to 2D
            vec2 projected = project3D(posi, rotation);

            // Box size based on layer - variable size for visual interest
            float boxSize = (360.0 / float(boxNum) * 0.9 / 360.0 * 3.14159 * r) * 0.6;
            boxSize *= (0.8 + energy * 0.4);

            // Draw box with enhanced glow
            float distToBox = length(uv - projected);
            float boxHit = smoothstep(boxSize, boxSize * 0.4, distToBox);
            float glowHit = smoothstep(boxSize * 2.0, boxSize * 0.8, distToBox);

            if (boxHit > 0.0 || glowHit > 0.0) {
                // Vibrant color using user palette + HSB
                float hue = map(float(layer), 0.0, float(layerNum), 0.0, 1.0);
                hue += time * 0.05; // Slow hue rotation
                float saturation = 0.85 + (1.0 - 0.85) * sin(radians(mod(time * 40.0, 360.0)));
                saturation *= (0.8 + energy * 0.5);
                float brightnessVal = 1.2 + (0.3) * cos(radians(mod(time * 30.0, 360.0)));
                brightnessVal *= (0.9 + high * 0.5);

                vec3 hsb = vec3(hue, saturation, brightnessVal);
                vec3 boxColorHSB = hsbToRgb(hsb);

                // Mix HSB color with user palette for vibrancy
                vec3 boxColor = mix(boxColorHSB, palette * 1.8, 0.4);

                // Audio-reactive color modulation
                boxColor *= (1.1 + energy * 0.5);
                boxColor.r *= (1.0 + bass * 0.3);
                boxColor.g *= (1.0 + mid * 0.2);
                boxColor.b *= (1.0 + high * 0.4);

                brightness += boxHit * 0.7;
                glowAccum += glowHit * 0.3;
                color += boxColor * (boxHit + glowHit * 0.5);
            }
        }
    }

    brightness = clamp(brightness, 0.0, 1.0);
    glowAccum = clamp(glowAccum, 0.0, 1.0);
    color = clamp(color, 0.0, 1.0);

    // Add glow effect
    color += palette * glowAccum * 0.4 * (1.0 + energy * 0.3);

    // Darker background for contrast
    vec3 bgColor = vec3(0.02, 0.02, 0.08);
    color = mix(bgColor, color, brightness + glowAccum * 0.5);

    alpha = brightness + glowAccum * 0.3 + 0.15;

    return vec4(color, clamp(alpha, 0.0, 1.0));
}
