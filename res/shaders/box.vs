#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in float aFaceId;

out vec2 TexCoords;
flat out int FaceId;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 box_size;
uniform vec3 box_offset;

void main()
{
    vec3 worldPos = aPos * box_size + box_offset;
    gl_Position = projection * view * vec4(worldPos, 1.0);
    TexCoords = aTexCoord * max(box_size.x, max(box_size.y, box_size.z));
    FaceId = int(aFaceId);
}
