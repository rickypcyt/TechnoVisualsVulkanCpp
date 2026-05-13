// @EFFECT name="Star Ring" index=87 desc="Star ring with particle system and gradient layers" author="p5.js port"



vec4 renderStarRing(
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
    float innerRadiusi = 75.0;
    float innerRadius = 130.0;
    float innerRadiussa = 320.0;
    float innerRadiussb = 198.0;
    float outerRadius = 550.0;
    float noiseAmp = 20.0;
    float Noisefactor = 33.0;
    float innerNoisefactor = 10.0;
    float innerAlpha = 255.0;
    float outerAlpha = 15.0;
    float thick = 10.0;
    float pointStep = 0.9;
    
    // Audio-reactive parameters
    noiseAmp *= (0.8 + bass * 0.4);
    Noisefactor *= (0.8 + mid * 0.4);
    
    // Center coordinates
    vec2 center = vec2(0.5);
    vec2 fromCenter = st - center;
    float dist = length(fromCenter);
    float angle = atan(fromCenter.y, fromCenter.x);
    
    // Rotation angles
    float d = time * 0.8 * 1.5;
    float e = time * 0.8 * 0.65;
    float g = time * 0.8 * 0.2;
    
    d *= (0.5 + bass * 0.5);
    e *= (0.5 + mid * 0.5);
    g *= (0.5 + high * 0.5);
    
    // Calculate ring contributions
    float ringBrightness = 0.0;
    
    // Inner ring (white to orange gradient)
    if (d < 360.0) {
        vec2 rotatedUV = rotate2D(fromCenter, d);
        float ringDist = length(rotatedUV);
        
        if (ringDist > innerRadiusi && ringDist < outerRadius) {
            float noiseOffset = sin(time / innerNoisefactor / 10.0) * cos(time / innerNoisefactor / 10.0) * 15.0;
            float noiseVal = noise(vec2(ringDist / Noisefactor / 3.0, time)) * noiseAmp;
            
            float alpha = mix(innerAlpha / 255.0, outerAlpha / 255.0, (ringDist - innerRadiusi) / (outerRadius - innerRadiusi));
            
            // Color gradient from white to orange
            vec3 c3 = vec3(247.0, 127.0, 0.0) / 255.0;
            vec3 c4 = vec3(1.0);
            float mixVal = (ringDist - innerRadiusi) / (outerRadius - innerRadiusi);
            vec3 ringColor = mix(c4, c3, mixVal);
            
            float pointSize = 1.0;
            float pointDist = abs(ringDist - (floor(ringDist / pointStep) * pointStep + noiseOffset));
            
            if (pointDist < 0.01) {
                ringBrightness += alpha * ringColor.r * 0.3;
            }
        }
    }
    
    // Middle ring (white to blue gradient)
    if (e < 360.0) {
        vec2 rotatedUV = rotate2D(fromCenter, e);
        float ringDist = length(rotatedUV);
        
        if (ringDist > innerRadius && ringDist < outerRadius) {
            float noiseOffset = sin(20.0 * time) * 10.0 + noise(vec2(time / innerNoisefactor, 1.0)) * 50.0;
            float noiseVal = noise(vec2(ringDist / Noisefactor, time)) * noiseAmp;
            
            float alpha = mix(innerAlpha / 255.0, outerAlpha / 255.0, (ringDist - innerRadius) / (outerRadius - innerRadius));
            
            // Color gradient from white to blue
            vec3 c1 = vec3(0.0, 167.0, 225.0) / 255.0;
            vec3 c2 = vec3(1.0);
            float mixVal = (ringDist - innerRadius) / (outerRadius - innerRadius);
            vec3 ringColor = mix(c2, c1, mixVal);
            
            float thick1 = noise(vec2(time * 10.0, 0.0)) * thick + 20.0 * (sin(10.0 * time) + 1.0);
            float irisStroke = 1.0 + 0.6 * step(thick1, 0.0);
            
            float pointDist = abs(ringDist - (floor(ringDist / pointStep) * pointStep + noiseOffset));
            float waveOffset = noiseVal;
            
            if (pointDist < 0.02) {
                ringBrightness += alpha * ringColor.g * 0.4;
            }
        }
    }
    
    // Outer ring (blue to teal gradient)
    if (g < 360.0) {
        vec2 rotatedUV = rotate2D(fromCenter, g);
        float ringDist = length(rotatedUV);
        
        if (ringDist > innerRadiussa && ringDist < outerRadius) {
            float noiseOffset = sin(2.5 * time) * 10.0 + noise(vec2(time / innerNoisefactor / 1.1, 2.0)) * 40.0;
            
            float alpha = mix(innerAlpha / 255.0, (outerAlpha - 15.0) / 255.0, (ringDist - innerRadius) / (outerRadius - innerRadius));
            
            // Color gradient from blue to teal
            vec3 c5 = vec3(3.0, 71.0, 72.0) / 255.0;
            vec3 c6 = vec3(20.0, 129.0, 186.0) / 255.0;
            float mixVal = (ringDist - innerRadius) / (outerRadius - innerRadius);
            vec3 ringColor = mix(c6, c5, mixVal);
            
            float pointDist = abs(ringDist - (floor(ringDist / pointStep) * pointStep + noiseOffset));
            
            if (pointDist < 0.01) {
                ringBrightness += alpha * ringColor.b * 0.3;
            }
        }
    }
    
    // Inner solid ring (cyan to dark blue gradient)
    if (g < 360.0) {
        vec2 rotatedUV = rotate2D(fromCenter, g);
        float ringDist = length(rotatedUV);
        
        if (ringDist > innerRadiussb && ringDist < outerRadius) {
            float noiseOffset = cos(2.5 * time) * 10.0 + noise(vec2(time / innerNoisefactor / 1.2, 3.0)) * 40.0;
            
            float alpha = mix((innerAlpha - 90.0) / 255.0, (outerAlpha - 15.0) / 255.0, (ringDist - innerRadius) / (outerRadius - innerRadius));
            
            // Color gradient from cyan to dark blue
            vec3 c7 = vec3(0.0, 150.0, 199.0) / 255.0;
            vec3 c8 = vec3(10.0, 36.0, 99.0) / 255.0;
            float mixVal = (ringDist - innerRadius) / (outerRadius - innerRadius);
            vec3 ringColor = mix(c7, c8, mixVal);
            
            float pointDist = abs(ringDist - (floor(ringDist / pointStep) * pointStep + noiseOffset));
            
            if (pointDist < 0.01) {
                ringBrightness += alpha * ringColor.b * 0.2;
            }
        }
    }
    
    // Particle system
    float particleBrightness = 0.0;
    const int numParticles = 100;
    
    for (int i = 0; i < numParticles; i++) {
        float idx = float(i);
        float particleAngle = hash21(vec2(idx, 0.0)) * 6.28318;
        float particleRadius = mix(innerRadius, outerRadius * 0.5, hash21(vec2(idx, 1.0)));
        
        vec2 particlePos = vec2(cos(particleAngle), sin(particleAngle)) * particleRadius;
        
        // Noise-based movement
        float noiseFactorx = 35.0;
        float noiseFactory = 35.0;
        vec2 vel = vec2(
            map(noise(vec2(particlePos.x / noiseFactorx, particlePos.y / noiseFactory)), 0.0, 1.0, -1.0, 1.0) * 2.5,
            map(noise(vec2(particlePos.x / noiseFactorx + 10.0, particlePos.y / noiseFactory + 100.0)), 0.0, 1.0, -1.0, 1.0) * 2.5
        );
        
        particlePos += vel * time * 0.01;
        
        float particleDist = length(fromCenter - particlePos);
        if (particleDist < 2.5) {
            float alpha = mix(100.0 / 255.0, 0.0, sqrt(dot(particlePos, particlePos)) / outerRadius);
            particleBrightness += alpha * 0.5;
        }
    }
    
    // Star system (radial lines from center)
    float starBrightness = 0.0;
    const int numStars = 50;
    
    for (int i = 0; i < numStars; i++) {
        float idx = float(i);
        float starAngle = hash21(vec2(idx, 0.0)) * 6.28318;
        float starDist = hash21(vec2(idx, 1.0)) * outerRadius;
        
        vec2 starPos = vec2(cos(starAngle), sin(starAngle)) * starDist;
        
        // Radial movement
        float acc = mix(0.005, 0.2, energy);
        vec2 vel = vec2(cos(starAngle), sin(starAngle)) * acc * time * 10.0;
        
        starPos += vel;
        
        // Check if star is near our sample point
        float lineDist = abs(dot(normalize(fromCenter), normalize(starPos)));
        float distToLine = length(fromCenter - starPos * dot(fromCenter, starPos) / dot(starPos, starPos));
        
        if (distToLine < 0.02 && lineDist > 0.9) {
            float alpha = mix(0.0, 1.0, length(vel) / 3.0);
            starBrightness += alpha * 0.3;
        }
    }
    
    // Combine all layers
    ringBrightness = clamp(ringBrightness, 0.0, 1.0);
    particleBrightness = clamp(particleBrightness, 0.0, 1.0);
    starBrightness = clamp(starBrightness, 0.0, 1.0);
    
    // Composite colors
    vec3 ringColor = vec3(ringBrightness * 0.8, ringBrightness * 0.6, ringBrightness * 0.4);
    vec3 particleColor = vec3(particleBrightness);
    vec3 starColor = vec3(starBrightness);
    
    color = ringColor + particleColor + starColor;
    alpha = ringBrightness + particleBrightness * 0.5 + starBrightness * 0.3;
    
    // Audio-reactive color modulation
    color *= (0.8 + energy * 0.4);
    
    // Background
    vec3 bgColor = vec3(0.0);
    color = mix(bgColor, color, clamp(alpha, 0.0, 1.0));
    
    return vec4(color, clamp(alpha + 0.1, 0.0, 1.0));
}
