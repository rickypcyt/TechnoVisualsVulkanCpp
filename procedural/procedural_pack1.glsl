// @EFFECT name="Nebula" index=11 desc="Nebula cloud effect with fbm noise" author="System"
// @EFFECT name="ASCII Ocean" index=1 desc="ASCII-style ocean waves" author="System"
// @EFFECT name="Sacred Geometry" index=2 desc="Sacred geometry flower pattern" author="System"
// @EFFECT name="Glitch Grid" index=3 desc="Digital glitch grid pattern" author="System"
// @EFFECT name="Chemical Flow" index=4 desc="Flowing chemical reaction diffusion" author="System"
// @EFFECT name="Crystal Lattice" index=5 desc="Crystal lattice structure" author="System"
// @EFFECT name="Phantom Fractals" index=6 desc="Phantom fractal bands" author="System"

vec4 renderNebula(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = st;
    float warp = time * (0.15 + tempo * 0.05);
    float n = fbm(p * (1.8 + energy * 0.6) + warp);
    float glow = fbm(p * 4.5 + warp * 0.6);
    vec3 base = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend, 0.0, 1.0));
    vec3 accent = mix(uSecondaryColor, uPrimaryColor, n);
    vec3 color = mix(base, accent, 0.5 + 0.5 * n);
    color += vec3(0.15, 0.10, 0.20) * glow * (0.6 + energy * 0.6);
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(0.25 + n * 0.6 + energy * 0.35, 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderASCIIOcean(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = st;
    p.x += sin(st.y * 2.5 + time * 0.6) * 0.12;
    float surface = sin(p.x * 5.0 + time * (1.1 + tempo * 0.2));
    surface += sin((p.x + p.y) * 4.0 - time * (0.8 + mid * 0.3));
    surface += sin(p.y * 7.0 + time * 1.4) * 0.5;
    surface = surface * 0.3 + 0.5 + energy * 0.25 + bass * 0.15;
    surface = clamp(surface, 0.0, 1.0);
    float crest = smoothstep(0.35, 0.75, surface);
    vec3 deep = mix(uPrimaryColor, vec3(0.02, 0.05, 0.12), 0.7);
    vec3 foam = mix(uSecondaryColor, vec3(0.85, 0.95, 1.0), 0.5);
    vec3 color = mix(deep, foam, crest);
    color += vec3(0.04, 0.06, 0.10) * sin((st.y + time * 0.5) * 40.0) * 0.2;
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(0.35 + crest * 0.45 + energy * 0.25, 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderSacredGeometry(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    float r = length(st);
    float angle = atan(st.y, st.x);
    float petals = 6.0 + floor(mid * 6.0);
    float radial = pow(abs(cos(petals * angle)), 2.5);
    float rings = sin(r * (22.0 + high * 8.0) + time * (1.2 + tempo * 0.3)) * 0.5 + 0.5;
    float mask = radial * rings * exp(-r * (1.4 - energy * 0.3));
    mask = clamp(mask, 0.0, 1.0);
    vec3 inner = mix(uSecondaryColor, vec3(0.85, 0.55, 0.35), 0.4 + high * 0.2);
    vec3 outer = mix(uPrimaryColor, vec3(0.05, 0.03, 0.12), 0.6);
    vec3 color = mix(outer, inner, mask);
    color += vec3(0.20, 0.10, 0.30) * mask * high;
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(mask * (0.7 + energy * 0.4), 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderGlitchGrid(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 grid = (st + 0.5) * 8.0;
    vec2 cell = floor(grid);
    vec2 cellUV = fract(grid) - 0.5;
    float jitter = hash(cell + floor(time * (1.5 + tempo)));
    float mask = smoothstep(0.45 + high * 0.2, 0.0, length(cellUV + (jitter - 0.5) * 0.3));
    float pulse = sin(time * (4.0 + tempo * 1.2) + cell.x * 0.8 + cell.y * 0.6) * 0.5 + 0.5;

    // Internal colors (cyan/magenta digital colors)
    vec3 internalColor1 = vec3(0.0, 0.8, 1.0);
    vec3 internalColor2 = vec3(1.0, 0.0, 0.8);
    vec3 internalBase = mix(internalColor1, internalColor2, hash(cell));

    // Mix with user palette as tint (not as base)
    vec3 userTint = mix(uPrimaryColor, uSecondaryColor, hash(cell));
    vec3 color = internalBase * (0.5 + 0.5 * pulse);
    color = mix(color, color * userTint * 2.0, 0.3); // User colors as tint
    color = mix(color, color.bgr, high * 0.4);
    color *= mask * (0.6 + energy * 0.6);
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(mask * (0.45 + energy * 0.4), 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderChemicalFlow(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {

    // Doble rotación: capa lenta + capa rápida reactiva al audio
    vec2 p1 = rotate(st, time * (0.25 + tempo * 0.2));
    vec2 p2 = rotate(st, -time * (0.15 + bass * 0.3));

    // FBM en dos capas con escala diferente — más detalle sin coste extra
    float flow1 = fbm(p1 * (3.0 + energy));
    float flow2 = fbm(p2 * (5.5 + mid * 1.5) + vec2(time * 0.1));
    float flow  = mix(flow1, flow2, 0.4 + bass * 0.2);

    // Swirl más orgánico: frecuencias asimétricas y desfase por audio
    float swirl = sin(p1.x * 5.0 + time * 1.4 + bass * 1.2)
                * cos(p1.y * 7.0 - time * 1.1 + mid  * 0.8);

    // Venas — líneas finas que pulsan con los agudos
    float veins = abs(sin(flow * 12.0 + time * 0.3)) * high * 0.4;

    // Brillo central reactivo al bajo
    float radial  = 1.0 - smoothstep(0.0, 0.8, length(st));
    float glow    = radial * bass * 0.5;

    // Color base: make black more dominant by reducing light
    vec3 shadow = uPrimaryColor  * 0.15;  // Darker shadow
    vec3 midCol = mix(uPrimaryColor, uSecondaryColor, 0.5) * 0.6;  // Reduced mid brightness
    vec3 light  = uSecondaryColor * 0.4 + vec3(bass * 0.1, mid * 0.08, high * 0.12);  // Much darker light

    vec3 color = flow < 0.45
        ? mix(shadow, midCol, flow / 0.45)
        : mix(midCol, light,  (flow - 0.45) / 0.55);

    // Reduced additive layers for darker appearance
    color += vec3(0.04, 0.01, 0.06) * swirl * 0.15;   // tinte swirl (reduced)
    color += vec3(0.3,  0.15,  0.45)  * veins * 0.5;   // venas violeta (reduced)
    color += vec3(0.15,  0.05,  0.25)  * glow * 0.5;   // halo central (reduced)

    // Darken overall to make black dominate
    color *= 0.7;
    color = clamp(color, 0.0, 1.0);

    // Alpha: respira con el bajo y se abre con la energía
    float alpha = clamp(0.25 + flow * 0.5 + energy * 0.25 + bass * 0.15, 0.0, 1.0);

    return vec4(color, alpha);
}

vec4 renderCrystalLattice(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = rotate(st, time * 0.1);
    p *= 3.0;
    vec2 cell = fract(p) - 0.5;
    float node = exp(-12.0 * dot(cell, cell));
    float cross = exp(-30.0 * abs(cell.x)) + exp(-30.0 * abs(cell.y));
    float glow = node + 0.2 * cross;

    // Internal colors (crystal blue/purple)
    vec3 internalColor1 = vec3(0.3, 0.5, 1.0);
    vec3 internalColor2 = vec3(0.6, 0.3, 1.0);
    vec3 internalBase = mix(internalColor1, internalColor2, 0.5 + 0.5 * bass);

    // Mix with user palette as tint (not as base)
    vec3 userTint = mix(uPrimaryColor, uSecondaryColor, 0.5 + 0.5 * bass);
    vec3 color = internalBase * (0.5 + glow * (0.8 + energy * 0.5));
    color = mix(color, color * userTint * 2.0, 0.3); // User colors as tint
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(glow * (0.6 + energy * 0.4), 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderPhantomFractals(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    float r = length(st);
    float bands = sin(r * (18.0 + bass * 8.0) - time * (1.5 + tempo * 0.5));
    float fract = fbm(st * 6.0 + time * 0.4);
    float mask = smoothstep(0.0, 1.0, bands * 0.5 + 0.5) * (0.4 + fract);
    vec3 color = mix(uPrimaryColor * 0.4, uSecondaryColor, mask);
    color += vec3(0.12, 0.20, 0.30) * fract * high;
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(mask * (0.6 + energy * 0.4), 0.0, 1.0);
    return vec4(color, alpha);
}
