#include "post_common.glsl"

const float squares = 10.0;
const float amt = 0.2;
const float offset = 0.125;

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    
    vec2 tc = vUV;
    tc.x *= uResolution.x / uResolution.y;
    
    vec2 tile = fract(tc * squares);
    
    vec3 result = texture(uScene, vUV+(tile*amt)-offset ).rgb;
    vec3 finalColor = mix(sceneColor.rgb, result, uStrength);
    
    FragColor = vec4(finalColor, sceneColor.a);
}
