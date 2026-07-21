#include "post_common.glsl"

/*
  Pixel Sorting post-effect — congruent with the red grid cave shader.
  Vertical-dominant sorting creates the "colors sliding down through grid" look.
  Horizontal sort adds lateral smear. Luminance mask preserves the vivid red
  highlights and black background. Red channel is prioritized in the sort
  metric so the red grid structure stays coherent.
*/

float sortMetric(vec3 c) {
    // Weight red heavily so the red grid structure sorts coherently
    return c.r * 0.7 + dot(c, vec3(0.15, 0.1, 0.05));
}

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec2 texel = 1.0 / uResolution.xy;

    float strength = clamp(uStrength, 0.0, 1.0);
    int   windowSize = int(mix(4.0, 50.0, strength));
    // Higher threshold by default — only sort brighter regions, leave most of the image untouched
    float threshold  = mix(0.25, 0.65, uBassLevel);
    float maskSoft   = 0.08;

    float curMetric = sortMetric(sceneColor.rgb);

    // --- Vertical sort (dominant): colors slide downward through the grid ---
    int vRank = 0;
    int vTotal = 0;
    for (int i = -50; i <= 50; ++i) {
        if (abs(i) > windowSize) continue;
        vec2 sUV = clamp(vUV + vec2(0.0, float(i) * texel.y), 0.0, 1.0);
        float sMetric = sortMetric(texture(uScene, sUV).rgb);
        vTotal++;
        if (sMetric < curMetric) vRank++;
    }
    float vOffset = float(vRank - vTotal / 2) * texel.y;
    vec2 vUV2 = clamp(vUV + vec2(0.0, vOffset), 0.0, 1.0);
    vec3 vColor = texture(uScene, vUV2).rgb;

    // --- Horizontal sort (subtle): lateral smear for organic streaks ---
    int hWindow = max(2, windowSize / 3);
    int hRank = 0;
    int hTotal = 0;
    for (int i = -20; i <= 20; ++i) {
        if (abs(i) > hWindow) continue;
        vec2 sUV = clamp(vUV + vec2(float(i) * texel.x, 0.0), 0.0, 1.0);
        float sMetric = sortMetric(texture(uScene, sUV).rgb);
        hTotal++;
        if (sMetric < curMetric) hRank++;
    }
    float hOffset = float(hRank - hTotal / 2) * texel.x;
    vec2 hUV = clamp(vUV + vec2(hOffset, 0.0), 0.0, 1.0);
    vec3 hColor = texture(uScene, hUV).rgb;

    // Blend: vertical dominant (grid slide), horizontal subtle
    vec3 sortedColor = mix(vColor, hColor, 0.25);

    // --- Luminance mask: sort mid-tones, preserve black bg and bright reds ---
    float sortMask = smoothstep(threshold, threshold + maskSoft, curMetric)
                   * (1.0 - smoothstep(0.85, 1.0, curMetric));

    // --- Diagonal melt: subtle downward-diagonal streak ---
    float diagOffset = (curMetric - 0.5) * texel.y * float(windowSize) * 0.2;
    vec2 diagUV = clamp(vUV + vec2(diagOffset * 0.3, diagOffset), 0.0, 1.0);
    vec3 diagColor = texture(uScene, diagUV).rgb;
    sortedColor = mix(sortedColor, diagColor, 0.12 * strength);

    // --- Final mix ---
    vec3 result = mix(sceneColor.rgb, sortedColor, sortMask * strength);

    FragColor = vec4(clamp(result, 0.0, 1.0), sceneColor.a);
}
