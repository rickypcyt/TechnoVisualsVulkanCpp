// Stage7 - Geometric patterns with beat synchronization
// Based on Danguafer/Silexars shader with audio-reactive modifications

// Modified beat function for audio reactivity
float mb(vec2 p1, vec2 p0, float beat) { 
    return (0.04 + beat) / (pow(p1.x - p0.x, 2.0) + pow(p1.y - p0.y, 2.0)); 
}

vec4 renderStage7(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Audio-reactive beat detection
    float audioBeat = pow(bass * 0.8 + energy * 0.4, 2.0) * 0.05;
    float beat = audioBeat; // Use audio instead of channel time
    
    // Coordinate system
    vec2 p = (2.0 * st - 1.0);
    vec2 o = vec2(pow(p.x, 2.0), pow(p.y, 2.0));
    
    // Base geometric pattern
    vec3 col = vec3(pow(2.0 * abs(o.x + o.y) + abs(o.x - o.y), 5.0));
    col = col * 0.5; // Scale down to prevent oversaturation
    
    // Time with beat influence
    float t = time + beat * 2.0;
    
    // Multiple frequency oscillators
    float t2 = t * 2.0, t3 = t * 3.0, s2 = sin(t2), s3 = sin(t3), s4 = sin(t * 4.0), c2 = cos(t2), c3 = cos(t3);
    
    // Moving blobs for each color channel
    vec2 mbr, mbg, mbb;
    mbr = mbg = mbb = vec2(0.0);
    
    // Red channel movement - responds to bass
    mbr += vec2(0.10 * s4 + 0.40 * c3, 0.40 * s2 + 0.20 * c3);
    mbr += vec2(bass * 0.2 * sin(time * 5.0), bass * 0.2 * cos(time * 3.0));
    
    // Green channel movement - responds to mid
    mbg += vec2(0.15 * s3 + 0.30 * c2, 0.10 * -s4 + 0.30 * c3);
    mbg += vec2(mid * 0.15 * sin(time * 4.0 + 1.0), mid * 0.15 * cos(time * 6.0));
    
    // Blue channel movement - responds to high
    mbb += vec2(0.10 * s3 + 0.50 * c3, 0.10 * -s4 + 0.50 * c2);
    mbb += vec2(high * 0.25 * sin(time * 7.0 + 2.0), high * 0.25 * cos(time * 5.0));
    
    // Distance-based color modulation
    col.r *= length(mbr.xy - p.xy);
    col.g *= length(mbg.xy - p.xy);
    col.b *= length(mbb.xy - p.xy);
    
    // Apply the metaball effect
    col *= pow(mb(mbr, p, beat) + mb(mbg, p, beat) + mb(mbb, p, beat), 1.75);
    
    // Apply scene colors
    vec3 baseColor = mix(uPrimaryColor, uSecondaryColor, uColorBlend);
    col = mix(col, baseColor, 0.2);
    
    // Audio-reactive brightness
    col *= (0.8 + energy * 0.4);
    
    // Add frequency-based color enhancement
    col.r *= (1.0 + bass * 0.3);
    col.g *= (1.0 + mid * 0.3);
    col.b *= (1.0 + high * 0.3);
    
    // Add glow for high energy moments
    if (energy > 0.6) {
        float glow = (energy - 0.6) * 2.5;
        col += vec3(0.2, 0.4, 0.8) * glow;
    }
    
    // Ensure colors are in valid range
    col = clamp(col, 0.0, 1.0);
    
    // Alpha based on intensity and energy
    float alpha = 0.7 + length(col) * 0.15 + energy * 0.3;
    alpha = clamp(alpha, 0.0, 1.0);
    
    return vec4(col, alpha);
}
