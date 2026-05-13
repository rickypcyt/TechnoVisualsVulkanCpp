// @EFFECT name="Recursive Subdivision" index=81 desc="Quadtree subdivision with 3D boxes" author="p5.js port"


// Gaussian random approximation
float randomGaussian(vec2 p) {
    float u1 = hash21(p);
    float u2 = hash21(p + 100.0);
    return sqrt(-2.0 * log(u1 + 0.001)) * cos(6.28318 * u2);
}



// Draw a 3D box (simplified ray intersection)
float drawBox(vec3 ro, vec3 rd, vec3 boxPos, vec3 boxSize) {
    vec3 boxMin = boxPos - boxSize * 0.5;
    vec3 boxMax = boxPos + boxSize * 0.5;
    
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


vec4 renderRecursiveSubdivision(
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
    const float bb = 3.0;
    const int maxSquares = 100;  // Reduced from 200 for performance
    const int subdivisions = 175;
    
    // Audio-reactive parameters
    float rotationSpeed = 0.5 + bass * 0.5;
    float subdivisionSeed = floor(time * 0.1);
    
    // Camera setup
    vec3 ro = vec3(0.0, 0.0, 400.0);
    vec3 lookAt = vec3(0.0, 0.0, 0.0);
    vec3 forward = normalize(lookAt - ro);
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), forward));
    vec3 up = cross(forward, right);
    
    // Apply camera zoom
    forward *= (1.0 / uCameraZoom);
    
    // Ray direction
    vec3 rd = normalize(forward + st.x * right + st.y * up);
    
    // Scene rotation
    float rotX = 3.14159 / 2.0 - 3.14159 / 6.0 + 3.14159 / 12.0 * sin(3.14159 / 60.0 * time * rotationSpeed);
    float rotZ = 3.14159 / 60.0 * time * rotationSpeed;
    
    ro = rotateZ(rotateX(ro, rotX), rotZ);
    rd = rotateZ(rotateX(rd, rotX), rotZ);
    
    // Find closest box intersection
    float closestT = 1e6;
    vec3 closestPos = vec3(0.0);
    float closestSize = 0.0;
    int closestIndex = 0;
    
    // Initialize 4 base squares (2x2 grid)
    float baseSize = 300.0;
    vec2 basePositions[4] = vec2[](
        vec2(-100.0, -100.0),
        vec2(100.0, -100.0),
        vec2(100.0, 100.0),
        vec2(-100.0, 100.0)
    );
    
    // Procedurally generate subdivision structure
    for (int i = 0; i < maxSquares; i++) {
        // Determine which base square this belongs to
        int baseIdx = i % 4;
        vec2 basePos = basePositions[baseIdx];
        
        // Calculate subdivision level and position
        int level = 0;
        vec2 pos = basePos;
        float size = baseSize;
        
        // Simulate quadtree subdivision
        for (int s = 0; s < 3; s++) {  // Reduced from 5 to 3 for performance
            float seed = hash21(vec2(float(i), float(s) + subdivisionSeed));
            if (seed < 0.4 && size > 10.0) {
                // Subdivide
                size *= 0.5;
                int quadrant = int(hash21(vec2(float(i), float(s) + subdivisionSeed + 1000.0)) * 4.0);
                if (quadrant == 0) pos += vec2(size * 0.5, size * 0.5);
                else if (quadrant == 1) pos += vec2(-size * 0.5, size * 0.5);
                else if (quadrant == 2) pos += vec2(-size * 0.5, -size * 0.5);
                else pos += vec2(size * 0.5, -size * 0.5);
                level++;
            }
        }
        
        // Calculate box depth based on type
        float type = hash21(vec2(float(i), subdivisionSeed));
        float depth = size * 0.25;
        if (type < 0.6) {
            // Stepped layers
            int layers = int(size / bb);
            depth = float(layers) * bb * 0.5;
        }
        
        // Box position in 3D
        vec3 boxPos = vec3(pos.x, pos.y, 0.0);
        
        // Draw box
        float t = drawBox(ro, rd, boxPos, vec3(size, size, depth));
        
        if (t < closestT) {
            closestT = t;
            closestPos = boxPos;
            closestSize = size;
            closestIndex = i;
        }
    }
    
    // If we hit a box, render it
    if (closestT < 1e5) {
        // HSB color calculation
        float baseHue = hash21(vec2(0.0, subdivisionSeed)) * 360.0;
        float hue = fract((baseHue + hash21(vec2(float(closestIndex), 0.0)) * 180.0 - 90.0) / 360.0);
        float saturation = 0.8 + hash21(vec2(float(closestIndex), 1.0)) * 0.1;
        float brightness = 0.8 + hash21(vec2(float(closestIndex), 2.0)) * 0.1;
        
        vec3 hsbColor = hsb2rgb(vec3(hue, saturation, brightness));
        
        // Audio-reactive color modulation
        hsbColor *= (0.8 + energy * 0.4);
        
        // Add rim lighting based on surface normal
        color = hsbColor;
        
        // Simple distance fog
        float fog = exp(-closestT * 0.002);
        color *= fog;
        alpha = fog;
        
        // Add glow based on energy
        color += vec3(0.1) * energy;
    } else {
        // Background
        color = vec3(0.08, 0.08, 0.04);
        alpha = 1.0;
    }
    
    return vec4(color, alpha);
}
