// @EFFECT name="Lava Fluid Floor" index=97 desc="Fluid lava simulation on floor with spikes" author="System"

float lavaFloor(vec3 p, float time, float tempo, float energy, float bass, float mid, float high) {
    // Base floor at y = -1.0
    float floorY = -1.0;
    
    // Fluid simulation with multiple noise layers
    float fluid = 0.0;
    float scale = 2.0 + energy * 1.0;
    
    // Layer 1 - large waves
    fluid += sin(p.x * scale * 0.5 + time * (1.0 + bass * 0.5)) * 0.3;
    fluid += sin(p.z * scale * 0.5 + time * (0.8 + mid * 0.4)) * 0.3;
    
    // Layer 2 - medium detail
    fluid += sin(p.x * scale * 1.2 + time * (1.5 + high * 0.6)) * 0.15;
    fluid += sin(p.z * scale * 1.2 + time * (1.2 + bass * 0.3)) * 0.15;
    
    // Layer 3 - fine detail
    fluid += sin(p.x * scale * 2.5 + time * (2.0 + mid * 0.8)) * 0.05;
    fluid += sin(p.z * scale * 2.5 + time * (1.8 + high * 0.5)) * 0.05;
    
    // Add Perlin-like noise for organic fluid
    float noise = sin(p.x * 3.0 + p.z * 2.0 + time) * 0.1;
    noise += cos(p.x * 2.0 - p.z * 3.0 + time * 1.3) * 0.1;
    fluid += noise;
    
    // Audio-reactive amplitude
    float amplitude = 0.5 + energy * 0.5 + bass * 0.3;
    fluid *= amplitude;
    
    // Floor with fluid displacement
    float floorDist = p.y - (floorY + fluid);
    
    return floorDist;
}

float lavaSpike(vec3 p, float time, float tempo, float energy, float bass, float mid, float high) {
    // Create lava spikes rising from the floor
    float spikeCount = 8.0 + floor(high * 8.0);
    float angle = atan(p.z, p.x);
    float sector = mod(angle, 2.0 * PI / spikeCount);
    float spikeIndex = floor(angle / (2.0 * PI / spikeCount));
    
    // Spike height varies with audio and time
    float spikeTime = time * (1.0 + tempo * 0.3) + spikeIndex * 0.5;
    float spikeHeight = 0.3 + bass * 0.4 + sin(spikeTime) * 0.2;
    spikeHeight *= (1.0 + energy * 0.5);
    
    // Spike position
    float spikeAngle = spikeIndex * (2.0 * PI / spikeCount) + time * 0.1;
    vec2 spikeCenter = vec2(cos(spikeAngle), sin(spikeAngle)) * 0.5;
    
    // Distance to spike center
    float distToSpike = length(p.xz - spikeCenter);
    
    // Spike shape (cone-like)
    float spikeWidth = 0.15 + mid * 0.1;
    float spike = max(distToSpike, (p.y + 1.0) / spikeHeight);
    spike = spike * spikeWidth;
    
    // Add secondary spikes
    for (int i = 0; i < 3; i++) {
        float offset = float(i) * 0.5;
        vec2 secondaryCenter = spikeCenter * (0.3 + offset * 0.3);
        float secondaryDist = length(p.xz - secondaryCenter);
        float secondaryHeight = spikeHeight * (0.6 - offset * 0.2);
        float secondary = max(secondaryDist, (p.y + 1.0) / secondaryHeight);
        spike = min(spike, secondary * (spikeWidth + offset * 0.1));
    }
    
    return spike;
}

float mapLavaScene(vec3 p, float time, float tempo, float energy, float bass, float mid, float high) {
    float floorDist = lavaFloor(p, time, tempo, energy, bass, mid, high);
    float spikeDist = lavaSpike(p, time, tempo, energy, bass, mid, high);

    // Combine floor and spikes
    return min(floorDist, spikeDist);
}

