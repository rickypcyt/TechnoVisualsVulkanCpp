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
    const int kGridSize = 9;
    const int kCenter = 4;
    const int kSteps = 4;
    float cells[kGridSize][kGridSize];
    float nextCells[kGridSize][kGridSize];

    vec2 aspect = uResolution / max(min(uResolution.x, uResolution.y), 1.0);
    vec2 gridSize = aspect * 20.0;
    vec2 gridPos = (st * 0.5 + 0.5) * gridSize;
    ivec2 cellIndex = ivec2(floor(gridPos));
    float epoch = floor(time / 8.0);

    for (int y = 0; y < kGridSize; ++y) {
        for (int x = 0; x < kGridSize; ++x) {
            ivec2 seedCell = cellIndex + ivec2(x - kCenter, y - kCenter);
            float seed = sin(float(seedCell.x) * 12.9898 +
                             float(seedCell.y) * 78.233 + epoch * 37.719) * 43758.5453;
            cells[y][x] = step(0.68, fract(seed));
        }
    }

    for (int stepIndex = 0; stepIndex < kSteps; ++stepIndex) {
        for (int y = 1; y < kGridSize - 1; ++y) {
            for (int x = 1; x < kGridSize - 1; ++x) {
                float neighbors = 0.0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox != 0 || oy != 0) {
                            neighbors += cells[y + oy][x + ox];
                        }
                    }
                }
                float alive = cells[y][x];
                nextCells[y][x] = (alive > 0.5)
                    ? step(2.0, neighbors) * step(neighbors, 3.0)
                    : step(3.0, neighbors) * step(neighbors, 3.0);
            }
        }
        for (int edge = 0; edge < kGridSize; ++edge) {
            nextCells[0][edge] = 0.0;
            nextCells[kGridSize - 1][edge] = 0.0;
            nextCells[edge][0] = 0.0;
            nextCells[edge][kGridSize - 1] = 0.0;
        }
        for (int y = 0; y < kGridSize; ++y) {
            for (int x = 0; x < kGridSize; ++x) {
                cells[y][x] = nextCells[y][x];
            }
        }
    }

    float cellState = cells[kCenter][kCenter];
    vec3 deadColor = vec3(0.88);
    vec3 aliveColor = mix(vec3(0.0), uPrimaryColor.rgb, 0.35 + energy * 0.65);
    vec3 col = mix(deadColor, aliveColor, cellState);

    vec2 gridUV = fract(gridPos);
    float gridLine = smoothstep(0.02, 0.0, min(gridUV.x, gridUV.y)) +
                     smoothstep(0.98, 1.0, max(gridUV.x, gridUV.y));
    col += gridLine * (0.08 + high * 0.08);

    float alpha = cellState * 0.9 + 0.1;
    return vec4(col, alpha);
}
