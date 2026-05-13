#include "post_common.glsl"

vec2 mirror(vec2 x)
{
	return abs(fract(x/2.0) - 0.5)*2.0;	
}

// Using the version that adds up colors from all iterations
void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec2 uv = -1.0 + 2.0*vUV;

    float a = uTime*0.2;
    vec4 color = vec4(0.0);
    for (float i = 1.0; i < 10.0; i += 1.0) {
        uv = vec2(sin(a)*uv.y - cos(a)*uv.x, sin(a)*uv.x + cos(a)*uv.y);
        uv = mirror(uv);
        
        // These two lines can be changed for slightly different effects
        // This is just something simple that looks nice
        a += i;
        a /= i;
        color += texture(uScene, mirror(uv*2.0)) * 10.0/i;
    }
    
    vec3 result = color.rgb / 28.289;
    vec3 finalColor = mix(sceneColor.rgb, result, uStrength);
    
    FragColor = vec4(finalColor, sceneColor.a);
}
