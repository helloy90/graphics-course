#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out float fragColor;

layout(push_constant) uniform params {
  uvec2 iResolution;
} pushConstant;


float interpolate(float a0, float a1, float w) {
    // return (a1 - a0) * (3.0 - w * 2.0) * w * w + a0;

    return (a1 - a0) * ((w * (w * 6.0 - 15.0) + 10.0) * w * w * w) + a0;

}

vec2 randomGradient(int ix, int iy) {

    const uint w = 8 * 4;
    const uint s = w / 2;
    uint a = ix;
    uint b = iy;
    a *= 3284157443u;
    b ^= a << s | a >> (w - s);
    b *= 1911520717u;
    a ^= b << s | b >> (w - s);
    a *= 2048419325u;
    float random = a * (3.14159265 / ~(~0u >> 1));
    return vec2(cos(random), sin(random));
}

float dotGridGradient(int ix, int iy, float x, float y) {
    vec2 gradient = randomGradient(ix, iy);

    float dx = x - float(ix);
    float dy = y - float(iy);

    return (dx * gradient.x + dy * gradient.y);
}

float map(float value, float fromLow, float fromHigh, float toLow, float toHigh) {
    value = clamp(value, fromLow, fromHigh);

    return toLow + (toHigh - toLow) * ((value - fromLow) / (fromHigh - fromLow));
}

float perlin(vec2 vector, int octaves) {
  float frequency = 1.0;
  float amplitude = 1.0;
  float total = 0.0;

  for (int i = 0; i < octaves; i++) {

    vector *= frequency;

    int x0 = int(floor(vector.x));
    int x1 = x0 + 1;
    int y0 = int(floor(vector.y));
    int y1 = y0 + 1;

    float sx = vector.x - float(x0);
    float sy = vector.y - float(y0);

    float n0 = dotGridGradient(x0, y0, vector.x, vector.y);
    float n1 = dotGridGradient(x1, y0, vector.x, vector.y);
    float ix0 = interpolate(n0, n1, sx);

    n0 = dotGridGradient(x0, y1, vector.x, vector.y);
    n1 = dotGridGradient(x1, y1, vector.x, vector.y);

    float ix1 = interpolate(n0, n1, sx);

    float value = interpolate(ix0, ix1, sy);

    value = map(value, -1.0, 1.0, -amplitude, amplitude);

    total += value;

    frequency *= 2.0;
    amplitude *= 0.5;
  }
  return total * 0.5 + 0.5; // Will return in range -1 to 1. To make it in range 0 to 1, multiply by 0.5 and add 0.5
}

void main() {
  vec2 iResolution = pushConstant.iResolution;
  // vec2 fragCoord = vec2(gl_FragCoord.x, iResolution.y - gl_FragCoord.y); // flipped screen y coord

  fragColor = perlin(vec2(gl_FragCoord / 256), 5);
}