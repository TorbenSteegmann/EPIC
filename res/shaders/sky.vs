#version 330 core
layout(location = 0) in vec2 aPos;

out vec3 ViewDir;

uniform mat4 inv_vp;

void main()
{
    gl_Position = vec4(aPos, 0.9999, 1.0);

    // Unproject clip-space corners to world-space view direction
    vec4 world = inv_vp * vec4(aPos, 1.0, 1.0);
    ViewDir = world.xyz / world.w;
}
