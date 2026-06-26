// Funciones comunes para efectos de espejo
// Incluir este archivo en los shaders de mirror específicos

// Note: uStrength, uTime, uScene are already declared in post_common.glsl

// Espejo horizontal: mitad derecha refleja izquierda
vec2 horizontalMirror(vec2 uv) {
    if (uv.x > 0.5) {
        uv.x = 1.0 - uv.x;
    }
    return uv;
}

// Espejo vertical: mitad inferior refleja superior
vec2 verticalMirror(vec2 uv) {
    if (uv.y > 0.5) {
        uv.y = 1.0 - uv.y;
    }
    return uv;
}

// Kaleidoscopio circular
vec2 kaleidoMirror(vec2 uv, float time) {
    vec2 centered = uv - 0.5;
    float angle = atan(centered.y, centered.x);
    float radius = length(centered);
    
    // More segments + softer folding gives a cleaner result with fewer obvious cuts.
    float segments = 6.0;
    float sector = 3.14159 / segments;
    angle = mod(angle + sector, 2.0 * sector) - sector;
    angle = abs(angle);
    
    vec2 mirrored = vec2(cos(angle), sin(angle)) * radius + 0.5;
    mirrored += sin(time + radius * 8.0) * 0.004;
    
    return mirrored;
}

// Rorschach psicodélico
vec2 rorschachMirror(vec2 uv, float time) {
    vec2 centered = (uv - 0.5) * 2.0;
    
    vec2 symH = vec2(abs(centered.x), centered.y);
    vec2 symV = vec2(centered.x, abs(centered.y));
    vec2 symBoth = vec2(abs(centered.x), abs(centered.y));
    
    float cycle = fract(time * 0.2);
    vec2 finalUV;
    if (cycle < 0.33) {
        finalUV = symH;
    } else if (cycle < 0.66) {
        finalUV = mix(symH, symV, smoothstep(0.33, 0.66, cycle));
    } else {
        finalUV = mix(symV, symBoth, smoothstep(0.66, 1.0, cycle));
    }
    
    finalUV = finalUV * (1.0 + sin(time) * 0.1) * 0.5 + 0.5;
    return finalUV;
}

// Función genérica para aplicar mirror según modo
vec4 applyMirrorEffect(vec2 uv, int mode, float strength, float time) {
    vec2 mirroredUV = uv;
    
    if (mode == 0) {
        mirroredUV = horizontalMirror(uv);
    } else if (mode == 1) {
        mirroredUV = verticalMirror(uv);
    } else if (mode == 2) {
        mirroredUV = kaleidoMirror(uv, time);
    } else if (mode == 3) {
        mirroredUV = rorschachMirror(uv, time);
    }
    
    vec4 mirrored = texture(uScene, mirroredUV);
    vec4 original = texture(uScene, uv);
    
    vec4 result = mix(original, mirrored, strength);

    // Keep the mirror clean: avoid drawing separator highlights that make the image look low-res.
    // A tiny soft blend around the fold helps hide harsh edges instead of emphasizing them.
    if (mode <= 1 || mode >= 2) {
        float foldDist = min(abs(uv.x - 0.5), abs(uv.y - 0.5));
        float foldSoft = smoothstep(0.06, 0.0, foldDist);
        result.rgb = mix(result.rgb, original.rgb, foldSoft * 0.08);
    }
    
    return result;
}
