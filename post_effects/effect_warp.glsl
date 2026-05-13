#include "post_common.glsl"

void main() {
  vec2 uv = vUV;
  
  // flip y axis
  vec2 newPos = uv;
  newPos.y = 1.0 - newPos.y;
  
  // warp position
  newPos = newPos + (sin(newPos * 16.0) / 16.0) * (sin(uTime / 1000.0) / 2.0 + 0.5);
  
  // read colour from image
  vec4 col = texture(uScene, newPos);
  
  FragColor = col;
  
  // greyscale output (commented out)
  // float avg = (col.r + col.g + col.b) / 3.0;
  // FragColor = vec4(avg, avg, avg, 1.0);
}