vec3 estimateLavaNormal(vec3 p, float time, float tempo, float energy, float bass, float mid, float high) {
    float eps = 0.001;
    return normalize(vec3(
        mapLavaScene(p + vec3(eps, 0.0, 0.0), time, tempo, energy, bass, mid, high) - mapLavaScene(p - vec3(eps, 0.0, 0.0), time, tempo, energy, bass, mid, high),
        mapLavaScene(p + vec3(0.0, eps, 0.0), time, tempo, energy, bass, mid, high) - mapLavaScene(p - vec3(0.0, eps, 0.0), time, tempo, energy, bass, mid, high),
        mapLavaScene(p + vec3(0.0, 0.0, eps), time, tempo, energy, bass, mid, high) - mapLavaScene(p - vec3(0.0, 0.0, eps), time, tempo, energy, bass, mid, high)
    ));
}

vec4 renderLavaFluidFloor(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Camera setup (similar to Raymarched Object)
    vec3 ro = vec3(0.0, 0.5, 4.0);
    vec3 target = vec3(0.0, -0.5, 0.0);
    vec3 forward = normalize(target - ro);
    vec3 right = normalize(vec3(forward.z, 0.0, -forward.x));
    vec3 up = normalize(cross(right, forward));
    // Rotate 180 degrees by inverting screen coordinates
    vec3 rd = normalize(forward + right * (-st.x) * 1.4 + up * (-st.y) * 1.0);
    
    // Raymarching
    float t = 0.0;
    float d = 0.0;
    bool hit = false;
    
    for (int i = 0; i < 64; ++i) {
        vec3 pos = ro + rd * t;
        d = mapLavaScene(pos, time, tempo, energy, bass, mid, high);
        if (d < 0.001) {
            hit = true;
            break;
        }
        t += d * 0.85;
        if (t > 15.0) {
            break;
        }
    }

    vec3 color;
    float alpha;

    if (hit) {
        vec3 pos = ro + rd * t;
        vec3 normal = estimateLavaNormal(pos, time, tempo, energy, bass, mid, high);
        vec3 lightDir = normalize(vec3(0.6, 1.0, -0.4));
        
        // Lighting
        float diff = max(dot(normal, lightDir), 0.0);
        float spec = pow(max(dot(reflect(-lightDir, normal), -rd), 0.0), 32.0);
        float rim = pow(1.0 - max(dot(normal, -rd), 0.0), 4.0);
        
        // Lava color palette (similar to Raymarched Object but warmer)
        vec3 base = mix(uPrimaryColor, uSecondaryColor, 0.45 + high * 0.35);
        
        // Add lava glow based on height and audio
        float heightGlow = smoothstep(-1.0, 0.0, pos.y);
        vec3 lavaGlow = mix(vec3(1.0, 0.3, 0.0), vec3(1.0, 0.8, 0.2), heightGlow);
        lavaGlow *= (1.0 + bass * 0.5 + energy * 0.4);
        
        // Combine lighting with lava glow
        color = base * (0.25 + diff * (0.9 + energy * 0.4));
        color += lavaGlow * diff * (0.6 + bass * 0.4);
        color += vec3(1.0, 0.6, 0.2) * spec * (0.4 + high * 0.6);
        color += base.bgr * rim * (0.3 + energy * 0.5);
        
        // Add fluid lines/bands
        float fluidLines = sin(pos.x * 10.0 + time * 2.0) * sin(pos.z * 10.0 + time * 1.5);
        fluidLines *= (0.3 + mid * 0.4);
        color += vec3(1.0, 0.4, 0.0) * fluidLines * (0.2 + bass * 0.3);
        
        color = clamp(color, 0.0, 1.0);
        alpha = clamp(0.4 + diff * 0.4 + rim * 0.3 + energy * 0.25, 0.0, 1.0);
    } else {
        // Background (dark lava-like atmosphere)
        float fade = clamp(1.0 - t / 15.0, 0.0, 1.0);
        vec3 bg = mix(uPrimaryColor * 0.1, uSecondaryColor * 0.2, fade);
        bg += vec3(0.1, 0.05, 0.0) * fbm(st * 2.0 + time * 0.15);
        
        // Add subtle lava glow in background
        float bgGlow = exp(-t * 0.3) * (0.3 + bass * 0.3);
        bg += vec3(0.8, 0.2, 0.0) * bgGlow;
        
        color = clamp(bg, 0.0, 1.0);
        alpha = clamp(fade * 0.35, 0.0, 0.5);
    }
    
    return vec4(color, alpha);
}
