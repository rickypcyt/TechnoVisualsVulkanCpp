// @EFFECT name="3D Wave Boxes" index=79 desc="Concentric layers of boxes with wave motion" author="p5.js port"

// Draw a box at position with rotation
float drawBox(vec3 rayOrigin, vec3 rayDir, vec3 boxPos, float boxSize, float angle) {
    // Rotate ray into box local space
    vec3 localOrigin = rayOrigin - boxPos;
    localOrigin = rotateY(localOrigin, -angle);
    vec3 localDir = rotateY(rayDir, -angle);
    
    // Simple box intersection (AABB in local space)
    vec3 boxHalf = vec3(boxSize * 0.5);
    vec3 invDir = 1.0 / max(abs(localDir), 1e-6);
    vec3 tMin = (-boxHalf - localOrigin) * invDir;
    vec3 tMax = (boxHalf - localOrigin) * invDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);
    
    if (tNear < tFar && tFar > 0.0) {
        return tNear;
    }
    return 1e6;
}

vec4 render3DWaveBoxes(
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
    const int layerNum = 12;  // Reduced from 25 for performance
    const float distance = 10.0;
    const int boxNum = 20;  // Reduced from 40 for performance
    const float amplitude = 50.0;
    const float waveSpeed = 1.0;
    
    // Audio-reactive modifications
    float audioAmp = amplitude * (0.8 + bass * 0.4);
    float audioWaveSpeed = waveSpeed * (0.5 + energy * 0.5);
    
    // Camera setup (simplified raymarching)
    vec3 ro = vec3(0.0, -300.0, 300.0);
    vec3 lookAt = vec3(0.0, amplitude, 0.0);
    vec3 forward = normalize(lookAt - ro);
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), forward));
    vec3 up = cross(forward, right);
    
    // Apply camera zoom
    forward *= (1.0 / uCameraZoom);
    
    // Ray direction
    vec3 rd = normalize(forward + st.x * right + st.y * up);
    
    // Scene rotation around Y axis
    float sceneRotation = time * 10.0;
    ro = rotateY(ro, sceneRotation);
    rd = rotateY(rd, sceneRotation);
    
    // Find closest box intersection
    float closestT = 1e6;
    vec3 closestPos = vec3(0.0);
    float closestLayer = 0.0;
    float closestTheta = 0.0;
    
    // Iterate through layers
    for (int layer = 1; layer <= layerNum; layer++) {
        float r = float(layer) * distance;
        
        // Iterate through boxes in layer
        for (int i = 0; i < boxNum; i++) {
            float theta = float(i) * 360.0 / float(boxNum);
            
            // Spherical to rectangular
            vec3 posi = sphericalToRectangular(r, theta, 90.0);
            
            // Add wave motion
            float wavePhase = audioWaveSpeed * time + float(layer) / float(layerNum) * 360.0;
            posi.y += audioAmp * sin(radians(wavePhase));
            
            // Box size based on layer and theta
            float centralAngle = 360.0 / float(boxNum) * 0.9;
            float boxSize = centralAngle / 360.0 * 3.14159 * r;
            
            // Rotate ray into box space
            float t = drawBox(ro, rd, posi, boxSize, theta);
            
            if (t < closestT) {
                closestT = t;
                closestPos = posi;
                closestLayer = float(layer);
                closestTheta = theta;
            }
        }
    }
    
    // If we hit a box, render it
    if (closestT < 1e5) {
        // HSB color calculation
        float hue = remap(closestLayer, 0.0, float(layerNum), 0.0, float(layerNum));
        float sat = 100.0 * (0.75 + 0.25 * sin(radians(mod(time, 360.0))));
        float bri = 100.0 * (1.0 + 0.0 * cos(radians(mod(time / 2.0, 360.0))));
        
        vec3 hsbColor = vec3(hue, sat, bri) / vec3(float(layerNum), 100.0, 100.0);
        color = hsb2rgb(hsbColor);
        
        // Add audio-reactive glow
        color *= (0.8 + energy * 0.4);
        
        // Simple distance fog
        float fog = exp(-closestT * 0.001);
        color *= fog;
        alpha = fog;
        
        // Add rim lighting
        color += vec3(0.2) * energy;
    } else {
        // Background
        color = vec3(0.0);
        alpha = 0.0;
    }
    
    return vec4(color, alpha);
}
