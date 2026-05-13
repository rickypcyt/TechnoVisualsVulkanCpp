// @EFFECT name="Evolution Noise" index=35 desc="Time-evolving noise pattern with audio reactivity" author="System"
// Evolution Noise - Time-evolving noise pattern
// Based on fractal noise with temporal evolution

float noise(vec2 pos, float evolve) {
    // Loop the evolution (over a very long period of time).
    float e = fract((evolve * 0.01));
    
    // Coordinates
    float cx = pos.x * e;
    float cy = pos.y * e;
    
    // Generate a "random" black or white value
    return fract(23.0 * fract(2.0 / fract(fract(cx * 2.4 / cy * 23.0) * fract(cx * evolve / pow(abs(cy), 0.050)))));
}

vec4 renderEvolutionNoise(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Audio-reactive parameters
    float audioMod = 1.0 + energy * 0.8 + bass * 0.4;
    float timeMod = time * audioMod;
    
    // Scale based on audio frequencies
    vec2 scale = vec2(1.0) + vec2(bass, mid) * 0.5;
    vec2 pos = st * scale * 2.0;
    
    // Generate multiple layers of noise for complexity
    vec3 colour = vec3(0.0);
    int layers = 3; // Reduced for performance
    
    for(int i = 0; i < layers; i++) {
        float layerTime = timeMod + float(i) * 0.1;
        float noiseValue = noise(pos * (1.0 + float(i) * 0.5), layerTime);
        
        // Different color channels respond to different frequencies
        if(i == 0) {
            colour.r = noiseValue; // Bass affects red
        } else if(i == 1) {
            colour.g = noiseValue; // Mid affects green  
        } else {
            colour.b = noiseValue; // High affects blue
        }
    }
    
    // Apply scene colors with reduced intensity for TV static effect
    vec3 baseColor = mix(uPrimaryColor, uSecondaryColor, uColorBlend);
    colour = mix(colour, baseColor * 0.3, 0.2); // Reduced base color influence
    
    // Reduce overall brightness to TV static levels (mostly gray)
    colour *= (0.3 + energy * 0.2); // Much lower brightness
    
    // Add subtle pulsing effect based on tempo
    float pulse = sin(time * tempo * 0.1) * 0.5 + 0.5;
    colour += pulse * energy * 0.1; // Reduced pulse intensity
    
    // Ensure colors stay in TV static range (darker grays)
    colour = clamp(colour, 0.1, 0.8); // Clamp to prevent white washout
    
    // Alpha based on energy and noise intensity
    float alpha = 0.6 + energy * 0.4;
    alpha = clamp(alpha, 0.0, 1.0);
    
    return vec4(colour, alpha);
}
