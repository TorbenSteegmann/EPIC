#version 330 core
out vec4 FragColor;

uniform vec4 meshColor;

void main()
{
    FragColor = meshColor;
}
