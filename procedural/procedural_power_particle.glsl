// @EFFECT name="Power Particle" index=44 desc="Audio-reactive geometric pattern shader" author="System"
// Power Particle - audio-reactive geometric pattern shader
#define PI 3.14159265359
#define TWO_PI 6.28318530718

float impulse( float k, float x )
{
    float h = k*x;
    return h*exp(1.0-h);
}

float plot(float dis, float blur, float lineSize){
   float pct = smoothstep(dis,dis+blur,0.5)-smoothstep(lineSize+dis,lineSize+dis+blur,0.5);     
   return   pct ;
}

vec3 wooper(vec2 st, float timeCheck, float bass, float mid, float high, float energy){
    
    vec3 color = vec3(0.0);
    vec2 pos = vec2(0.5)-abs(st);

    float r = length(pos)*2.0;
    float a = atan(pos.y,pos.x);
    
    // Audio-reactive parameters
    float grid = 4.3651814 + high * 2.0;
    float grid2 = 4.1270218 + mid * 1.5;
    float morph = 2.30923 + bass * 1.0;
    float size = 0.544726 + energy * 0.2;
    float lineSize = 0.174972;
    float blur = 0.227794;
    
    float gridSine = 5.+ (grid2*sin(timeCheck/5. * PI));
    
    r = fract(impulse(r,gridSine)*grid);
    
    float morphSine = 0.2 + ( 1.+sin(timeCheck/3. * PI) /2.)*morph;
    float morphSine2 = 0.2 + ( 1.+sin(timeCheck/5. * PI) /2.)*morph;
    float morphSine3 = 0.2 + ( 1.+sin(timeCheck/9. * PI) /2.)*morph;
    
    float f = ( size*cos(a*6. + timeCheck/3.) + size*cos(a*2. + timeCheck/2.))/2.;
    float p = plot(1.-smoothstep(f,f+0.9,r*morphSine), blur, lineSize);
    float f2 = ( size*cos(a*4. + timeCheck/3.) + size*cos(a*3. + timeCheck*7.))/2.;
    float p2 = plot(1.-smoothstep(f2,f2+0.9,r*morphSine2), blur, lineSize);
    float f3 = ( size*cos(a*7. + timeCheck/30.) + size*cos(a*3. + timeCheck*7.))/2.;
    float p3 = plot(1.-smoothstep(f3,f3+0.9,r*morphSine3), blur, lineSize);
    
    color.r = p;
    color.g = p2 *st.x;
    color.b = p3;
 
   return(color);
}

vec3 powerParticle(vec2 st, float time, float bass, float mid, float high, float energy){
  
    st.y += ((st.x*0.05)*sin(time/10.*PI)+(st.x*0.1)*sin(time/12.*PI))/2.;
    st.x += ((st.y*0.05)*sin(time/10.*PI) + (st.y*0.1)*sin(time/12.*PI))/2.;
    
    vec2 pos = vec2(0.25+0.25*sin(time))-abs(st);

    float r = length(pos);
    float d = distance(st,vec2(0.5))* (sin(time/8.));
    d = distance(vec2(.5),st);
   vec3 colorNew = vec3(0);
   
   float delayAmount = 0.044175148;
   float speed = 0.466905 + energy * 0.3;
   float delay = delayAmount;
   float timerChecker = time * speed ;
    for(int i=0;i<10;i++) {
      vec3 colorCheck = wooper(st, timerChecker+ float(i)*delay, bass, mid, high, energy)* (1.-(float(i)/10.0));
      colorNew+= colorCheck ;
    }
    
    return(colorNew);
}

vec3 rgb2hsb( in vec3 c ){
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz),
                 vec4(c.gb, K.xy),
                 step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r),
                 vec4(c.r, p.yzx),
                 step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)),
                d / (q.x + e),
                q.x);
}


vec4 renderPowerParticle(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Apply raymarched object style centering (neutral coordinates)
    vec2 res = vec2(0);
    res.x = uResolution.x*0.5625;
    res.y = uResolution.y;
    vec2 coord = st * uResolution.xy / res;
    coord.x -= 0.35;
    
    vec3 powerColor = powerParticle(coord, time, bass, mid, high, energy);
    vec3 hue = rgb2hsb(powerColor);
    hue.x = mod(time/10.,1.);
    hue.y = 0.5;
    float d = 1.-distance(vec2(.5),coord)*2.;
   	
    vec3 finalColor = (hsb2rgb(hue)*d )+(powerColor*d*0.5);
    float alpha = clamp(0.7 + length(finalColor) * 0.3 + energy * 0.2, 0.0, 1.0);

    return vec4(finalColor, alpha);
}
