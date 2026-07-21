// @EFFECT name="Pixel Sort Luminance" index=109 desc="Vertical pixel sorting by luminance threshold" author="System"

float pixelRandom(vec2 xy) {
    return fract(sin(dot(xy.xy, vec2(12.0, 78.0))));
}

float luminance(vec4 color) {
    return (color.r * 0.3 + color.g * 0.6 + color.b * 0.1) * color.a;
}

// Procedural texture sampling
vec4 proceduralTexture(vec2 uv, float time) {
    float r = abs(sin(uv.x * 8.0 + time * 5.0));
    float g = abs(sin(uv.y * 6.0 + time * 4.0));
    float b = abs(sin((uv.x + uv.y) * 10.0 + time * 3.0));
    float a = 0.5 + 0.5 * sin(uv.x * 12.0 + uv.y * 8.0 + time * 2.0);
    return vec4(r, g, b, a);
}

// Returns the y coordinate of the first pixel that is brighter than the threshold
float getFirstThresholdPixel(vec2 xy, float threshold, float time) {
    float luma = luminance(proceduralTexture(xy / uResolution.xy, time));

    float increment = uResolution.y / (30.0 + (pixelRandom(xy.xx) * 6.0));

    while (luma <= threshold) {
        xy.y -= increment;
        if (xy.y <= 0.0) return 0.0;
        luma = luminance(proceduralTexture(xy / uResolution.xy, time));
    }

    return xy.y;
}

// Puts 10 pixels in an array
void putItIn(vec2 startxy, float size, inout vec4 colorarray[10], float time) {
    vec2 xy;
    for (int j = 9; j >= 0; --j) {
        xy = vec2(startxy.x, startxy.y + (size / 9.0) * float(j));
        colorarray[j] = proceduralTexture(xy / uResolution.xy, time);
    }
}

// Bubble sort for 10 pixels, sorting them from darkest to brightest
void sortArray(inout vec4 colorarray[10]) {
    vec4 tempcolor;
    int swapped = 1;

    while (swapped > 0) {
        swapped = 0;
        for (int j = 9; j > 0; --j) {
            if (luminance(colorarray[j]) > luminance(colorarray[j - 1])) {
                tempcolor = colorarray[j];
                colorarray[j] = colorarray[j - 1];
                colorarray[j - 1] = tempcolor;
                ++swapped;
            }
        }
    }
}

vec4 renderPixelSortLuminance(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 fragCoord = st * uResolution.xy;

    float firsty = getFirstThresholdPixel(vec2(fragCoord.x, uResolution.y), 0.0, time);
    float secondy = getFirstThresholdPixel(vec2(fragCoord.x, firsty - 1.0), 0.5, time);

    // Only work on the pixels that are between the two threshold pixels
    if (fragCoord.y < firsty && fragCoord.y > secondy) {
        float size = firsty - secondy;

        vec4 colorarray[10];
        putItIn(vec2(fragCoord.x, secondy), size, colorarray, time);
        sortArray(colorarray);

        float sectionSize = size / 9.0;
        float location = floor((fragCoord.y - secondy) / sectionSize);
        float bottom = secondy + (sectionSize * location);
        float locationBetween = (fragCoord.y - bottom) / sectionSize;

        // Fading between the colors of our ten sampled pixels
        int locIdx = int(location);
        if (locIdx < 0) locIdx = 0;
        if (locIdx > 8) locIdx = 8;

        vec4 topColor = colorarray[locIdx + 1] * locationBetween;
        vec4 bottomColor = colorarray[locIdx] * (1.0 - locationBetween);

        return topColor + bottomColor;
    } else {
        return proceduralTexture(st, time);
    }
}
