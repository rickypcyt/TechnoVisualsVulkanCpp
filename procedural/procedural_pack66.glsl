// @EFFECT name="Koch Snowflake" index=92 desc="Koch snowflake fractal with recursive subdivision" author="p5.js port"



// Line distance with glow
float lineGlow(vec2 p, vec2 a, vec2 b, float width) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    float d = length(pa - ba * h);
    return smoothstep(width, width * 0.5, d);
}

// Circle distance with glow
float circleGlow(vec2 p, vec2 center, float radius) {
    float d = length(p - center);
    return smoothstep(radius, radius * 0.5, d);
}

// Koch subdivision - subdivide line into 4 segments with triangular bump
vec2 kochSubdivide(vec2 p0, vec2 p1, float t, float rotAngle) {
    vec2 v = p1 - p0;
    vec2 p = p0 + v * t;
    
    // Add triangular bump at t = 0.5
    if (t >= 0.33 && t <= 0.67) {
        vec2 mid = p0 + v * 0.5;
        vec2 perp = vec2(-v.y, v.x);
        perp = normalize(perp) * length(v) * 0.2887; // tan(30°) / 3
        perp = rotate2D(perp, rotAngle);
        p = mid + perp * (t - 0.5) * 3.0;
    }
    
    return p;
}

vec4 renderKochSnowflake(
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

    // Early exit for pixels outside viewport (optimization)
    vec2 center = vec2(0.5);
    vec2 fromCenter = st - center;
    float dist = length(fromCenter);
    if (dist > 0.5) {
        return vec4(0.0, 0.0, 0.0, 0.0);
    }

    // Parameters from p5.js - heavily reduced for performance
    int angle = 3; // Number of sides (triangle by default)
    int layer = 1; // Recursion depth - reduced to 1 for performance

    // Audio-reactive parameters - limited range
    angle = int(3.0 + bass * 2.0);  // Reduced from 3.0
    layer = int(1.0 + mid * 0.5);   // Reduced base to 1
    if (layer > 2) layer = 2;       // Hard cap at 2 for performance
    if (angle > 8) angle = 8;       // Cap vertices

    float radius = 0.3;  // Reduced from 0.35
    radius *= (0.8 + energy * 0.3);  // Reduced energy multiplier

    // Rotation for the peak point (simulating mouse interaction)
    float rotAngle = time * 0.3 + energy * 0.5;  // Slower rotation

    float brightness = 0.0;

    // Generate initial polygon vertices - reduced max
    const int maxVertices = 8;  // Reduced from 15
    vec2 vertices[maxVertices];
    int vertexCount = angle;

    for (int i = 0; i < maxVertices; i++) {
        if (i >= angle) break;
        float a = float(i) * (6.28318 / float(angle)) + 1.5708; // Start at top
        vertices[i] = vec2(cos(a), sin(a)) * radius;
    }

    // Draw Koch snowflake - simplified approach
    const int samplesPerLine = 10;  // Aggressively reduced from 25

    for (int i = 0; i < maxVertices; i++) {
        if (i >= vertexCount) break;

        vec2 p0 = vertices[i];
        vec2 p1 = vertices[(i + 1) % vertexCount];

        // Draw the main line only (removed expensive point sampling loop)
        float line = lineGlow(fromCenter, p0, p1, 0.004);
        brightness += line * 0.7;

        // Draw circles at endpoints only
        float circle0 = circleGlow(fromCenter, p0, 0.006);
        float circle1 = circleGlow(fromCenter, p1, 0.006);
        brightness += (circle0 + circle1) * 0.4;
    }

    // Simplified detail - only for layer 2
    if (layer >= 2) {
        float scale = 0.33;

        for (int i = 0; i < maxVertices; i++) {
            if (i >= vertexCount) break;

            vec2 p0 = vertices[i];
            vec2 p1 = vertices[(i + 1) % vertexCount];
            vec2 mid = p0 + (p1 - p0) * 0.5;

            // Add triangular bump
            vec2 perp = vec2(-(p1.y - p0.y), p1.x - p0.x);
            perp = normalize(perp) * length(p1 - p0) * 0.2887 * scale;
            perp = rotate2D(perp, rotAngle);
            vec2 peak = mid + perp;

            // Draw bump - simplified to single line
            float bumpLine = lineGlow(fromCenter, mid, peak, 0.003);
            brightness += bumpLine * 0.25;

            // Draw circle at peak
            float peakCircle = circleGlow(fromCenter, peak, 0.004);
            brightness += peakCircle * 0.2;
        }
    }

    brightness = clamp(brightness, 0.0, 1.0);

    // Color (white to gray gradient based on distance)
    vec3 startCol = vec3(1.0);
    vec3 endCol = vec3(0.4, 0.4, 0.4);  // Slightly brighter
    vec3 col = mix(startCol, endCol, smoothstep(0.0, 0.3, dist));

    color = col * brightness;

    // Audio-reactive color modulation
    color *= (0.9 + energy * 0.3);  // Slightly brighter base

    // Add subtle hue shift based on audio
    color = mix(color, vec3(color.r * 0.8, color.g, color.b * 1.2), bass * 0.2);  // Reduced effect

    alpha = brightness + 0.15;  // Slightly higher base alpha

    // Background
    vec3 bgColor = vec3(0.0);
    color = mix(bgColor, color, clamp(alpha, 0.0, 1.0));

    return vec4(color, clamp(alpha + 0.1, 0.0, 1.0));
}
