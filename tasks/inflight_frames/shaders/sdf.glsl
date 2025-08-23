struct Surface {
  int id;
  float sd;
  Material material;
};

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

