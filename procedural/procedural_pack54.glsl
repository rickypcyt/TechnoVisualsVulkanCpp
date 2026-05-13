// @EFFECT name="Circular Cellular Automata" index=80 desc="2D cellular automata on circular grid with multiple rule sets" author="p5.js port"


// Get cell state at grid position with toroidal wrapping
float getCellState(vec2 gridPos, vec2 gridSize, float time, int patternIndex) {
    vec2 wrapped = mod(gridPos, gridSize);
    float seed = hash21(wrapped + vec2(float(patternIndex), floor(time * 0.1)));
    return seed;
}

// Count selected neighbors based on pattern
float countSelectedNeighbors(vec2 gridPos, vec2 gridSize, float time, int patternIndex) {
    float total = 0.0;
    
    // Neighbor selection patterns (8 neighbors)
    // Pattern order: left-up, up, right-up, left, right, left-down, down, right-down
    int patterns[152]; // 19 patterns × 8 neighbors
    
    // Pattern 0
    patterns[0] = 0; patterns[1] = 1; patterns[2] = 1; patterns[3] = 1;
    patterns[4] = 1; patterns[5] = 1; patterns[6] = 1; patterns[7] = 1;
    // Pattern 1
    patterns[8] = 1; patterns[9] = 0; patterns[10] = 1; patterns[11] = 1;
    patterns[12] = 1; patterns[13] = 1; patterns[14] = 1; patterns[15] = 1;
    // Pattern 2
    patterns[16] = 1; patterns[17] = 1; patterns[18] = 0; patterns[19] = 1;
    patterns[20] = 1; patterns[21] = 1; patterns[22] = 1; patterns[23] = 1;
    // Pattern 3
    patterns[24] = 1; patterns[25] = 1; patterns[26] = 1; patterns[27] = 0;
    patterns[28] = 1; patterns[29] = 1; patterns[30] = 1; patterns[31] = 1;
    // Pattern 4
    patterns[32] = 1; patterns[33] = 1; patterns[34] = 1; patterns[35] = 1;
    patterns[36] = 0; patterns[37] = 1; patterns[38] = 1; patterns[39] = 1;
    // Pattern 5
    patterns[40] = 1; patterns[41] = 1; patterns[42] = 1; patterns[43] = 1;
    patterns[44] = 1; patterns[45] = 0; patterns[46] = 1; patterns[47] = 1;
    // Pattern 6
    patterns[48] = 1; patterns[49] = 1; patterns[50] = 1; patterns[51] = 1;
    patterns[52] = 1; patterns[53] = 1; patterns[54] = 0; patterns[55] = 1;
    // Pattern 7
    patterns[56] = 1; patterns[57] = 1; patterns[58] = 1; patterns[59] = 1;
    patterns[60] = 1; patterns[61] = 1; patterns[62] = 1; patterns[63] = 0;
    // Pattern 8
    patterns[64] = 1; patterns[65] = 1; patterns[66] = 1; patterns[67] = 1;
    patterns[68] = 1; patterns[69] = 1; patterns[70] = 1; patterns[71] = 1;
    // Pattern 9
    patterns[72] = 0; patterns[73] = 0; patterns[74] = 1; patterns[75] = 1;
    patterns[76] = 1; patterns[77] = 1; patterns[78] = 1; patterns[79] = 1;
    // Pattern 10
    patterns[80] = 1; patterns[81] = 0; patterns[82] = 0; patterns[83] = 1;
    patterns[84] = 1; patterns[85] = 1; patterns[86] = 1; patterns[87] = 1;
    // Pattern 11
    patterns[88] = 1; patterns[89] = 1; patterns[90] = 1; patterns[91] = 1;
    patterns[92] = 1; patterns[93] = 1; patterns[94] = 0; patterns[95] = 0;
    // Pattern 12
    patterns[96] = 1; patterns[97] = 1; patterns[98] = 1; patterns[99] = 1;
    patterns[100] = 1; patterns[101] = 0; patterns[102] = 0; patterns[103] = 1;
    // Pattern 13
    patterns[104] = 1; patterns[105] = 1; patterns[106] = 1; patterns[107] = 0;
    patterns[108] = 1; patterns[109] = 1; patterns[110] = 0; patterns[111] = 1;
    // Pattern 14
    patterns[112] = 0; patterns[113] = 1; patterns[114] = 0; patterns[115] = 1;
    patterns[116] = 1; patterns[117] = 1; patterns[118] = 1; patterns[119] = 1;
    // Pattern 15
    patterns[120] = 1; patterns[121] = 1; patterns[122] = 0; patterns[123] = 0;
    patterns[124] = 1; patterns[125] = 1; patterns[126] = 1; patterns[127] = 1;
    // Pattern 16
    patterns[128] = 0; patterns[129] = 1; patterns[130] = 1; patterns[131] = 0;
    patterns[132] = 1; patterns[133] = 1; patterns[134] = 1; patterns[135] = 1;
    // Pattern 17
    patterns[136] = 1; patterns[137] = 1; patterns[138] = 1; patterns[139] = 1;
    patterns[140] = 1; patterns[141] = 0; patterns[142] = 1; patterns[143] = 0;
    // Pattern 18
    patterns[144] = 0; patterns[145] = 0; patterns[146] = 0; patterns[147] = 0;
    patterns[148] = 1; patterns[149] = 0; patterns[150] = 0; patterns[151] = 0;
    
    int idx = patternIndex * 8;
    
    // Check each neighbor based on pattern
    for (int i = 0; i < 8; i++) {
        if (patterns[idx + i] == 1) {
            vec2 offset = vec2(0.0);
            if (i == 0) offset = vec2(-1.0, -1.0);      // left-up
            else if (i == 1) offset = vec2(0.0, -1.0); // up
            else if (i == 2) offset = vec2(1.0, -1.0);  // right-up
            else if (i == 3) offset = vec2(-1.0, 0.0);  // left
            else if (i == 4) offset = vec2(1.0, 0.0);   // right
            else if (i == 5) offset = vec2(-1.0, 1.0);  // left-down
            else if (i == 6) offset = vec2(0.0, 1.0);   // down
            else if (i == 7) offset = vec2(1.0, 1.0);    // right-down
            
            total += getCellState(gridPos + offset, gridSize, time, patternIndex);
        }
    }
    
    return total;
}

