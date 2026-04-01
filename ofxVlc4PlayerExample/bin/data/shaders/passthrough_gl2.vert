#version 120
varying vec2 vTexCoord;

void main() {
    vTexCoord = gl_MultiTexCoord0.xy;
    gl_Position = ftransform();
}
