# Arquitectura de Efectos - 3 Capas (7 Passes)

## Visión General
El pipeline de efectos está organizado en 3 capas jerárquicas claras:
1. **BASE (inferior)**: Procedural Controls + Post FX
2. **VJAY BASICS (medio)**: Efectos VJAY sobre BASE
3. **VJAY EXTRA (superior)**: Efectos extra sobre VJAY BASICS

## Distribución de Passes (7 passes reorganizados)

### CAPA 1 - BASE (Procedural Controls + Post FX)
Esta es la base de efectos que se aplica primero al video/procedural.

**Pass A - Base Sampling**
- Video/procedural mixing
- Temporal interpolation
- Grayscale
- Sharpen (base)
- Upscaling (FSR)

**Pass B - Post FX CRT + Spatial**
- CRT curvature (horizontal/vertical)
- Fish eye
- Screen bend
- UV warp (movido desde VJAY BASICS para suavizar)

**Pass C - Post FX Output**
- Bloom
- CRT scanlines/mask
- Vignette
- Film grain
- Chromatic aberration
- Color balance (Post FX)

### CAPA 2 - VJAY BASICS (sobre BASE)
Efectos VJAY que se aplican sobre el resultado de BASE.

**Pass D - Spatial VJAY (sin UV warp)**
- Ripple
- Swirl
- Displacement
- Kaleidoscope
- Tunnel (depth/curvature)

**Pass E - Temporal VJAY**
- Feedback
- Trails
- Temporal accumulation
- Feedback decay
- Recursive blend
- Frame accumulation
- Slow motion
- Temporal interpolation

**Pass F - Color VJAY**
- Brightness
- Contrast
- Saturation
- Hue shift
- Gamma
- LUT (Filmic, Neon, Noir, Heatmap, Analog CRT)
- Split tone (shadows/highlights)

### CAPA 3 - VJAY EXTRA (sobre VJAY BASICS) + Audio + Detail
Efectos extra que se aplican sobre el resultado de VJAY BASICS.

**Pass G - Combined (Detail + Audio + Extra)**
- Detail VJAY: Gaussian blur, directional blur, zoom blur, motion blur, temporal blur, unsharp mask, CAS, local contrast
- Audio VJAY: Audio-reactive warp, feedback, blur, color, glitch, beat sync, LFO rate
- Extra FX: Pixelate, strobe, threshold, slow zoom, mirror, invert, posterize, infrared, zoom pulse, RGB shift
- Analog VJAY: Analog bloom, scanline focus, mask balance, noise, VHS distortion, chromatic aberration
- Glitch VJAY: Datamosh, RGB split, scanline break, jitter, tearing, pixel sort, buffer corruption
- Blending VJAY: Blend modes

## Orden de Ejecución
1. Pass A → Pass B → Pass C (BASE completa)
2. Pass D → Pass E → Pass F (VJAY BASICS sobre BASE)
3. Pass G (VJAY EXTRA + Audio + Detail + Analog + Glitch + Blending sobre VJAY BASICS)

## Mapeo UI → Passes

### Ventana "Procedural Controls" → Pass A
- Animation speed, layers, colors, audio inputs
- Video mix, playback rate, decode oversample
- Force FPS, grayscale, sharpen, upscale
- Loop crossfade, random start

### Post FX UI → Pass B + Pass C
- CRT curvature, fish eye, bend → Pass B
- Bloom, scanlines, vignette, grain, aberration, color balance → Pass C

### Ventana "VJAY BASICS" → Pass D + Pass E + Pass F + (algunos a Pass G)
- Color grading dinámico → Pass F
- Feedback temporal → Pass E
- Distorsión espacial (ripple, swirl, displacement, kaleido, tunnel) → Pass D
- UV warp → Pass B (movido a BASE para suavizar)
- Blur & motion → Pass G
- Sharpen / detalle → Pass G
- Glitch / corruption → Pass G
- Compositing & blending → Pass G
- CRT / analog simulation → Pass G
- Audio reactivity → Pass G
- Temporal speed processing → Pass E

### Ventana "VJAY EXTRA" → Pass G
- Pixelate, strobe, threshold, slow zoom
- Mirror, invert, posterize, infrared
- Zoom pulse, RGB shift
