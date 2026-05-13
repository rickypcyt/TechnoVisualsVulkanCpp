// @EFFECT name="Tiles and Numbers" index=74 desc="Animated tiles with numbers, fractals, truchet and droste patterns" author="Shane/ikr7/FabriceNeyret2"

#define RotTiles(a) mat2(cos(a),-sin(a),sin(a),cos(a))
#define BTiles(p,s) max(abs(p).x-s.x,abs(p).y-s.y)
#define deg45Tiles .707
#define R45Tiles(p) (( p + vec2(p.y,-p.x) ) *deg45Tiles)
#define TriTiles(p,s) max(R45Tiles(p).x,max(R45Tiles(p).y,BTiles(p,s)))
#define LINE_SIZE_TILES 0.05

vec2 _uvTiles;

float randomTiles(vec2 p) {
    return fract(sin(dot(p.xy, vec2(12.9898,78.233)))* 43758.5453123);
}

float lineToTiles(vec2 p, vec2 a, vec2 b){
    return distance(p,mix(a,b,clamp(dot(p-a,b-a)/dot(b-a,b-a),0.0,1.0)));
}

float c0Tiles(vec2 p){
    vec2 prevP = p;
    p.x = abs(p.x);
    float d = lineToTiles(p,vec2(0.3,0.4),vec2(0.3,-0.4));
    p = prevP;
    p.y = abs(p.y);
    float d2 = lineToTiles(p,vec2(-0.3,0.4),vec2(0.3,0.4));
    d = min(d,d2);
    return d - LINE_SIZE_TILES;
}

float c1Tiles(vec2 p){
    float d = lineToTiles(p,vec2(-0.15,0.4),vec2(0.0,0.4));
    float d2 = lineToTiles(p,vec2(0.0,0.4),vec2(0.0,-0.4));
    d = min(d,d2);
    return d - LINE_SIZE_TILES;
}

float c2Tiles(vec2 p){
    float d = lineToTiles(p,vec2(0.3,0.4),vec2(0.3,0.0));
    float d2 = lineToTiles(p,vec2(0.3,0.0),vec2(0.1,0.0));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(-0.3,0.0),vec2(-0.1,0.0));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(-0.3,0.0),vec2(-0.3,-0.4));
    d = min(d,d2);
    p.y = abs(p.y);
    d2 = lineToTiles(p,vec2(-0.3,0.4),vec2(0.3,0.4));
    d = min(d,d2);
    return d - LINE_SIZE_TILES;
}

float c3Tiles(vec2 p){
    float d = lineToTiles(p,vec2(0.3,0.4),vec2(0.3,-0.4));
    float d2 = lineToTiles(p,vec2(0.3,0.0),vec2(0.0,0.0));
    d = min(d,d2);
    p.y = abs(p.y);
    d2 = lineToTiles(p,vec2(-0.3,0.4),vec2(0.3,0.4));
    d = min(d,d2);
    return d - LINE_SIZE_TILES;
}

float c4Tiles(vec2 p){
    float d = lineToTiles(p,vec2(0.0,0.4),vec2(-0.3,-0.25));
    float d2 = lineToTiles(p,vec2(-0.3,-0.25),vec2(0.3,-0.25));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(0.2,-0.1),vec2(0.2,-0.4));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(0.2,-0.1),vec2(0.0,-0.1));
    d = min(d,d2);
    return d - LINE_SIZE_TILES;
}

float c5Tiles(vec2 p){
    p.x *= -1.0;
    return c2Tiles(p);
}

float c6Tiles(vec2 p){
    float d = lineToTiles(p,vec2(-0.3,0.4),vec2(0.2,0.4));
    float d2 = lineToTiles(p,vec2(-0.3,0.4),vec2(-0.3,-0.4));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(-0.3,-0.4),vec2(-0.2,-0.4));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(0.0,-0.4),vec2(0.3,-0.4));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(0.3,-0.4),vec2(0.3,0.0));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(0.3,0.0),vec2(-0.2,0.0));
    d = min(d,d2);
    return d - LINE_SIZE_TILES;
}

float c7Tiles(vec2 p){
    float d = lineToTiles(p,vec2(-0.3,0.4),vec2(0.3,0.4));
    float d2 = lineToTiles(p,vec2(0.3,0.4),vec2(-0.3,-0.4));
    d = min(d,d2);
    return d - LINE_SIZE_TILES;
}

float c8Tiles(vec2 p){
    float d = lineToTiles(p,vec2(-0.3,0.4),vec2(-0.3,0.0));
    float d2 = lineToTiles(p,vec2(-0.3,0.0),vec2(0.2,0.0));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(0.3,0.3),vec2(0.3,-0.4));
    d = min(d,d2);
    d2 = lineToTiles(p,vec2(-0.3,-0.1),vec2(-0.3,-0.4));
    d = min(d,d2);
    p.y = abs(p.y);
    d2 = lineToTiles(p,vec2(-0.3,0.4),vec2(0.3,0.4));
    d = min(d,d2);
    return d - LINE_SIZE_TILES;
}

float c9Tiles(vec2 p){
    p *= -1.0;
    return c6Tiles(p);
}

