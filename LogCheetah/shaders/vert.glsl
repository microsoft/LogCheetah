#version 330 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 yzOffset;

out vec3 modelPos;
out vec4 baseColor;

uniform mat4 viewProj;

void main()
{
    vec4 finalVertexPos = vec4(vertexPosition, 1.0);
    finalVertexPos.yz += yzOffset.xy;

    gl_Position = viewProj * finalVertexPos;
    modelPos = vertexPosition;
    baseColor = vertexColor;
}
