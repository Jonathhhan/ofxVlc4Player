#version 120
uniform sampler2D tex0;
uniform vec3 leftTint;
uniform vec3 rightTint;
uniform float eyeSeparation;
uniform float swapEyes;
varying vec2 vTexCoord;

void main() {
    float leftSource = (swapEyes > 0.5) ? 0.5 : 0.0;
    float rightSource = (swapEyes > 0.5) ? 0.0 : 0.5;
    float leftX = clamp(leftSource + vTexCoord.x * 0.5 + eyeSeparation, leftSource, leftSource + 0.5);
    float rightX = clamp(rightSource + vTexCoord.x * 0.5 - eyeSeparation, rightSource, rightSource + 0.5);
    vec3 leftColor = texture2D(tex0, vec2(leftX, vTexCoord.y)).rgb;
    vec3 rightColor = texture2D(tex0, vec2(rightX, vTexCoord.y)).rgb;
    float leftLuma = dot(leftColor, vec3(0.299, 0.587, 0.114));
    float rightLuma = dot(rightColor, vec3(0.299, 0.587, 0.114));
    gl_FragColor = vec4(leftTint * leftLuma + rightTint * rightLuma, 1.0);
}
