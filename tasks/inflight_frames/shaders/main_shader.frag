#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "sdf_objects.glsl"
#include "UniformParams.h"

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D generatedTexture; //generated

layout(binding = 1) uniform sampler2D swordTexture; //loaded

layout(binding = 2) uniform samplerCube cubemapTexture; //cubemap

layout(binding = 3) uniform params {
  UniformParams uniformParams;
};

// --------- Constants ---------

const int kMaxSteps = 256;
const int kMaxShadowSteps = 32;
const float kPresicion = 0.001;
const float kMinDist = 0.0;
const float kMaxDist = 100.0;

// --------- Structs ---------

struct Light {
  vec3 pos;
  float intensity;
};

// --------- Camera ---------

mat3 camera(in vec3 camera_pos, in vec3 look_at_point) {
  vec3 camera_dir = normalize(look_at_point - camera_pos);
  vec3 camera_right = normalize(cross(vec3(0, 1, 0), camera_dir));
  vec3 camera_up = normalize(cross(camera_dir, camera_right));
    
  return mat3(-camera_right, camera_up, -camera_dir);
}

// --------- Render ---------

// --- Full Scene ---

Surface sdScene(in vec3 pos) {
  Surface outer_shell = Surface(1, sdPlane(pos, vec3(0.0, 1.0, 0.0), 3.0), mPlane(pos));
  
  Surface blade = sdBlade(pos);
  Surface guard_handle = sdGuardHandle(pos);
  Surface res = opUnion(guard_handle, blade);
  res = opUnion(res, outer_shell);

  return res;
}

// --- Tracing ---

Surface trace(in vec3 ray_origin, in vec3 ray_dir) {
  vec3 pos = ray_origin;
  float total_depth = kMinDist;
  Surface closest_object;

  for (int steps = 0; steps < kMaxSteps; steps++) {
    closest_object = sdScene(pos);
    if (closest_object.sd < kPresicion) {
        break;
    }

    total_depth += closest_object.sd;

    if (total_depth > kMaxDist) {
      break;
    }

    pos += closest_object.sd * ray_dir;
  }
    
  closest_object.sd = total_depth;

  return closest_object;
}

vec3 generateNormal(in vec3 pos, in float offset) {
  float dx1 = sdScene(pos + vec3(offset, 0, 0)).sd;
  float dx2 = sdScene(pos - vec3(offset, 0, 0)).sd;
  float dy1 = sdScene(pos + vec3(0, offset, 0)).sd;
  float dy2 = sdScene(pos - vec3(0, offset, 0)).sd;
  float dz1 = sdScene(pos + vec3(0, 0, offset)).sd;
  float dz2 = sdScene(pos - vec3(0, 0, offset)).sd;

  return normalize(vec3(dx1 - dx2, dy1 - dy2, dz1 - dz2));
}

// --- Texturing ---

vec3 transformTexture(in vec3 pos, in sampler2D channel, in vec3 normal, float coeff, vec3 mult) {
  vec3 weight = abs(normal);
  vec3 color = 
      pow(weight.x, coeff) * texture(channel, pos.yz * mult.x).rgb + 
      pow(weight.y, coeff) * texture(channel, pos.xz * mult.y).rgb +
      pow(weight.z, coeff) * texture(channel, pos.xy * mult.z).rgb;
  return color;
}

vec3 applyTextures(in vec3 pos, in vec3 normal, in Material material) {
  if (material.texture_id == 0) { // blade
    return normalize(transformTexture(pos, swordTexture, normal, 5.0, vec3(1))) * normalize(transformTexture(pos, generatedTexture, normal, 10.0, vec3(0.05)));
  }
  if (material.texture_id == 1) { // handle 
    //return 2.0 * transformTexture(pos, iChannel3, normal, 1.0, vec3(1));
  }
  return vec3(1);
}

// --- Lighting ---

