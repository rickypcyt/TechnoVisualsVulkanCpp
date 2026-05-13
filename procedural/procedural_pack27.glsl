// @EFFECT name="Mandelbulb Flux Bloom" index=52 desc="Mandelbulb raymarch matching tigrou's original look" author="tigrou"

const int kMandelbulbMaxIter = 15;
const float kMandelbulbBailout = 6.0;
const float kMandelbulbPower = 20.0;

float mandelbulbDistance(vec3 p) {
    vec3 z = p;
    vec3 c = z;
    float r = 0.0;
    float dr = 1.2;
    for (int i = 0; i <= kMandelbulbMaxIter; ++i) {
        r = length(z);
        if (r > kMandelbulbBailout) {
            break;
        }

        float theta = acos(clamp(z.z / max(r, 1e-5), -1.0, 1.0));
        float phi = atan(z.y, z.x);
        dr = pow(r, kMandelbulbPower - 1.0) * kMandelbulbPower * dr + 1.0;

        float zr = pow(r, kMandelbulbPower);
        theta *= kMandelbulbPower;
        phi *= kMandelbulbPower;

        vec3 trig = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
        z = trig * zr + c;
    }
    return 0.3 * log(r) * r / max(dr, 1e-5);
}

vec4 renderMandelbulbFlux(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 fragCoord = (st * 0.5 + 0.5) * uResolution;
    vec2 pos = (fragCoord * 2.0 - uResolution) / uResolution.y;

    vec3 camPos = vec3(cos(time * 0.3), sin(time * 0.3), 1.5);
    vec3 camTarget = vec3(0.0);
    vec3 camDir = normalize(camTarget - camPos);
    vec3 camUp = normalize(vec3(0.0, 1.0, 0.0));
    vec3 camSide = normalize(cross(camDir, camUp));
    camUp = normalize(cross(camSide, camDir));
    float focus = 1.8;

    vec3 rayDir = normalize(camSide * pos.x + camUp * pos.y + camDir * focus);
    vec3 ray = camPos;
    float accumSteps = 0.0;
    float d = 0.0;
    float totalDist = 0.0;
    const int MAX_MARCH = 50;
    const float MAX_DISTANCE = 1000.0;

    for (int i = 0; i < MAX_MARCH; ++i) {
        d = mandelbulbDistance(ray);
        totalDist += d;
        ray += rayDir * d;
        accumSteps += 1.4;
        if (d < 0.001) {
            break;
        }
        if (totalDist > MAX_DISTANCE) {
            totalDist = MAX_DISTANCE;
            break;
        }
    }

    float c = totalDist * 0.1;
    vec3 baseColor = 1.0 - vec3(c * 0.003, c * 0.5, c * 0.2)
                     - vec3(0.025, 0.019, 0.02) * accumSteps * 0.8;
    baseColor = clamp(baseColor, 0.0, 1.0);

    // Blend gently with the project palette so UI colors still affect the effect.
    vec3 palette = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend, 0.0, 1.0));
    vec3 color = mix(baseColor, baseColor * palette, 0.35 + high * 0.1);

    return vec4(color, 1.0);
}
