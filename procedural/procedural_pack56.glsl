// @EFFECT name="Lowres Pixelation" index=82 desc="Rotating 3D box rendered as dot-matrix" author="p5.js port"




// Simple box intersection
float boxIntersect(vec3 ro, vec3 rd, vec3 boxSize) {
    vec3 boxMin = -boxSize * 0.5;
    vec3 boxMax = boxSize * 0.5;
    
    vec3 invDir = 1.0 / max(abs(rd), 1e-6);
    vec3 tMin = (boxMin - ro) * invDir;
    vec3 tMax = (boxMax - ro) * invDir;
    
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);
    
    if (tNear < tFar && tFar > 0.0) {
        return tNear;
    }
    return 1e6;
}

// Calculate box normal
vec3 boxNormal(vec3 p, vec3 boxSize) {
    vec3 boxMin = -boxSize * 0.5;
    vec3 boxMax = boxSize * 0.5;
    
    vec3 normal = vec3(0.0);
    float epsilon = 0.01;
    
    if (abs(p.x - boxMin.x) < epsilon) normal = vec3(-1.0, 0.0, 0.0);
    else if (abs(p.x - boxMax.x) < epsilon) normal = vec3(1.0, 0.0, 0.0);
    else if (abs(p.y - boxMin.y) < epsilon) normal = vec3(0.0, -1.0, 0.0);
    else if (abs(p.y - boxMax.y) < epsilon) normal = vec3(0.0, 1.0, 0.0);
    else if (abs(p.z - boxMin.z) < epsilon) normal = vec3(0.0, 0.0, -1.0);
    else if (abs(p.z - boxMax.z) < epsilon) normal = vec3(0.0, 0.0, 1.0);
    
    return normal;
}

vec4 renderLowresPixelation(
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
    const int maxTiles = 100;
    vec3 boxSize = vec3(30.0, 80.0, 20.0);
    
    // Audio-reactive resolution (10-100 tiles)
    float tiles = mix(10.0, float(maxTiles), energy);
    vec2 gridPos = floor(st * tiles);
    vec2 gridUV = fract(st * tiles);
    
    // Calculate low-res UV coordinates
    vec2 lowResUV = gridPos / tiles;
    
    // Render 3D box to low-res buffer
    vec3 ro = vec3(0.0, 0.0, 150.0);
    vec3 rd = normalize(vec3(lowResUV - 0.5, -1.0));
    
    // Apply rotations from p5.js
    float rotX = radians(time * 1.2);
    float rotY = radians(time * 0.5);
    float rotZ = radians(time * 1.5);
    
    // Audio-reactive rotation speed
    rotX *= (0.5 + bass * 0.5);
    rotY *= (0.5 + mid * 0.5);
    rotZ *= (0.5 + high * 0.5);
    
    ro = rotateZ(rotateY(rotateX(ro, rotX), rotY), rotZ);
    rd = rotateZ(rotateY(rotateX(rd, rotX), rotY), rotZ);
    
    // Intersect with box
    float t = boxIntersect(ro, rd, boxSize);
    
    float brightness = 0.0;
    if (t < 1e5) {
        // Calculate hit point
        vec3 hit = ro + rd * t;
        vec3 normal = boxNormal(hit, boxSize);
        
        // Lighting from p5.js
        vec3 ambient = vec3(0.31); // 80/255
        vec3 lightDir = normalize(vec3(-1.0, 0.0, -1.0));
        vec3 lightColor = vec3(1.0);
        
        float diff = max(dot(normal, lightDir), 0.0);
        vec3 lighting = ambient + lightColor * diff;
        
        brightness = dot(lighting, vec3(0.299, 0.587, 0.114));
    }
    
    // Calculate dot size based on brightness
    float tileW = 1.0 / tiles;
    float dotSize = map(brightness, 0.0, 1.0, 0.0, tileW);
    
    // Draw dot
    vec2 center = gridUV - 0.5;
    float dist = length(center);
    float dotMask = smoothstep(dotSize * 0.5, dotSize * 0.4, dist);

    // Multi-color variation based on position and brightness
    vec3 palette = mix(uPrimaryColor, uSecondaryColor, uColorBlend);

    // Position-based color shift
    float posColor = sin(lowResUV.x * 6.28318 + time) * 0.5 + 0.5;
    float posColor2 = cos(lowResUV.y * 6.28318 + time * 0.7) * 0.5 + 0.5;

    // Mix multiple color sources for variety
    vec3 color1 = mix(vec3(0.2, 0.8, 1.0), vec3(1.0, 0.2, 0.8), brightness);
    vec3 color2 = mix(vec3(1.0, 0.6, 0.2), vec3(0.2, 1.0, 0.6), posColor);
    vec3 color3 = palette * (1.0 + posColor2);

    // Combine colors with brightness modulation
    vec3 dotColor = mix(color1, color2, 0.4);
    dotColor = mix(dotColor, color3, brightness * 0.5);

    // Audio-reactive brightness per channel
    dotColor.r *= (0.8 + energy * 0.5) * (1.0 + bass * 0.3);
    dotColor.g *= (0.8 + energy * 0.4) * (1.0 + mid * 0.2);
    dotColor.b *= (0.8 + energy * 0.6) * (1.0 + high * 0.4);

    color = dotColor * dotMask;
    alpha = dotMask;
    
    // Background (dark blue-gray)
    vec3 bgColor = vec3(0.05, 0.05, 0.1);
    color = mix(bgColor, color, alpha);
    
    return vec4(color, alpha);
}
