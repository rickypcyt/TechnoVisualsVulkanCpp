// @EFFECT name="Cellular Simulation" index=107 desc="Cellular automaton simulation with procedural feedback" author="System"

const float cell_pi = 3.1415;
const float cell_pi2 = cell_pi / 2.0;

float cell_random(vec2 coord) {
    return fract(sin(dot(coord, vec2(12.9898, 78.233))) * 43758.5453);
}

vec4 cell_get_pixel(vec2 uv, float x_offset, float y_offset) {
    vec2 offset = vec2(x_offset, y_offset) / uResolution.xy;
    vec2 sampleUV = uv + offset;
    float r = abs(sin(sampleUV.x * 10.0 + uTime * 5.0));
    float g = abs(sin(sampleUV.y * 8.0 + uTime * 4.0));
    float b = abs(sin((sampleUV.x + sampleUV.y) * 12.0 + uTime * 3.0));
    return vec4(r, g, b, 1.0);
}

float step_simulation(vec2 uv, float time) {
    float val = cell_get_pixel(uv, 0.0, 0.0).r;
    
    val += cell_random(uv) * val * 0.15;
    
    float val1 = cell_get_pixel(uv, val, 0.0).r;
    float val2 = cell_get_pixel(uv, -val, 0.0).r;
    float val3 = cell_get_pixel(uv, 0.0, -val).r;
    float val4 = cell_get_pixel(uv, 0.0, val).r;
    
    vec2 offset = vec2(sin(val1 - val2 + cell_pi) * val * 0.4, cos(val3 - val4 - cell_pi2) * val * 0.4) / uResolution.xy;
    val = cell_get_pixel(uv + offset, 0.0, 0.0).r;
    
    val *= 1.0001;
    
    return val;
}

vec4 renderCellularSimulation(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st;
    float val = step_simulation(uv, time);
    
    // Initialize with procedural pattern
    if (time < 0.1) {
        float dist = length(uv - 0.5) * 2.0;
        val = cell_random(uv) * length(uResolution.xy) / 100.0 + smoothstep(length(uResolution.xy) / 2.0, 0.5, dist) * 25.0;
    }
    
    // Add mouse-like interaction based on time (simulating cursor movement)
    vec2 mousePos = vec2(0.5 + sin(time * 3.0) * 0.3, 0.5 + cos(time * 4.0) * 0.3);
    float mouseDist = length(mousePos - uv) * length(uResolution.xy);
    val += smoothstep(length(uResolution.xy) / 10.0, 0.5, mouseDist);
    
    // Create color from simulation value
    vec3 col = vec3(val);
    col = mix(col, col.gbr, 0.1 + sin(time * 2.0) * 0.1);
    col = clamp(col, 0.0, 1.0);
    
    return vec4(col, 1.0);
}
