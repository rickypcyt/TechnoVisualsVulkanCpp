void main() {
    vec2 st = (vUV - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
    
    // Apply global camera zoom and offset
    st *= uCameraZoom;
    st += vec2(uCameraOffsetX, uCameraOffsetY);

    vec4 color;
    if (uMode == 0) {
        FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    } else if (uMode == 1) {
        color = renderASCIIOcean(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 2) {
        color = renderSacredGeometry(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 3) {
        color = renderGlitchGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 4) {
        color = renderChemicalFlow(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 5) {
        color = renderCrystalLattice(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 6) {
        color = renderPhantomFractals(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 7) {
        color = renderFractalObject(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 8) {
        color = renderPulsarTunnel(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 9) {
        color = renderAuroraBloom(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 10) {
        color = renderRibbonScanlines(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 11) {
        color = renderNebula(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 12) {
        color = renderKaleidoscopeFractal(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 13) {
        color = renderVoronoiCells(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 14) {
        color = renderRaymarchedObject(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 15) {
        color = renderReactionDiffusionPattern(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 16) {
        color = renderLiquidRefraction(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 17) {
        color = renderStarfieldWarp(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 18) {
        color = renderPlasmaClassic(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 19) {
        color = renderDomainWarpedFractal(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 20) {
        color = renderFractalTunnel(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 21) {
        color = renderVolumetricStarfield(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 22) {
        color = renderVoxelPathTracer(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 23) {
        color = renderEtiennePulse(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 24) {
        color = renderFractalRunway(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 25) {
        color = renderVolumetricTunnel(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 26) {
        color = renderChromaticSwirl(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 27) {
        color = renderHyperPulse(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 28) {
        color = renderGyroidReflections(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 29) {
        color = renderHead(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 30) {
        color = renderMetalGyroidHall(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 31) {
        color = renderHexKaleidoscope(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 32) {
        color = renderHSVColorShift(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 33) {
        color = renderCryptRoots(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 34) {
        color = renderBreathing(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 35) {
        color = renderEvolutionNoise(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 36) {
        color = renderPhiFields(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 37) {
        color = renderFractalInfinity(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 38) {
        color = renderWalker(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 39) {
        color = renderWeirdCreature(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 40) {
        color = renderAnaglyphAssembly(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 41) {
        color = renderMessageTunnel(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 42) {
        color = renderPouetGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 43) {
        color = renderCylinderRepeat(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 44) {
        color = renderPowerParticle(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 45) {
        color = renderFlopine(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 46) {
        color = renderEiyeronDeform(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 47) {
        color = renderFractalRotation(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 48) {
        color = renderKaleidoscopicFlow(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 49) {
        color = renderReactiveTwistField(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 50) {
        color = renderCollapsedTransit(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 51) {
        color = renderCelestialRibbonBloom(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 52) {
        color = renderMandelbulbFlux(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 53) {
        color = renderIridescentEye(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 54) {
        color = renderVoronoiGateStream(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 55) {
        color = renderRecursiveCubeBloom(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 56) {
        color = renderHelloWorldGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 57) {
        color = renderDailyFlowLines(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 58) {
        color = renderLitnGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 59) {
        color = renderHannahAdamsGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 60) {
        color = renderAnotherCodeGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 61) {
        color = renderLuperfutGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 62) {
        color = renderGlassRefractionField(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 63) {
        color = renderMatrixDigitalRain(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 64) {
        color = renderIkedaDigits(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 65) {
        color = renderIkedaGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 66) {
        color = renderIChingHexagrams(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 67) {
        color = renderReflectedTurbulence(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 68) {
        color = renderIkedaDataStream(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 69) {
        color = renderLoopNoiseSDF(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 70) {
        color = renderLoopNoiseRays(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 71) {
        color = renderCellRings(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 72) {
        color = renderCoronaVirus(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 73) {
        color = renderSphereRaytrace(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 74) {
        color = renderTilesNumbers(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 75) {
        color = renderAudioEQ(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 76) {
        color = renderNoiseDotGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 77) {
        color = renderRecursiveGrids(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 78) {
        color = renderGameOfLife(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 79) {
        color = render3DWaveBoxes(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 80) {
        color = renderCircularCellularAutomata(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 81) {
        color = renderRecursiveSubdivision(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 82) {
        color = renderLowresPixelation(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 83) {
        color = renderParticleCloud(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 84) {
        color = renderFibonacciCurl(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 85) {
        color = renderCollatzSpiral(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 86) {
        color = renderQuadtreeBoxes(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 87) {
        color = renderStarRing(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 88) {
        color = renderRotatingCircle(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 89) {
        color = renderCurvedLines(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 90) {
        color = renderTriangleParticles(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 91) {
        color = renderBezierPetals(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 92) {
        color = renderKochSnowflake(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 93) {
        color = renderFlowField(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 94) {
        color = renderIsometricCubes(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 95) {
        color = renderWaveCircles(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 96) {
        color = renderNoiseParticles(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 97) {
        color = renderLavaFluidFloor(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 98) {
        color = renderCristianCamiloGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 99) {
        color = renderJuanmiPaganGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 100) {
        color = renderTizianoSterpaGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    } else if (uMode == 101) {
        color = renderBohmGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh);
    }
    
    // Invalid modes will show black/pink error color

    FragColor = color;
}
