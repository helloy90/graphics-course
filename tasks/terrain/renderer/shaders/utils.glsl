vec4 toTerrainCoords(vec4 vector) {
    return vector.xzyw;
}

vec3 toTerrainCoords(vec3 vector) {
    return vector.xzy;
}

vec2 getHorizontalCoords(vec4 vector) {
    return vector.xz;
}

vec2 getHorizontalCoords(vec3 vector) {
    return vector.xz;
}