float softShadow(in vec3 ray_origin, in vec3 ray_direction, in float mint, in float weight) { //unused rn
  float result = 1.0;
  float t = mint;

  for(int i = 0; i < kMaxShadowSteps; i++) {
    float h = sdScene(ray_origin + ray_direction * t).sd;
    if(h < kPresicion) {
      break;
    }
        
    result = min(result, h / (t * weight));
    t += clamp(h, 0.01, 0.5);
    if(t > kMaxDist) {
      break;
    }
  }
  return clamp(result, 0.0, 1.0);
}

vec3 lightCalc(vec3 pos, vec3 normal, vec3 ray_dir, vec3 ray_origin, Light light, Material material) {
  vec3 cubemap_reflection = texture(cubemapTexture, reflect(ray_dir, normal)).rgb;

  vec3 light_dir = normalize(light.pos - pos);
  vec3 ambient = material.ambient_color * normalize(applyTextures(pos, normal, material));
  if (material.reflection > 0.001) {
    ambient *= material.reflection * normalize(cubemap_reflection);
  }
  vec3 new_ray_origin = pos + normal * kPresicion * 2.0;
  float shadow_ray_length = trace(new_ray_origin, light_dir).sd; // hard shadows

  float normal_lighting = clamp(dot(light_dir, normal), 0., 1.);

  vec3 diffuse = material.diffuse_color * normal_lighting;
  if (shadow_ray_length < length(light.pos - new_ray_origin)) {
    diffuse *= 0.2;
   }

  float specular_lighting = clamp(dot(-reflect(light_dir, normal), -ray_dir), 0.0, 1.0);
  vec3 specular = material.specular_color * pow(specular_lighting, material.shininess);
  if (shadow_ray_length < length(light.pos - new_ray_origin)) {
    specular *= 0.5;
  }

  return (ambient + diffuse + specular) * light.intensity;
}

// --- Render call ---

vec3 render(in vec3 ray_origin, in vec3 ray_dir) {
  vec3 color = texture(cubemapTexture, ray_dir).rgb;
    
  // Set up lights
  Light light = Light(vec3(2, 0.1, 0), 0.9);
    
  Surface closest_object = trace(ray_origin, ray_dir);

  if(closest_object.sd < kMaxDist) {
    vec3 pos = ray_origin + ray_dir * closest_object.sd;
    vec3 normal = generateNormal(pos, kPresicion);
      
    color = lightCalc(pos, normal, ray_dir, ray_origin, light, closest_object.material);
       
  }
  return color;
}

vec3 postfx(vec3 color) {
  //color = pow(color, vec3(1.0/2.2)); //gamma correction
  return color;
}

// --------- Main ---------

void main()
{
  vec2 iResolution = uniformParams.iResolution;
  vec2 iMouse = {uniformParams.mouseX, -uniformParams.mouseY + iResolution.y}; // flipped mouse y coord
  vec2 fragCoord = vec2(gl_FragCoord.x, iResolution.y - gl_FragCoord.y); // flipped screen y coord

  vec3 camera_origin = vec3(0.0, 4.0, 0.0);
  float camera_radius = 2.0;
  vec3 look_at_point = vec3(0.0, 0.0, 3.0);

  vec2 uv_coord = (fragCoord - 0.5 * iResolution.xy) / max(iResolution.x, iResolution.y);
  vec2 mouse = iMouse.xy / iResolution.xy;

  camera_origin.yz = camera_origin.yz * camera_radius * rotate2d(mix(kPi / 2.0, 0.0, mouse.y));
  camera_origin.xz = camera_origin.xz * rotate2d(mix(-kPi, kPi, mouse.x)) + vec2(look_at_point.x, look_at_point.z);
    
  vec3 ray_dir = camera(camera_origin, look_at_point) *  normalize(vec3(uv_coord, -1));

  vec3 color = render(camera_origin, ray_dir);
  color = postfx(color);
  fragColor = vec4(color, 1.0);
}
