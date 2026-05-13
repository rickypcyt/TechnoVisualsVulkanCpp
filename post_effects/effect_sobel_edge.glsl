#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 col = sceneColor.rgb;
    
    /*** Sobel kernels ***/
    // Note: GLSL's mat3 is COLUMN-major ->  mat3[col][row]
    mat3 sobelX = mat3(-1.0, -2.0, -1.0,
                       0.0,  0.0, 0.0,
                       1.0,  2.0,  1.0);
    mat3 sobelY = mat3(-1.0,  0.0,  1.0,
                       -2.0,  0.0, 2.0,
                       -1.0,  0.0,  1.0);  
    
    float sumX = 0.0;	// x-axis change
    float sumY = 0.0;	// y-axis change
    
    vec2 fragCoord = vUV * uResolution.xy;
    
    for(int i = -1; i <= 1; i++)
    {
        for(int j = -1; j <= 1; j++)
        {
            float x = (fragCoord.x + float(i))/uResolution.x;	
    		float y =  (fragCoord.y + float(j))/uResolution.y;
            
            sumX += length(texture( uScene, vec2(x, y) ).xyz) * float(sobelX[1+i][1+j]);
            sumY += length(texture( uScene, vec2(x, y) ).xyz) * float(sobelY[1+i][1+j]);
        }
    }
    
    float g = abs(sumX) + abs(sumY);
    
    vec3 edgeColor = (g > 1.0) ? vec3(1.0) : vec3(0.0);
    
    vec3 result = mix(sceneColor.rgb, edgeColor, uStrength);
    FragColor = vec4(result, sceneColor.a);
}