// Apply cellular automata rules
float applyCARules(float state, float total, float average, float previous) {
    if (average >= 254.0) {
        return 0.0;
    } else if (average <= 1.0) {
        return 255.0;
    } else {
        float nextState = state + average;
        if (previous > 0.0) nextState -= previous;
        return clamp(nextState, 0.0, 255.0);
    }
}

vec4 renderCircularCellularAutomata(
    vec2  st,
    float time,
    float tempo,
    float energy,
    float bass,
    float mid,
    float high)
{
    vec3 color = vec3(0.0);
    float alpha = 0.0;
    
    // Grid parameters from p5.js
    const int columns = 140;
    const int rows = 30;
    const int numPatterns = 19;
    
    // Audio-reactive pattern selection
    float patternCycle = mod(floor(time * 0.2), float(numPatterns));
    int patternIndex = int(patternCycle);
    
    // Audio-reactive evolution
    float evolutionSpeed = 0.5 + bass * 0.5;
    float timeStep = floor(time * evolutionSpeed);
    
    // Convert screen position to polar coordinates
    vec2 center = vec2(0.5);
    vec2 fromCenter = st - center;
    float dist = length(fromCenter);
    float angle = atan(fromCenter.y, fromCenter.x) + 3.14159; // 0 to 2PI
    
    // Map to circular grid
    float maxRadius = 0.45;
    float minRadius = 0.05;
    
    if (dist < minRadius || dist > maxRadius) {
        return vec4(0.0, 0.0, 0.0, 0.0);
    }
    
    // Calculate ring and position on ring
    float ringFraction = (dist - minRadius) / (maxRadius - minRadius);
    int ring = int(ringFraction * float(rows));
    
    // Calculate position on ring (column)
    float angleFraction = angle / 6.28318;
    int col = int(angleFraction * float(columns));
    
    // Get cell state
    vec2 gridPos = vec2(float(col), float(ring));
    vec2 gridSize = vec2(float(columns), float(rows));
    
    float currentState = getCellState(gridPos, gridSize, timeStep, patternIndex);
    float previousState = getCellState(gridPos, gridSize, timeStep - 1.0, patternIndex);
    
    // Count selected neighbors
    float total = countSelectedNeighbors(gridPos, gridSize, timeStep, patternIndex);
    float average = total / 8.0;
    
    // Apply rules
    float cellState = applyCARules(currentState, total, average, previousState);
    
    // Smooth transition between states
    float blend = fract(time * evolutionSpeed);
    float smoothState = mix(currentState, cellState, blend);
    
    // Normalize state to 0-1
    float normalizedState = smoothState / 255.0;
    
    // Binary cell state for proper cellular automata visualization
    float cellActive = step(0.5, normalizedState);
    
    // Color: use user colors instead of hardcoded white
    vec3 bgColor = uPrimaryColor * 0.3;
    vec3 cellColor = mix(uPrimaryColor, uSecondaryColor, 0.7);
    
    // Audio-reactive brightness
    cellColor *= (0.8 + energy * 0.4);
    
    // Mix based on cell state
    vec3 rgb = mix(bgColor, cellColor, cellActive);
    
    // Alpha based on cell state
    alpha = cellActive * 0.9 + 0.1;
    
    // Add circular grid lines (decorative)
    float gridLine = smoothstep(0.02, 0.0, fract(ringFraction * float(rows))) +
                     smoothstep(0.98, 1.0, fract(ringFraction * float(rows)));
    rgb += gridLine * 0.1 * uSecondaryColor;
    
    return vec4(rgb, alpha);
}
