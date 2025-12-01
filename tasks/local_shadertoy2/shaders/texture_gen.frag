#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform params {
  uvec2 iResolution;
  float mouseX;
  float mouseY;
} pushConstant;

float rand(vec2 co){
  return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
  vec2 iResolution = pushConstant.iResolution;
  vec2 iMouse = {pushConstant.mouseX, -pushConstant.mouseY + iResolution.y}; // flipped mouse y coord
  vec2 fragCoord = vec2(gl_FragCoord.x, iResolution.y - gl_FragCoord.y); // flipped screen y coord

  vec2 uv_coord = fragCoord/iResolution.xy;
  vec3 color = vec3(0.9, 0.1, 0.1);
    
  float x = uv_coord.x;
  float y = uv_coord.y;
  float ref = abs(y - sin(cos(rand(uv_coord) * 40.0) * 20.0) + 0.0);
    
  color = vec3(0.1) + ref * color;
  fragColor = vec4(color, 1.0); // Output to screen
}