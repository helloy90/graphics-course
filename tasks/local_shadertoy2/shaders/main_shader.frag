#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform params {
  uvec2 iResolution;
  float mouseX;
  float mouseY;
} pushConstant;

layout(binding = 0) uniform sampler2D iChannel1; //generated

layout(binding = 1) uniform sampler2D iChannel2; //loaded

// --------- Constants ---------

const float kPi = 3.14159265359;

const int kMaxSteps = 256;
const int kMaxShadowSteps = 32;
const float kPresicion = 0.001;
const float kMinDist = 0.0;
const float kMaxDist = 100.0;

const mat3 kIdentityMat = mat3(
    vec3(1, 0, 0),
    vec3(0, 1, 0),
    vec3(0, 0, 1)
);

// --------- Structs ---------

struct Material {
  vec3 ambient_color; // ambient reflection * ambient lighting
  vec3 diffuse_color; // diffuse reflection * light intensity
  vec3 specular_color; // specular reflection * specular light
  float reflection;
  float shininess;
  int texture_id; // if -1 then no texture
};

struct Surface {
  int id;
  float sd;
  Material material;
};

struct Light {
  vec3 pos;
  float intensity;
};

// --------- Materials ---------

Material mGuard() {
  vec3 aCol = 0.4 * vec3(0.8);
  vec3 dCol = 0.3 * vec3(0.7);
  vec3 sCol = 0.2 * vec3(1, 1, 1);
  float ref = 0.01;
  float a = 1.;
  int tex_id = 1;

  return Material(aCol, dCol, sCol, ref, a, tex_id);
}

Material mHandle() {
  vec3 aCol = 0.5 * vec3(0.1);
  vec3 dCol = 0.5 * vec3(0.4);
  vec3 sCol = 0.6 * vec3(0);
  float ref = 0.0;
  float a = 1.;
  int tex_id = 1;

  return Material(aCol, dCol, sCol, ref, a, tex_id);
}

Material mBlade() {
  vec3 aCol = 1.5 * vec3(0.7, 0, 0);
  vec3 dCol = 0.6 * vec3(0.7, 0, 0);
  vec3 sCol = 1.0 * vec3(1, 1, 1);
  float ref = 1.5;
  float a = 50.;
  int tex_id = 0;

  return Material(aCol, dCol, sCol, ref, a, tex_id);
}

Material mPlane(vec3 p) {
  vec3 aCol = 0.4 * vec3(0.835, 1, 1);
  vec3 dCol = 0.3 * vec3(1.0);
  vec3 sCol = 0.0 * vec3(1.0);
  float ref = 0.1;
  float a = 1.0;
  int tex_id = -1;

  return Material(aCol, dCol, sCol, ref, a, tex_id);
}

// --------- Utils ---------

// --- Rotations ---

mat3 rotateX(in float angle) {
  float angle_cos = cos(angle);
  float angle_sin = sin(angle);
  return mat3(
      vec3(1, 0, 0),
      vec3(0, angle_cos, -angle_sin),
      vec3(0, angle_sin, angle_cos)
  );
}

mat3 rotateY(in float angle) {
  float angle_cos = cos(angle);
  float angle_sin = sin(angle);
  return mat3(
      vec3(angle_cos, 0, angle_sin),
      vec3(0, 1, 0),
      vec3(-angle_sin, 0, angle_cos)
  );
}

mat3 rotateZ(in float angle) {
  float angle_cos = cos(angle);
  float angle_sin = sin(angle);
  return mat3(
      vec3(angle_cos, -angle_sin, 0),
      vec3(angle_sin, angle_cos, 0),
      vec3(0, 0, 1)
  );
}

mat2 rotate2d(in float angle) {
  float angle_cos = cos(angle);
  float angle_sin = sin(angle);
  return mat2(
      vec2(angle_cos, angle_sin),
      vec2(-angle_sin, angle_cos)
  );
}

// --- SDF operations ---

Surface opUnion(Surface first_sur, Surface second_sur) {
  if (second_sur.sd < first_sur.sd) {
      return second_sur;
  }
  return first_sur;
}

Surface opSmoothUnion(Surface first_sur, Surface second_sur, in float coeff) {
  float h = clamp( 0.5 + 0.5 * (second_sur.sd - first_sur.sd) / coeff, 0.0, 1.0 );
  return Surface(second_sur.id, mix(second_sur.sd, first_sur.sd, h) - coeff * h * (1.0 - h), second_sur.material);
}

Surface opIntersection(Surface first_sur, Surface second_sur) {
  if (second_sur.sd < first_sur.sd) {
    return first_sur;
  }
  return second_sur;
}

Surface opSmoothIntersection(Surface first_sur, Surface second_sur, in float coeff) {
  float h = clamp( 0.5 - 0.5 * (second_sur.sd - first_sur.sd) / coeff, 0.0, 1.0 );
  return Surface(second_sur.id, mix(second_sur.sd, first_sur.sd, h) + coeff * h * (1.0 - h), second_sur.material);
}

Surface opSubtraction(Surface first_sur, Surface second_sur) {
  if (second_sur.sd > -first_sur.sd) {
    return second_sur;
  }
  return first_sur;
}


float opRound(in float sdf, in float rad) {
  return sdf - rad;
}

// --------- Metrics ---------

