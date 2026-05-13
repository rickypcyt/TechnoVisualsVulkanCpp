// @EFFECT name="Triangle Particles" index=90 desc="Triangle particle system with growing rotating triangles" author="p5.js port"



// Draw a triangle
float drawTriangle(vec2 p, vec2 center, float size, float angle) {
    vec2 localPos = rotate2D(p - center, -angle);
    
    // Triangle vertices (equilateral triangle)
    vec2 v0 = vec2(0.0, size);
    vec2 v1 = vec2(size * cos(2.094), size * sin(2.094));
    vec2 v2 = vec2(size * cos(4.189), size * sin(4.189));
    
    // Edge function for triangle
    vec2 e0 = v1 - v0;
    vec2 e1 = v2 - v1;
    vec2 e2 = v0 - v2;
    
    vec2 p0 = localPos - v0;
    vec2 p1 = localPos - v1;
    vec2 p2 = localPos - v2;
    
    float s0 = sign(e0.x * p0.y - e0.y * p0.x);
    float s1 = sign(e1.x * p1.y - e1.y * p1.x);
    float s2 = sign(e2.x * p2.y - e2.y * p2.x);
    
    // Inside triangle if all signs are same
    float inside = step(0.0, s0 * s1 * s2);
    
    // Add edge glow
    float edge0 = abs(e0.x * p0.y - e0.y * p0.x) / length(e0);
    float edge1 = abs(e1.x * p1.y - e1.y * p1.x) / length(e1);
    float edge2 = abs(e2.x * p2.y - e2.y * p2.x) / length(e2);
    float edge = min(min(edge0, edge1), edge2);
    float edgeGlow = (1.0 - smoothstep(0.0, 0.02, edge)) * 0.5;
    
    return inside + edgeGlow;
}

vec4 renderTriangleParticles(
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
    
    // Background color (light gray from p5.js)
    vec3 bgColor = vec3(240.0 / 255.0);
    
    // Audio-reactive parameters
    float particleCount = 50.0 + energy * 50.0;
    float maxParticleSize = 50.0 + bass * 30.0;
    float growthRate = 1.0 + mid * 0.5;
    
    // Generate particles
    float brightness = 0.0;
    const int maxParticles = 100;
    
    for (int i = 0; i < maxParticles; i++) {
        if (float(i) >= particleCount) break;
        
        // Seed for this particle
        vec2 seed = vec2(float(i), 0.0);
        
        // Initial position (center of screen, or random)
        vec2 initPos = vec2(0.5) + (vec2(hash21(seed), hash21(seed + 1.0)) - 0.5) * 0.1;
        
        // Velocity (random direction)
        float angle = hash21(seed + 2.0) * 6.28318;
        float speed = hash21(seed + 3.0) * 10.0;
        vec2 vel = vec2(cos(angle), sin(angle)) * speed * 0.001;
        
        // Acceleration
        float mx = mix(0.3, 0.5, hash21(seed + 4.0));
        float my = mix(0.3, 0.5, hash21(seed + 5.0));
        if (hash21(seed + 6.0) < 0.5) mx = -mx;
        if (hash21(seed + 7.0) < 0.5) my = -my;
        vec2 acc = vec2(mx, my) * 0.0001;
        
        // Particle lifetime
        float lifetime = hash21(seed + 8.0) * 5.0;
        float particleTime = mod(time + lifetime, 10.0);
        
        // Size grows over time
        float size = 1.0 + particleTime * growthRate * 10.0;
        float maxSize = mix(5.0, maxParticleSize, hash21(seed + 9.0));
        
        // Kill particle when size exceeds max
        if (size > maxSize) continue;
        
        // Position updates
        vec2 pos = initPos + vel * particleTime * 60.0 + acc * particleTime * particleTime * 3600.0;
        
        // Rotation
        float rotAngle = 0.0;
        float incr = 5.0;
        if (hash21(seed + 10.0) < 0.5) incr = -5.0;
        rotAngle = incr * particleTime * 60.0 * 0.01;
        
        // Draw triangle
        float tri = drawTriangle(st, pos, size * 0.002, rotAngle);
        
        if (tri > 0.0) {
            // Color (black from p5.js)
            brightness += tri * 0.3;
        }
    }
    
    brightness = clamp(brightness, 0.0, 1.0);
    
    // Color (black triangles on light background)
    color = mix(bgColor, vec3(0.0), brightness);
    
    // Add shadow effect (simulated)
    float shadow = brightness * 0.2;
    color = mix(color, vec3(0.0), shadow);
    
    alpha = brightness + 0.1;
    
    return vec4(color, clamp(alpha + 0.1, 0.0, 1.0));
}
