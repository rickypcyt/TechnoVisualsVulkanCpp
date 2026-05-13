#include "post_common.glsl"

#define PI 3.14159265359
#define PHI 1.61803398875

#define SAMPLES 5
#define BLOOM_RADIUS 30.0

const mat3 ACESInputMat = mat3(
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777
);

const mat3 ACESOutputMat = mat3(
     1.60475, -0.53108, -0.07367,
    -0.10208,  1.10813, -0.00605,
    -0.00327, -0.07276,  1.07602
);

vec3 RRTAndODTFit( in vec3 v ) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACESFitted( in vec3 color ) {
    color = color * ACESInputMat;
    color = RRTAndODTFit(color);
    color = color * ACESOutputMat;
    color = clamp(color, 0.0, 1.0);
    return color;
}

vec3 hash33(vec3 p3) {
	p3 = fract(p3 * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yxz+33.33);
    return fract((p3.xxy + p3.yxx)*p3.zyx);
}

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec2 fragCoord = vUV * uResolution;
    
    vec3 bloom = vec3(0);
    float totfac = 0.0;
    
    vec3 rnd = hash33(vec3(fragCoord, uTime));
    float offset = rnd.x*2.0*PI;
    
    // bloom
    for (int i = 0 ; i < SAMPLES ; i++) {
        float theta = 2.0*PI*PHI*float(i) + offset;
        float radius = sqrt(float(i)) / sqrt(float(SAMPLES));
        radius *= BLOOM_RADIUS;
        vec2 offset = vec2(cos(theta), sin(theta))*radius;
        vec2 delta = vec2( 1.0+exp(-abs(offset.y)*0.1) , 0.5);
        offset *= delta;
        vec4 here = textureGrad(uScene,(fragCoord+offset)/uResolution.xy, 
                                vec2(0.001, 0)*BLOOM_RADIUS, vec2(0, 0.001)*BLOOM_RADIUS);
        float fact = smoothstep(BLOOM_RADIUS, 0.0, radius);
        bloom += here.rgb*0.05*fact;
        totfac += fact;
    }
    
    bloom /= totfac;
    
    vec2 uv = fragCoord/uResolution.xy;
    vec2 mo = uv*2.0-1.0;
    mo *= 0.01;
    vec4 result;
    result.r = textureLod(uScene, uv-mo*0.1, 0.0).r;
    result.g = textureLod(uScene, uv-mo*0.6, 0.0).g;
    result.b = textureLod(uScene, uv-mo*1.0, 0.0).b;
    
    result.rgb += bloom * bloom * 2.0; // Reduced intensity to prevent white washout
    vec2 vi = fragCoord / uResolution.xy * 2.0 - 1.0;
    result.rgb *= (1.0-sqrt(dot(vi,vi)*0.45));
    
    // Invert the effect: make bright areas dark and add subtle glow to dark areas
    vec3 inverted = 1.0 - result.rgb;
    result.rgb = mix(result.rgb, inverted, 0.7); // Mostly inverted
    
    result.rgb = ACESFitted(result.rgb);
    result.rgb = pow( result.rgb, vec3(1.0/2.2) );
    result.rgb += (rnd-0.5)*0.05; // Reduced noise
    
    vec3 finalColor = mix(sceneColor.rgb, result.rgb, uStrength);
    FragColor = vec4(finalColor, sceneColor.a);
}
