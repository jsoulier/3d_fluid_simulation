#version 450

#include "shader.glsl"

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D inVelocityX;
layout(set = 2, binding = 1) uniform sampler2D inVelocityY;
layout(set = 2, binding = 2) uniform sampler2D inDensity;

void main()
{
    float density = texture(inDensity, inTexCoord).x;
    outColor = vec4(density * 100.0f);
}