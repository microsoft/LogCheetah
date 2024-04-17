#version 330 core

out vec4 color;

in vec3 modelPos;
in vec4 baseColor;

void main()
{
    float bMod = sqrt(modelPos.y);

    float darkenMod = modelPos.z * modelPos.z * 0.5 + 0.5;
    color.rgb = baseColor.rgb * darkenMod;
    color.b = color.b * 0.75 + 0.25 * bMod;

    color.a = baseColor.a;
}
