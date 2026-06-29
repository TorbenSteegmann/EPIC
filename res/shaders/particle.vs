#version 330 core
layout(location = 0) in vec4 aPosTex;     // quad pos + tex
layout(location = 1) in vec3 iPosition;   // instance world position
layout(location = 2) in float iRadius;    // instance radius
layout(location = 3) in vec3 iVelocity;   // instance velocity
layout(location = 4) in float iHighlight; // 0 normal, 1 selected, 2 CFL, 3 both

out vec2 TexCoords;
out vec3 Velocity;
out float Highlight;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    // Billboard: extract camera right and up from the view matrix
    vec3 camRight = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 camUp    = vec3(view[0][1], view[1][1], view[2][1]);

    // aPosTex.xy is [0,1], remap to [-1,1] then scale by radius
    vec2 offset = (aPosTex.xy - 0.5) * iRadius * 2.0;

    vec3 worldPos = iPosition + camRight * offset.x + camUp * offset.y;

    gl_Position = projection * view * vec4(worldPos, 1.0);
    TexCoords = aPosTex.zw;
    Velocity = iVelocity;
    Highlight = iHighlight;
}
