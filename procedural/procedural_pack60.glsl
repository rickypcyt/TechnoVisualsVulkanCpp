// @EFFECT name="Quadtree Boxes" index=86 desc="3D quadtree subdivision with layered and grid boxes" author="p5.js port"






// Box intersection
float boxIntersect(vec3 ro, vec3 rd, vec3 boxMin, vec3 boxMax) {
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

// Box normal
vec3 boxNormal(vec3 p, vec3 boxMin, vec3 boxMax) {
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


vec4 renderQuadtreeBoxes(
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
    float bb = 3.0;
    float cf = hash21(vec2(0.0, time)) * 360.0;
    
    // Audio-reactive parameters
    bb *= (0.8 + energy * 0.4);
    
    // 3D rotation
    float rotX = 1.5708 - 0.5236 + 0.2618 * sin(3.14159 / 60.0 * time);
    float rotZ = 3.14159 / 60.0 * time;
    
    rotX *= (0.5 + bass * 0.5);
    rotZ *= (0.5 + mid * 0.5);
    
    // Ray setup
    vec3 ro = vec3(0.0, 0.0, 400.0);
    vec2 screen = st * 800.0 - 400.0;
    vec3 rd = normalize(vec3(screen, -400.0));
    
    // Apply rotations
    ro = rotateZ(rotateX(ro, rotX), rotZ);
    rd = rotateZ(rotateX(rd, rotX), rotZ);
    
    // Procedurally generate quadtree structure
    float brightness = 0.0;
    
    // Initial 4 boxes
    vec3 boxPositions[4];
    float boxSizes[4];
    
    boxPositions[0] = vec3(-200.0, -200.0, 0.0);
    boxSizes[0] = 400.0;
    
    boxPositions[1] = vec3(200.0, -200.0, 0.0);
    boxSizes[1] = 400.0;
    
    boxPositions[2] = vec3(200.0, 200.0, 0.0);
    boxSizes[2] = 400.0;
    
    boxPositions[3] = vec3(-200.0, 200.0, 0.0);
    boxSizes[3] = 400.0;
    
    // Render boxes
    for (int i = 0; i < 4; i++) {
        vec3 boxPos = boxPositions[i];
        float boxSize = boxSizes[i];
        
        // Random properties for this box
        float ran = hash31(vec3(float(i), 0.0, 0.0));
        int o = int(hash31(vec3(float(i), 1.0, 0.0)) * 3.0) + 2;
        
        // HSB color
        float hue = mod(cf + hash31(vec3(float(i), 2.0, 0.0)) * 180.0 - 90.0, 360.0) / 360.0;
        float sat = 0.8 + hash31(vec3(float(i), 3.0, 0.0)) * 0.1;
        float bri = 0.8 + hash31(vec3(float(i), 4.0, 0.0)) * 0.1;
        vec3 boxColor = hsb2rgb(vec3(hue, sat, bri));
        
        // Audio-reactive color
        boxColor *= (0.8 + energy * 0.4);
        
        vec3 boxMin = boxPos - boxSize * 0.5;
        vec3 boxMax = boxPos + boxSize * 0.5;
        
        float t = boxIntersect(ro, rd, boxMin, boxMax);
        
        if (t < 1e5) {
            vec3 hit = ro + rd * t;
            vec3 normal = boxNormal(hit, boxMin, boxMax);
            
            // Lighting
            vec3 lightDir = normalize(vec3(-1.0, 0.0, -1.0));
            float diff = max(dot(normal, lightDir), 0.0);
            vec3 lighting = vec3(0.2) + boxColor * diff;
            
            brightness += 0.5 * diff;
            
            // Add grid pattern if ran >= 0.6
            if (ran >= 0.6) {
                vec2 localUV = (hit.xy - boxPos.xy) / boxSize + 0.5;
                float gridX = step(0.5, mod(localUV.x * float(o), 1.0));
                float gridY = step(0.5, mod(localUV.y * float(o), 1.0));
                if (gridX > 0.5 || gridY > 0.5) {
                    lighting *= 0.5;
                }
            }
            
            color += lighting * 0.3;
            alpha += 0.3;
        }
    }
    
    // Procedural subdivision (simplified)
    for (int k = 0; k < 50; k++) {
        float x = (hash31(vec3(float(k), 0.0, 0.0)) - 0.5) * 240.0;
        float y = (hash31(vec3(float(k), 1.0, 0.0)) - 0.5) * 240.0;
        
        // Find containing box
        int h = -1;
        for (int i = 0; i < 4; i++) {
            vec3 boxPos = boxPositions[i];
            float boxSize = boxSizes[i];
            
            if (x > boxPos.x - boxSize * 0.5 && x < boxPos.x + boxSize * 0.5 &&
                y > boxPos.y - boxSize * 0.5 && y < boxPos.y + boxSize * 0.5) {
                h = i;
                break;
            }
        }
        
        if (h >= 0) {
            float newSize = boxSizes[h] * 0.5;
            vec3 subBoxPos = boxPositions[h] + vec3(newSize * 0.5, newSize * 0.5, 0.0);
            
            vec3 subBoxMin = subBoxPos - newSize * 0.5;
            vec3 subBoxMax = subBoxPos + newSize * 0.5;
            
            float t = boxIntersect(ro, rd, subBoxMin, subBoxMax);
            
            if (t < 1e5) {
                vec3 hit = ro + rd * t;
                vec3 normal = boxNormal(hit, subBoxMin, subBoxMax);
                
                float hue = mod(cf + hash31(vec3(float(k), 0.0, 0.0)) * 180.0 - 90.0, 360.0) / 360.0;
                vec3 subColor = hsb2rgb(vec3(hue, 0.85, 0.85));
                
                vec3 lightDir = normalize(vec3(-1.0, 0.0, -1.0));
                float diff = max(dot(normal, lightDir), 0.0);
                vec3 lighting = vec3(0.2) + subColor * diff;
                
                color += lighting * 0.2;
                brightness += 0.2 * diff;
                alpha += 0.2;
            }
        }
    }
    
    color = clamp(color, 0.0, 1.0);
    alpha = clamp(alpha, 0.0, 1.0);
    
    // Background
    vec3 bgColor = vec3(0.08, 0.08, 0.04); // 20,20,10 in HSB converted
    color = mix(bgColor, color, alpha);
    
    return vec4(color, alpha + 0.1);
}