float drawNumberTiles(vec2 p, int d){
    return d==0 ? c0Tiles(p)
         : d==1 ? c1Tiles(p)
         : d==2 ? c2Tiles(p)
         : d==3 ? c3Tiles(p)
         : d==4 ? c4Tiles(p)
         : d==5 ? c5Tiles(p)
         : d==6 ? c6Tiles(p)
         : d==7 ? c7Tiles(p)
         : d==8 ? c8Tiles(p)
         : d==9 ? c9Tiles(p)
         : 10.0;
}

float arrowTiles(vec2 p){
    p.y -= 0.05;
    float d = TriTiles(p,vec2(0.1));
    p.y += 0.05;
    float d2 = TriTiles(p,vec2(0.1));
    d = max(-d2,d);
    d2 = length(p-vec2(0.0,-0.03))-0.015;
    d = min(d,d2);
    return d;
}

float arrowsTiles(vec2 p, float n, float time){
    p.y -= time*clamp((0.1+n),0.5,1.0)*0.3;
    p.y = mod(p.y,0.2)-0.1;
    p.y += 0.01;
    float d = arrowTiles(p);
    return d;
}

float fractalTiles(vec2 p, float n, float time){
    vec2 prevP = p;
    float d = 10.0;
    for(float i = 1.0; i<4.0; i+=1.0){
        p *= RotTiles(radians(n+i*30.0*time));
        p = abs(p)-i*0.1;
        p *= RotTiles(radians(n+i*30.0));
        float d2 = arrowsTiles(p,n,time);
        d = min(d,d2);
    }
    p = prevP;
    d = max(BTiles(p,vec2(0.45)),d);
    return d;
}

float truchetTiles(vec2 p, float n, float time){
    vec2 prevP = p;
    p -= time*0.1+n;
    p *= 5.0;
    vec2 id = floor(p);
    vec2 gr = fract(p)-0.5;
    float n2 = randomTiles(id);
    float r = 45.0;
    if(n2>=0.25 && n2 < 0.5){
        r = -45.0;
    } else if(n2>=0.5 && n2 < 0.75){
        r = -135.0;
    } else if(n2>=0.75){
        r = 135.0;
    }
    float a = radians(r);
    float d = dot(gr,vec2(cos(a),sin(a)));
    p = prevP;
    d = max(BTiles(p,vec2(0.45)), d*0.1);
    return d;
}

vec2 clogTiles(vec2 z) {
    return vec2(log(length(z)), atan(z.y, z.x));
}

vec2 drosteUVTiles(vec2 p, float n, float time){
    float speed = 0.25+n;
    float animate = mod(time*speed,2.07);
    float rate = sin(time*0.5);
    p = clogTiles(p)*mat2(1.0,0.11,rate*0.5,1.0);
    p = exp(p.x-animate) * vec2(cos(p.y), sin(p.y));
    vec2 c = abs(p);
    vec2 duv = 0.5+p*exp2(ceil(-log2(max(c.y,c.x))-2.0));
    return duv;
}

vec2 pmodTiles(vec2 p, float s, float space){
    float modVal = s*(2.0+space);
    p = mod(p,modVal)-(modVal*0.5);
    return p;
}

float drosteCirclesTiles(vec2 p, float s, float space, float n, float time){
    vec2 prevP = p;
    p = drosteUVTiles(p,n,time);
    p = pmodTiles(p,s,space);
    p *= RotTiles(radians(time*30.0));
    float d = abs(length(p)-s)-0.02;
    d = max(-(abs(p.x)-0.02),d);
    p = prevP;
    d = max(BTiles(p,vec2(0.45)),d);
    return d;
}

vec2 mobiusLogUVTiles(vec2 uv, float time) {
    vec2 z = uv - vec2(-1.0, 0.0);
    uv.x -= 0.5;
    uv *= mat2(z, -z.y, z.x) / dot(uv, uv);
    _uvTiles = log(length(uv + 0.5)) * vec2(0.5, -0.5);
    uv = log(length(uv += 0.5)) * vec2(0.5, -0.5) + atan(uv.y, uv.x) / 6.2831853 * vec2(3.0, 1.0);
    return uv;
}

float tilesTiles(vec2 p, float time){
    p *= 4.0;
    vec2 id = floor(p);
    vec2 gr = fract(p)-0.5;
    float n = randomTiles(id);
    float n2 = randomTiles(id)*10.0;
    float d = 10.0;
    if(n<0.25){
        d = fractalTiles(gr,n,time);
    } else if(n>0.25 && n<0.5){
        d = truchetTiles(gr,n,time);
    } else if(n>0.5 && n<0.8){
        d = drawNumberTiles(gr,int(mod(time+n2,10.0)));
    } else if(n>0.8){
        d = drosteCirclesTiles(gr,0.1,0.5,n*0.3,time);
    }
    return d;
}

float renderTiles(vec2 p, float time){
    p.y += time*0.2;
    vec2 gr = fract(p)-0.5;
    float d = tilesTiles(gr,time);
    return d;
}

vec4 renderTilesNumbers(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = (st - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
    vec2 p = uv;
    uv = mobiusLogUVTiles(uv, time);
    float d = renderTiles(uv, time);
    float w = length(fwidth(_uvTiles)) * 1.5;
    float aa = smoothstep(-w, w, d);
    // Use user colors from preset
    vec3 col = uPrimaryColor; // Primary color background
    col = mix(col, uSecondaryColor, aa); // Secondary color lines
    return vec4(col, 1.0);
}
