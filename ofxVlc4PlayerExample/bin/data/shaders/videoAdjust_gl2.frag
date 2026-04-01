#version 120
uniform sampler2D tex0;
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float gammaValue;
uniform float hueDegrees;
varying vec2 vTexCoord;

vec3 applyHueRotation(vec3 color, float hueDegreesValue) {
    float angle = radians(hueDegreesValue);
    float cosA = cos(angle);
    float sinA = sin(angle);

    mat3 rgbToYiq = mat3(
        0.299, 0.587, 0.114,
        0.596, -0.274, -0.322,
        0.211, -0.523, 0.312
    );
    mat3 yiqToRgb = mat3(
        1.0, 0.956, 0.621,
        1.0, -0.272, -0.647,
        1.0, -1.106, 1.703
    );

    vec3 yiq = rgbToYiq * color;
    vec2 iq = mat2(cosA, -sinA, sinA, cosA) * yiq.yz;
    return yiqToRgb * vec3(yiq.x, iq.x, iq.y);
}

void main() {
    vec3 color = texture2D(tex0, vTexCoord).rgb;
    color = applyHueRotation(color, hueDegrees);
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, saturation);
    color = ((color - 0.5) * contrast) + 0.5;
    color += vec3(brightness - 1.0);
    color = clamp(color, 0.0, 1.0);
    color = pow(color, vec3(1.0 / max(gammaValue, 0.001)));
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