float lengthInf(in vec2 vector) {
  return max(abs(vector.x), abs(vector.y));
}

float lengthInf(in vec3 vector) {
  return max(abs(vector.x), max(abs(vector.y), abs(vector.z)));
}

// --------- Primitives ---------

float sdTriPrism(in vec3 pos, in vec2 h, in vec3 offset, in vec3 params, mat3 rotation, mat3 rotation_offset) {
  pos = pos * rotation;
  pos = (pos - offset) * rotation_offset;
  vec3 q = abs(pos);
  return max(q.z - h.y, max(q.x * params.x + pos.y * params.y, -pos.y) - h.x * params.z);
}

float sdPlane(in vec3 pos, in vec3 normal, in float offset) {
  return dot(pos, normal) + offset;
}

float sdSphere(in vec3 pos, in vec3 center, in float radius) {
  return length(pos - center) - radius;
}

float sdTor(in vec3 p, in vec2 t, in vec3 offset, mat3 rotation, mat3 scale) {
  p = (p - offset) * scale * rotation;
  vec2 q = vec2(length(p.xz) - t.x, p.y);
  return lengthInf(q) - t.y;
}

float sdBox(in vec3 pos, in vec3 size, in vec3 offset, mat3 rotation) {
  vec3 adj_pos = abs((pos - offset) * rotation) - size;
  return length(max(adj_pos, 0.0)) + min(max(adj_pos.x, max(adj_pos.y, adj_pos.z)), 0.0);
}

// --------- Camera ---------

mat3 camera(in vec3 camera_pos, in vec3 look_at_point) {
  vec3 camera_dir = normalize(look_at_point - camera_pos);
  vec3 camera_right = normalize(cross(vec3(0, 1, 0), camera_dir));
  vec3 camera_up = normalize(cross(camera_dir, camera_right));
    
  return mat3(-camera_right, camera_up, -camera_dir);
}

// --------- Render ---------

// --- Separate Objects ---

Surface sdOuterShell(in vec3 pos) { // unused
  Surface outer_shell = Surface(1, sdPlane(pos, vec3(0.0, 0.0, 1.0), 20.0), mPlane(pos));
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(0.0, 1.0, 0.0), 1.0), mPlane(pos)), outer_shell, 0.9); // floor plane
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(1.0, 0.0, 0.0), 20.0), mPlane(pos)), outer_shell, 0.9);
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(0.0, 0.0, -1.0), 20.0), mPlane(pos)), outer_shell, 0.9);
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(0.0, -1.0, 0.0), 20.0), mPlane(pos)), outer_shell, 0.9);
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(-1.0, 0.0, 0.0), 20.0), mPlane(pos)), outer_shell, 0.9);
  return outer_shell;
}

Surface sdBlade(in vec3 pos) { 
  Surface upper_blade = Surface(2, sdBox(pos, vec3(0.03, 3, 0.05), vec3(0, 0.07, 3), rotateX(kPi/2.0)), mBlade());
  Surface lower_blade = Surface(2, sdTriPrism(pos, vec2(0.5, 3), vec3(0, 0.005, 3), vec3(1.1, 0.4, 0.05), rotateZ(kPi), kIdentityMat), mBlade());
  Surface tip = Surface(2, sdTriPrism(pos, vec2(0.5, 0.1), vec3(0, -0.023, 6.0), vec3(1, 0.4, 0.05), rotateZ(kPi), rotateX(-kPi/ 2.0)), mBlade());
  Surface intersector = Surface(2, sdSphere(pos, vec3(0, 4, 0), 7.15), mBlade());
  Surface blade_full = opSmoothUnion(upper_blade, lower_blade, 0.003);
  blade_full = opSmoothUnion(blade_full, tip, 0.005);
  blade_full = opIntersection(intersector, blade_full);
  return blade_full;
}

Surface sdGuardHandle(in vec3 pos) {
  mat3 guard_scale = mat3(
      vec3(1, 0, 0),
      vec3(0, 1, 0),
      vec3(0, 0, 1.5)
  );
  Surface guard = Surface(2, sdTor(pos, vec2(0.3, 0.1), vec3(0), rotateX(kPi / 2.0), guard_scale), mGuard());
  Surface handle = Surface(2, opRound(sdBox(pos, vec3(0.07, 0.8, 0.07), vec3(0, 0, -0.8), rotateX(kPi / 2.0)), 0.05), mHandle());
  return opSmoothUnion(guard, handle, 0.2);
}

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
    return transformTexture(pos, iChannel2, normal, 5.0, vec3(0.1)) * transformTexture(pos, iChannel1, normal, 10.0, vec3(0.01)); //changed to fit texture
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
  //vec3 cubemap_reflection = texture(iChannel0, reflect(ray_dir, normal)).rgb; no cubemap - no reflection

  vec3 light_dir = normalize(light.pos - pos);
  vec3 ambient = material.ambient_color * applyTextures(pos, normal, material);
  if (material.reflection > 0.001) {
    ambient *= material.reflection;// * cubemap_reflection;
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
  vec3 color = vec3(1, 0.58, 0.58);//texture(iChannel0, ray_dir).rgb; // default color - cubemap - for now just color
    
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
  vec2 iResolution = pushConstant.iResolution;
  vec2 iMouse = {pushConstant.mouseX, -pushConstant.mouseY + iResolution.y}; // flipped mouse y coord
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
