#version 450

#include "shader.glsl"

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D inVelocityX;
layout(set = 2, binding = 1) uniform sampler2D inVelocityY;
layout(set = 2, binding = 2) uniform sampler2D inDensity;

const float Color = 10000.0f;
const float Density = 10.0f;

void main()
{
    float red = abs(texture(inVelocityX, inTexCoord).x) * Color;
    float green = abs(texture(inVelocityY, inTexCoord).x) * Color;
    float density = texture(inDensity, inTexCoord).x * Density;
    outColor = vec4(red, green, 0.0f, density);
}