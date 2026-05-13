// @EFFECT name="Isometric Cubes" index=94 desc="Isometric 3D cube grid with ray tracing" author="p5.js port"


// Isometric projection
vec3 isometricProject(vec3 p, float alpha, float beta) {
    // Convert degrees to radians
    float a = radians(alpha);
    float b = radians(beta);
    
    // Isometric rotation matrix
    float x = p.x * cos(a) - p.y * sin(a);
    float y = (p.x * sin(a) + p.y * cos(a)) * cos(b) - p.z * sin(b);
    float z = (p.x * sin(a) + p.y * cos(a)) * sin(b) + p.z * cos(b);
    
    return vec3(x, y, z);
}

// AABB ray-box intersection
bool intersectAABB(vec3 rayOrigin, vec3 rayDir, vec3 boxMin, vec3 boxMax) {
    vec3 invDir = 1.0 / rayDir;
    
    float tmin = (boxMin.x - rayOrigin.x) * invDir.x;
    float tmax = (boxMax.x - rayOrigin.x) * invDir.x;
    
    if (tmin > tmax) {
        float temp = tmin;
        tmin = tmax;
        tmax = temp;
    }
    
    float tymin = (boxMin.y - rayOrigin.y) * invDir.y;
    float tymax = (boxMax.y - rayOrigin.y) * invDir.y;
    
    if (tymin > tymax) {
        float temp = tymin;
        tymin = tymax;
        tymax = temp;
    }
    
    if (tmin > tymax || tymin > tmax) {
        return false;
    }
    
    if (tymin > tmin) {
        tmin = tymin;
    }
    
    if (tymax < tmax) {
        tmax = tymax;
    }
    
    float tzmin = (boxMin.z - rayOrigin.z) * invDir.z;
    float tzmax = (boxMax.z - rayOrigin.z) * invDir.z;
    
    if (tzmin > tzmax) {
        float temp = tzmin;
        tzmin = tzmax;
        tzmax = temp;
    }
    
    if (tmin > tzmax || tzmin > tmax) {
        return false;
    }
    
    return true;
}

// Draw cube with isometric projection
float drawCube(vec2 uv, vec3 cubePos, float cubeSize, float alpha, float beta) {
    // Calculate cube bounds
    vec3 boxMin = cubePos - cubeSize * 0.5;
    vec3 boxMax = cubePos + cubeSize * 0.5;
    
    // Project cube vertices to 2D isometric
    vec3 corners[8];
    corners[0] = isometricProject(vec3(boxMin.x, boxMin.y, boxMin.z), alpha, beta);
    corners[1] = isometricProject(vec3(boxMax.x, boxMin.y, boxMin.z), alpha, beta);
    corners[2] = isometricProject(vec3(boxMax.x, boxMax.y, boxMin.z), alpha, beta);
    corners[3] = isometricProject(vec3(boxMin.x, boxMax.y, boxMin.z), alpha, beta);
    corners[4] = isometricProject(vec3(boxMin.x, boxMin.y, boxMax.z), alpha, beta);
    corners[5] = isometricProject(vec3(boxMax.x, boxMin.y, boxMax.z), alpha, beta);
    corners[6] = isometricProject(vec3(boxMax.x, boxMax.y, boxMax.z), alpha, beta);
    corners[7] = isometricProject(vec3(boxMin.x, boxMax.y, boxMax.z), alpha, beta);
    
    // Check if point is inside projected cube (simplified)
    float minProjX = 1000.0, maxProjX = -1000.0;
    float minProjY = 1000.0, maxProjY = -1000.0;
    
    for (int i = 0; i < 8; i++) {
        minProjX = min(minProjX, corners[i].x);
        maxProjX = max(maxProjX, corners[i].x);
        minProjY = min(minProjY, corners[i].y);
        maxProjY = max(maxProjY, corners[i].y);
    }
    
    // Simple bounding box check
    if (uv.x >= minProjX && uv.x <= maxProjX && uv.y >= minProjY && uv.y <= maxProjY) {
        return 1.0;
    }
    
    return 0.0;
}

vec4 renderIsometricCubes(
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
    
    // Parameters from original code
    float alphaAngle = 45.0; // yaw
    float betaAngle = 35.264; // pitch
    float cubeSize = 0.1;
    
    // Audio-reactive parameters
    alphaAngle += bass * 10.0;
    betaAngle += mid * 5.0;
    cubeSize *= (0.8 + energy * 0.4);
    
    // Grid size
    int gridSize = 8;
    
    // Center coordinates
    vec2 center = vec2(0.5);
    vec2 uv = (st - center) * 2.0;
    
    float brightness = 0.0;
    
    // Create cube grid
    for (int x = -4; x < 4; x++) {
        for (int y = -4; y < 4; y++) {
            for (int z = -2; z < 2; z++) {
                // Cube position
                vec3 cubePos = vec3(float(x) * cubeSize, float(y) * cubeSize, float(z) * cubeSize);
                
                // Apply audio-reactive offset
                cubePos.x += sin(time + float(x) * 0.5) * bass * 0.1;
                cubePos.y += cos(time + float(y) * 0.5) * mid * 0.1;
                cubePos.z += sin(time + float(z) * 0.5) * high * 0.1;
                
                // Project cube
                float cubeHit = drawCube(uv, cubePos, cubeSize, alphaAngle, betaAngle);
                
                if (cubeHit > 0.0) {
                    // Use user colors instead of hardcoded purple
                    vec3 cubeColor = mix(uPrimaryColor, uSecondaryColor, 0.5);
                    
                    // Audio-reactive color modulation
                    cubeColor *= (0.8 + energy * 0.4);
                    
                    brightness += cubeHit * 0.8;
                    color += cubeColor * cubeHit;
                }
            }
        }
    }
    
    brightness = clamp(brightness, 0.0, 1.0);
    color = clamp(color, 0.0, 1.0);
    
    // Background
    vec3 bgColor = vec3(0.05, 0.05, 0.1);
    color = mix(bgColor, color, brightness);
    
    alpha = brightness + 0.1;
    
    return vec4(color, clamp(alpha, 0.0, 1.0));
}
