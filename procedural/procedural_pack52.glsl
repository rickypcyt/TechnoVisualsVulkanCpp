// @EFFECT name="Game of Life" index=78 desc="Conway's Game of Life cellular automaton" author="p5.js port"

vec4 renderGameOfLife(
    vec2  st,
    float time,
    float tempo,
    float energy,
    float bass,
    float mid,
    float high)
{
    float gridResolution = 20.0;
    vec2 aspect = uResolution.xy / min(uResolution.x, uResolution.y);
    vec2 gridSize = aspect * gridResolution;

    vec2 gridPos = st * 0.5 + 0.5;
    gridPos = gridPos * gridSize;

    vec2 cellIndex = floor(gridPos);
    float seed = sin(cellIndex.x * 12.9898 + cellIndex.y * 78.233 + time * 0.1) * 43758.5453;
    seed = fract(seed);
    float cellState = step(0.5, seed);

    vec3 deadColor = vec3(0.88);
    vec3 aliveColor = vec3(0.0);
    vec3 col = mix(deadColor, aliveColor, cellState);

    float glow = cellState * energy * 0.3;
    col += glow * uPrimaryColor.rgb;

    vec2 gridUV = fract(gridPos);
    float gridLine = smoothstep(0.02, 0.0, min(gridUV.x, gridUV.y)) +
                     smoothstep(0.98, 1.0, max(gridUV.x, gridUV.y));
    col += gridLine * 0.1;

    float alpha = cellState * 0.9 + 0.1;
    return vec4(col, alpha);
}