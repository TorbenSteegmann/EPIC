#version 330 core
layout(location = 0) in vec4 vertex; // xy position, zw texcoord

out vec3 WorldPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 world = model * vec4(vertex.xy, 0.0, 1.0);
    WorldPos = world.xyz;
    gl_Position = projection * view * world;
}
