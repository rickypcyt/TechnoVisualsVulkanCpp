#include "post_common.glsl"

/*
  Threshold Pixel Sort — multi-segment vertical sort.
  Divides each column into N segments, scans each for bright pixel boundaries,
  and sorts all segments that contain bright pixels. Much more aggressive
  than single-segment version.
*/

float random(vec2 xy) {
    return fract(sin(dot(xy, vec2(12.9898, 78.233))) * 43758.5453);
}

float lumWeighted(vec4 color) {
    return ((color.r * 0.3) + (color.g * 0.6) + (color.b * 0.1)) * color.a;
}

// Sample 20 pixels evenly across the segment
vec4[20] sampleSegment(vec2 startXY, float size) {
    vec4 arr[20];
    for (int j = 19; j >= 0; --j) {
        vec2 xy = vec2(startXY.x, startXY.y + (size / 19.0) * float(j));
        arr[j] = texture(uScene, xy / uResolution.xy);
    }
    return arr;
}

// Bubble sort 20 pixels darkest-to-brightest
vec4[20] sortArray(vec4 arr[20]) {
    vec4 temp;
    int swapped = 1;
    for (int pass = 0; pass < 20 && swapped > 0; pass++) {
        swapped = 0;
        for (int j = 19; j > 0; --j) {
            if (lumWeighted(arr[j]) > lumWeighted(arr[j - 1])) {
                temp = arr[j];
                arr[j] = arr[j - 1];
                arr[j - 1] = temp;
                swapped = 1;
            }
        }
    }
    return arr;
}

// Find the top and bottom of a bright segment within a column region
// Returns vec2(topY, bottomY) — if no bright pixel found, returns vec2(-1, -1)
vec2 findSegmentBounds(float x, float startY, float endY, float threshold) {
    float topY = -1.0;
    float bottomY = -1.0;
    float step = max(1.0, (startY - endY) / 60.0);

    // Scan downward to find first bright pixel (segment top)
    float y = startY;
    for (int i = 0; i < 60; i++) {
        if (y <= endY) break;
        float luma = lumWeighted(texture(uScene, vec2(x, y) / uResolution.xy));
        if (luma > threshold) {
            topY = y;
            break;
        }
        y -= step;
    }

    if (topY < 0.0) return vec2(-1.0, -1.0);

    // Continue scanning to find where brightness drops below threshold (segment bottom)
    y = topY - step;
    for (int i = 0; i < 60; i++) {
        if (y <= endY) {
            bottomY = endY;
            break;
        }
        float luma = lumWeighted(texture(uScene, vec2(x, y) / uResolution.xy));
        if (luma < threshold * 0.5) {
            bottomY = y;
            break;
        }
        y -= step;
    }
    if (bottomY < 0.0) bottomY = endY;

    return vec2(topY, bottomY);
}

void main() {
    vec2 fragCoord = vUV * uResolution.xy;

    float strength = clamp(uStrength, 0.0, 1.0);
    float threshold = mix(0.2, 0.05, strength);
    threshold *= mix(0.4, 1.0, uBassLevel);

    float imgH = uResolution.y;
    float x = fragCoord.x;
    float y = fragCoord.y;

    // Divide the column into 8 overlapping scan regions
    int numSegments = 8;
    float segHeight = imgH / float(numSegments);

    // Check each region to see if this pixel falls within a sorted segment
    for (int s = 0; s < 8; s++) {
        if (s >= numSegments) break;

        float regionTop = imgH - float(s) * segHeight;
        float regionBottom = regionTop - segHeight * 1.5; // overlap 50%

        // Find bright segment within this region
        vec2 bounds = findSegmentBounds(x, regionTop, max(regionBottom, 0.0), threshold);
        float topY = bounds.x;
        float bottomY = bounds.y;

        if (topY < 0.0) continue; // no bright pixel in this region

        // Check if current pixel is within this segment
        if (y <= topY && y >= bottomY) {
            float size = topY - bottomY;

            if (size < 2.0) continue;

            vec4 colorArray[20];
            colorArray = sampleSegment(vec2(x, bottomY), size);
            colorArray = sortArray(colorArray);

            // Map current pixel to sorted array with interpolation
            float sectionSize = size / 19.0;
            float location = floor((y - bottomY) / sectionSize);
            float secBottom = bottomY + sectionSize * location;
            float locationBetween = (y - secBottom) / sectionSize;

            int locIdx = int(clamp(location, 0.0, 18.0));

            vec4 topColor = colorArray[locIdx + 1] * locationBetween;
            vec4 bottomColor = colorArray[locIdx] * (1.0 - locationBetween);

            FragColor = topColor + bottomColor;
            return;
        }
    }

    // No segment matched — pass through
    FragColor = texture(uScene, vUV);
}
