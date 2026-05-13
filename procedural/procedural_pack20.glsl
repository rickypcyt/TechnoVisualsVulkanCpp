// @author ciphrd <https://instagram.com/ciphrd>
// @license MIT
//
// Pixel sorting shader - simplified single buffer version
// Based on multi-buffer implementation with vector field sorting

// returns a grayscale based on the average of the 3 components
float gscale (in vec3 color) {
	return (color.r + color.g + color.b) / 3.;
}

vec2 getSortDirection(vec2 uv, float time) {
    vec2 dir = vec2(1.0, 1.0);
    
    // we differentiate 1/2 pixels on the y axis
    vec2 iuv = floor(uv * iResolution.xy);
    float r = mod(iuv.y, 2.0) * 2. - 1.; // r = -1 or 1.0
    
    // here goes the rules to define the displacement map
    dir *= r * (mod(time * 10.0, 2.0) * 2. - 1.);
    
    // we create "bands", and swap correctly the direction 
    float b = mod(floor(uv.y * 5.0), 2.0) * 2. - 1.;
    dir *= vec2(b, 1.0);
    
    // we invert on the left to preserve consistency
    dir *= round(uv.x) * 2. - 1.;
    
    return dir;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy / iResolution.xy;
    
    // threshold for sorting
    float threshold = 0.04;
    
    // initialization - use previous frame as base
    vec4 baseColor = texture(iChannel0, uv);
    
    // get sort direction
    vec2 dir = getSortDirection(uv, iTime);
    vec2 dr = dir / iResolution.xy; // pixel space to square space
    vec2 p = uv + dr;
    
    // we make p loop on x
    if (p.x < 0.0) p.x = 1. - p.x;
    if (p.x > 1.0) p.x = fract(p.x);
    
    // sample the pixels to test
    vec4 actv = baseColor;
    vec4 comp = texture(iChannel0, p);
    
    // if we are next to a border on the Y-axis, prevent the sort from happening
    if (uv.y+dr.y < 0.0 || uv.y+dr.y > 1.0) {
    	fragColor = actv;
        return;
    }
    
    // we can sort the texel with the other one
    vec4 color = actv;
    float gAct = gscale(actv.rgb);
    float gCom = gscale(comp.rgb);
    
    // we separate the vector field dir into 2 "categories"
    float classed = sign(dr.x*2. + dr.y);
    
    // determine sort direction based on position and time
    bool sortForward = mod(floor(uv.y * iResolution.y) + iTime * 10.0, 2.0) > 0.5;
    
    if (classed < 0.0) {
        // given the sort direction, we sort one way or the other
		if (sortForward) {
			if (gCom > threshold && gAct > gCom) {
				color = comp;
			}
		} else {
			if (gAct > threshold && gAct < gCom) {
				color = comp;
			}
		}
	} else {
		if (sortForward) {
			if (gAct > threshold && gAct < gCom) {
				color = comp;
			}
		} else {
			if (gCom > threshold && gAct > gCom) {
				color = comp;
			}
		}
	}
    
    // add some visual interest with time-based color shifting
    float timeMod = sin(iTime * 0.5) * 0.5 + 0.5;
    color.rgb += vec3(timeMod * 0.1, (1.0 - timeMod) * 0.1, sin(iTime) * 0.05);
    
    fragColor = color;
}
