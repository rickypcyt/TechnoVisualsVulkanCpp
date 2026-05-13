// @EFFECT name="Breathing" index=34 desc="Rhythmic expansion effect with audio reactivity" author="System"
// Breathing Layer - Rhythmic expansion effect
// Based on glslify quintic-out easing function

float qinticOut(float t) {
  return 1.0 - (pow(abs(t - 1.0), 5.0));
}

vec4 renderBreathing(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st;
    uv *= 3.0;
	
    float l = length(uv);
    float t = qinticOut(fract(time * 0.5));
    
    // Enhanced audio reactivity
    float bassResponse = smoothstep(0.0, 1.0, bass) * 2.0;
    float midResponse = smoothstep(0.0, 1.0, mid) * 1.5;
    float highResponse = smoothstep(0.0, 1.0, high) * 1.0;
    float energyResponse = smoothstep(0.0, 1.0, energy) * 1.8;
    
    // Combined audio influence with different weights
    float audioReactivity = bassResponse * 0.4 + midResponse * 0.3 + highResponse * 0.2 + energyResponse * 0.5;
    audioReactivity = pow(audioReactivity, 1.5); // Enhance contrast
    
    // Base breathing pattern
    float breath = sin(t * 6.28 - l * 3.0);
    breath *= 1.0 - clamp(l, 0.0, 1.0);
    
    // Apply audio modulation to multiple aspects
    float audioModulation = 1.0 + audioReactivity * 3.0; // Stronger response
    breath *= audioModulation;
    
    // Add tempo-based pulsing
    float tempoPulse = sin(time * tempo * 0.1) * 0.2 * audioReactivity;
    breath += tempoPulse * (1.0 - l);
    
    // Enhanced color response
    vec3 baseColor = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend, 0.0, 1.0));
    
    // Dynamic color shifting based on audio
    vec3 bassColor = mix(baseColor, vec3(1.0, 0.3, 0.2), bassResponse * 0.4); // Red shift for bass
    vec3 midColor = mix(baseColor, vec3(0.3, 1.0, 0.4), midResponse * 0.3);   // Green shift for mid
    vec3 highColor = mix(baseColor, vec3(0.2, 0.4, 1.0), highResponse * 0.3); // Blue shift for high
    
    vec3 finalColor = mix(mix(bassColor, midColor, 0.5), highColor, 0.3);
    finalColor *= breath;
    
    // Enhanced alpha with audio response
    float baseAlpha = clamp(0.3 + breath * 0.7, 0.0, 1.0);
    float audioAlpha = 1.0 + audioReactivity * 2.0;
    float alpha = baseAlpha * audioAlpha;
    alpha = clamp(alpha, 0.0, 1.0);
    
    return vec4(finalColor, alpha);
}
