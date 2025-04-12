#ifndef COMPLEX_GLSL_INCLUDED
#define COMPLEX_GLSL_INCLUDED

#define kPI 3.1415926535897932384626433832795

vec2 complexMult(vec2 first, vec2 second) {
    return vec2(first.x * second.x - first.y * second.y, 
                first.x * second.y + first.y * second.x);
}

vec2 euler(float x) {
    return vec2(cos(x), sin(x));
}

vec2 complexExponent(vec2 num) {
    return exp(num.x) * euler(num.y);
}

#endif // COMPLEX_GLSL_INCLUDED