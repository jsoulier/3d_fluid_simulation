#version 450

#include "shader.glsl"

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D inVelocityX;
layout(set = 2, binding = 1) uniform sampler2D inVelocityY;
layout(set = 2, binding = 2) uniform sampler2D inDensity;

const float Color = 1000.0f;
const float Density = 5.0f;

void main()
{
    float blue = texture(inVelocityX, inTexCoord).x * Color;
    float green = texture(inVelocityY, inTexCoord).x * Color;
    float red = abs(min(blue, green));
    float density = texture(inDensity, inTexCoord).x * Density;
    outColor = vec4(red, green, blue, density);
}