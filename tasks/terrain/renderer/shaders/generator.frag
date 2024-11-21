#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out float fragColor;

layout(push_constant) uniform params {
  uvec2 iResolution;
} pushConstant;

/* Function to linearly interpolate between a0 and a1
 * Weight w should be in the range [0.0, 1.0]
 */
float interpolate(float a0, float a1, float w) {
    /* // You may want clamping by inserting:
     * if (0.0 > w) return a0;
     * if (1.0 < w) return a1;
     */
    // return (a1 - a0) * w + a0;
    /* // Use this cubic interpolation [[Smoothstep]] instead, for a smooth appearance:
     *
     */

    return (a1 - a0) * (3.0 - w * 2.0) * w * w + a0;
     /* // Use [[Smootherstep]] for an even smoother result with a second derivative equal to zero on boundaries:
     * return (a1 - a0) * ((w * (w * 6.0 - 15.0) + 10.0) * w * w * w) + a0;
     */
}

/* Create pseudorandom direction vector
 */
vec2 randomGradient(int ix, int iy) {
    // No precomputed gradients mean this works for any number of grid coordinates
    const uint w = 8 * 4; // 4
    const uint s = w / 2; // rotation width
    uint a = ix;
    uint b = iy;
    a *= 3284157443;
    b ^= a << s | a >> w-s;
    b *= 1911520717;
    a ^= b << s | b >> w-s;
    a *= 2048419325;
    float random = a * (3.14159265 / ~(~0u >> 1)); // in [0, 2*Pi]
    vec2 v = {cos(random), sin(random)};
    return v;
}

// Computes the dot product of the distance and gradient vectors.
float dotGridGradient(int ix, int iy, float x, float y) {
    // Get gradient from integer coordinates
    vec2 gradient = randomGradient(ix, iy);

    // Compute the distance vector
    float dx = x - float(ix);
    float dy = y - float(iy);

    // Compute the dot-product
    return (dx * gradient.x + dy * gradient.y);
}

// Compute Perlin noise at coordinates x, y
float perlin(vec2 vector) {
    // Determine grid cell coordinates
    int x0 = int(floor(vector.x));
    int x1 = x0 + 1;
    int y0 = int(floor(vector.y));
    int y1 = y0 + 1;

    // Determine interpolation weights
    // Could also use higher order polynomial/s-curve here
    float sx = vector.x - float(x0);
    float sy = vector.y - float(y0);

    // Interpolate between grid point gradients
    float n0, n1, ix0, ix1, value;

    n0 = dotGridGradient(x0, y0, vector.x, vector.y);
    n1 = dotGridGradient(x1, y0, vector.x, vector.y);
    // ix0 = interpolate(n0, n1, sx);
    ix0 = smoothstep(n0, n1, sx);

    n0 = dotGridGradient(x0, y1, vector.x, vector.y);
    n1 = dotGridGradient(x1, y1, vector.x, vector.y);
    // ix1 = interpolate(n0, n1, sx);
    ix1 = smoothstep(n0, n1, sx);

    // value = interpolate(ix0, ix1, sy);
    value = smoothstep(ix0, ix1, sy);
    return value * 0.5 + 0.5; // Will return in range -1 to 1. To make it in range 0 to 1, multiply by 0.5 and add 0.5
}

void main() {
  vec2 iResolution = pushConstant.iResolution;
  vec2 fragCoord = vec2(gl_FragCoord.x, iResolution.y - gl_FragCoord.y); // flipped screen y coord

  vec2 uv_coord = fragCoord/iResolution.xy;
//   vec3 color = vec3(perlin(fragCoord), 0, 0);

  fragColor = perlin(vec2(fragCoord));
}