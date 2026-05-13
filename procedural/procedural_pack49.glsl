// @EFFECT name="Audio Spectrum EQ" index=75 desc="Fullscreen 20Hz-18kHz spectrum analyzer like Pulse Audio Easy Effects" author="Visualizer"

// Fullscreen 20Hz to 18kHz spectrum analyzer
// 64 bands covering the full audible spectrum

float eqRect(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Log frequency to linear position (20Hz - 18kHz)
float freqToX(float freq) {
    float minLog = log2(20.0);
    float maxLog = log2(18000.0);
    return (log2(freq) - minLog) / (maxLog - minLog);
}

// Map band index to frequency
float bandToFreq(int band, int totalBands) {
    float t = float(band) / float(totalBands - 1);
    float minLog = log2(20.0);
    float maxLog = log2(18000.0);
    return pow(2.0, mix(minLog, maxLog, t));
}

vec4 renderAudioEQ(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Fullscreen - use full UV range
    vec2 uv = (st - 0.5) * 2.0; // -1 to 1 range
    
    // Dark background
    vec3 col = vec3(0.005, 0.005, 0.008);
    
    // 64 frequency bands for smooth spectrum (20Hz - 18kHz)
    const int numBands = 64;
    float totalWidth = 2.0; // Full width (-1 to 1)
    float gap = 0.003;
    float barW = (totalWidth / float(numBands)) - gap;
    float spacing = barW + gap;
    float startX = -1.0 + barW * 0.5;
    float maxHeight = 1.8; // Taller bars for fullscreen
    float baseY = -0.9;
    
    // Fallback animation
    float t = time * 2.5;
    float anim[64];
    for (int i = 0; i < 64; i++) {
        // Different frequencies for each band
        float freq = 0.3 + float(i) * 0.05;
        // Simulate spectrum: more activity in mids
        float activity = 1.0 - abs(float(i) - 32.0) / 32.0;
        anim[i] = 0.05 + activity * 0.25 + sin(t * freq) * 0.1 * activity;
    }
    
    // Audio inputs with minimums
    float b = max(bass, 0.1);
    float m = max(mid, 0.08);
    float h = max(high, 0.06);
    float e = max(energy, 0.15);
    
    // Generate 64 frequency bands (20Hz - 18kHz)
    float bands[64];
    
    // Fill bands based on frequency distribution
    for (int i = 0; i < 64; i++) {
        float f = bandToFreq(i, 64);
        float normPos = float(i) / 63.0;
        
        // Frequency ranges:
        // 20-60 Hz: Sub-bass (bands 0-5)
        // 60-250 Hz: Bass (bands 6-15)
        // 250-500 Hz: Low-mids (bands 16-23)
        // 500 Hz - 2 kHz: Mids (bands 24-39)
        // 2-6 kHz: High-mids (bands 40-51)
        // 6-18 kHz: Highs (bands 52-63)
        
        float intensity;
        if (f < 60.0) {
            // Sub-bass
            intensity = b * (1.0 - float(i) * 0.08) + e * 0.1;
        } else if (f < 250.0) {
            // Bass
            float blend = (f - 60.0) / 190.0;
            intensity = mix(b * 0.9, b * 0.5, blend) + m * blend * 0.2 + e * 0.08;
        } else if (f < 500.0) {
            // Low-mids
            float blend = (f - 250.0) / 250.0;
            intensity = mix(b * 0.4, m * 0.7, blend) + e * 0.06;
        } else if (f < 2000.0) {
            // Mids
            float blend = (f - 500.0) / 1500.0;
            intensity = mix(m * 0.75, m * 0.95, blend) + h * blend * 0.15 + e * 0.05;
        } else if (f < 6000.0) {
            // High-mids
            float blend = (f - 2000.0) / 4000.0;
            intensity = mix(m * 0.5, h * 0.85, blend) + e * 0.07;
        } else {
            // Highs
            float blend = (f - 6000.0) / 12000.0;
            intensity = h * (1.0 - blend * 0.3) + e * 0.04;
        }
        
        // Add temporal variation
        float wave = sin(time * 3.0 + float(i) * 0.2) * 0.02;
        float fallback = anim[i] * 0.5;
        bands[i] = clamp(max(intensity + wave, fallback), 0.0, 1.0);
    }
    
    // Draw frequency bars
    for (int i = 0; i < numBands; i++) {
        float xPos = startX + float(i) * spacing;
        float barHeight = bands[i] * maxHeight;
        
        if (barHeight < 0.005) continue;
        
        vec2 barCenter = vec2(xPos, baseY + barHeight * 0.5);
        vec2 barSize = vec2(barW * 0.5, barHeight * 0.5);
        
        // Background track
        float trackY = baseY + maxHeight * 0.5;
        float track = eqRect(uv - vec2(xPos, trackY), vec2(barW * 0.5, maxHeight * 0.5));
        float trackMask = smoothstep(0.01, 0.0, track);
        col = mix(col, vec3(0.02, 0.02, 0.025), trackMask * 0.5);
        
        // Active bar
        float d = eqRect(uv - barCenter, barSize);
        float barMask = smoothstep(0.002, 0.0, d);
        
        if (barMask > 0.0) {
            vec3 barColor;
            float level = bands[i];
            float normPos = float(i) / 63.0;
            
            // Full spectrum color mapping (20Hz - 18kHz)
            // Deep red (sub) -> orange -> yellow -> green -> cyan -> blue -> violet (ultra)
            if (normPos < 0.15) {
                // 20-80 Hz: Deep red to orange
                barColor = mix(vec3(0.8, 0.0, 0.0), vec3(1.0, 0.4, 0.0), normPos / 0.15);
            } else if (normPos < 0.30) {
                // 80-200 Hz: Orange to yellow
                barColor = mix(vec3(1.0, 0.4, 0.0), vec3(1.0, 0.9, 0.0), (normPos - 0.15) / 0.15);
            } else if (normPos < 0.50) {
                // 200-800 Hz: Yellow to green
                barColor = mix(vec3(1.0, 0.9, 0.0), vec3(0.2, 1.0, 0.2), (normPos - 0.30) / 0.20);
            } else if (normPos < 0.70) {
                // 800 Hz - 3 kHz: Green to cyan
                barColor = mix(vec3(0.2, 1.0, 0.2), vec3(0.0, 0.9, 1.0), (normPos - 0.50) / 0.20);
            } else if (normPos < 0.85) {
                // 3-8 kHz: Cyan to blue
                barColor = mix(vec3(0.0, 0.9, 1.0), vec3(0.2, 0.4, 1.0), (normPos - 0.70) / 0.15);
            } else {
                // 8-18 kHz: Blue to violet
                barColor = mix(vec3(0.2, 0.4, 1.0), vec3(0.6, 0.2, 1.0), (normPos - 0.85) / 0.15);
            }
            
            // Brightness based on level
            barColor *= (0.6 + level * 0.8);
            
            // Add subtle palette influence
            barColor = mix(barColor, uPrimaryColor, 0.05);
            
            // Glow at top
            float topY = barCenter.y + barSize.y;
            float topGlow = smoothstep(0.0, 0.03, topY - uv.y) * smoothstep(0.0, 0.015, uv.y - (topY - 0.03));
            barColor += vec3(0.5) * topGlow * level;
            
            col = mix(col, barColor, barMask);
        }
    }
    
    // Horizontal dB grid lines
    for (int i = 0; i <= 12; i++) {
        float y = baseY + float(i) * (maxHeight / 12.0);
        float intensity = (i == 6) ? 0.4 : 0.15;
        float line = smoothstep(0.001, 0.0, abs(uv.y - y));
        col = mix(col, vec3(0.1, 0.1, 0.12), line * intensity);
    }
    
    // Frequency markers at bottom (20Hz, 100Hz, 1kHz, 10kHz, 18kHz)
    float markers[5] = float[](0.0, 0.22, 0.5, 0.78, 1.0);
    for (int i = 0; i < 5; i++) {
        float x = -1.0 + markers[i] * 2.0;
        float marker = smoothstep(0.005, 0.0, abs(uv.x - x));
        col = mix(col, vec3(0.15, 0.15, 0.18), marker * 0.6);
        
        // Small vertical tick
        float tick = smoothstep(0.0, 0.02, uv.y - baseY) * smoothstep(0.06, 0.0, uv.y - baseY);
        tick *= marker > 0.0 ? 1.0 : 0.0;
        col = mix(col, vec3(0.2, 0.2, 0.25), tick);
    }
    
    return vec4(col, 1.0);
}
