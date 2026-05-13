// Breathing layer - rhythmic expansion effect
// Based on glslify quintic-out easing function

float qinticOut(float t) {
  return 1.0 - (pow(abs(t - 1.0), 5.0));
}

void main() {
    vec2 uv = (vUV - 0.5) * vec2(aspectRatio, 1.0);
    
    uv *= 3.0;
	
    float l = length(uv);
    float t = qinticOut(fract(uTime * 0.5));
    
    float breath = sin(t * 6.28 - l * 3.0);
    breath *= 1.0 - clamp(l, 0.0, 1.0);
    
    // React to audio
    float audioReactivity = uEnergy * 0.3 + uBass * 0.4 + uOnset * 0.3;
    breath *= (1.0 + audioReactivity * 2.0);
    
    // Apply scene colors
    vec3 sceneColor = mix(uScenePrimary, uSceneSecondary, uSceneBlend);
    fragColor = vec4(sceneColor * breath, breath * uOpacity);
}
