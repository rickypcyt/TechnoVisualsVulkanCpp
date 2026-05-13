// @EFFECT name="Particle Cloud" index=83 desc="Rotating sphere-like cloud with mouse repulsion" author="p5.js port"


vec4 renderParticleCloud(
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
    const int particles = 8000;
    const float attraction = 0.01;
    const float damping = 0.9;
    const float repel_strength = 28.0;
    float radius = 250.0;
    float repel_radius = 90.0;
    
    // Audio-reactive parameters
    radius *= (0.8 + bass * 0.4);
    repel_radius *= (0.8 + energy * 0.5);
    
    // Rotation angle
    float angle = time * 0.5;
    angle *= (0.5 + tempo * 0.5);
    
    // Mouse position (use camera offset as mouse equivalent)
    vec2 mouse = vec2(uCameraOffsetX, uCameraOffsetY) * 500.0;
    
    // Sample particles procedurally
    float brightness = 0.0;
    
    // Use spatial hashing to simulate particle density
    vec2 gridPos = st * 100.0;
    vec2 gridIdx = floor(gridPos);
    vec2 gridUV = fract(gridPos);
    
    // Sample multiple particles in this region
    for (int i = 0; i < 16; i++) {  // Reduced from 32 to 16 for performance
        float idx = float(i) + hash21(gridIdx) * 100.0;
        
        // Compute rotating "home" position
        float homeX = sin(idx + angle) * sin(idx * idx) * radius;
        float homeY = cos(idx * idx) * radius;
        vec2 home = vec2(homeX, homeY);
        
        // Current particle position (with some noise)
        vec2 noise = vec2(hash21(vec2(idx, time)), hash21(vec2(idx, time + 100.0))) - 0.5;
        vec2 pos = home + noise * 10.0;
        
        // Mouse repulsion
        vec2 awayFromMouse = pos - mouse;
        float distSq = dot(awayFromMouse, awayFromMouse);
        
        if (distSq > 0.1 && distSq < repel_radius * repel_radius) {
            float distance = sqrt(distSq);
            awayFromMouse = normalize(awayFromMouse);
            float repel = repel_strength * (1.0 - distance / repel_radius);
            pos += awayFromMouse * repel * 0.5;
        }
        
        // Check if this particle is close to our sample point
        vec2 toSample = st * 1000.0 - pos;
        float sampleDist = length(toSample);
        
        if (sampleDist < 15.0) {
            brightness += smoothstep(15.0, 0.0, sampleDist);
        }
    }
    
    brightness = clamp(brightness, 0.0, 1.0);
    
    // White color from p5.js
    vec3 particleColor = vec3(1.0);
    
    // Audio-reactive brightness
    particleColor *= (0.8 + energy * 0.4);
    
    color = particleColor * brightness;
    alpha = brightness;
    
    // Background
    vec3 bgColor = vec3(0.0);
    color = mix(bgColor, color, alpha);
    
    // Add subtle glow based on energy
    color += vec3(0.1) * energy * brightness;
    
    return vec4(color, alpha + 0.1);
}
