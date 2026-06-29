#version 330 core
in vec2 TexCoords;
flat in int FaceId;
out vec4 FragColor;

uniform sampler2D blockTex;

void main()
{
    vec2 cell = fract(TexCoords / 10.0);
    vec2 major_cell = fract(TexCoords / 50.0);
    float minor_line = 1.0 - smoothstep(0.0, 0.035, min(min(cell.x, 1.0 - cell.x), min(cell.y, 1.0 - cell.y)));
    float major_line = 1.0 - smoothstep(0.0, 0.018, min(min(major_cell.x, 1.0 - major_cell.x), min(major_cell.y, 1.0 - major_cell.y)));

    vec3 base = vec3(0.55, 0.68, 0.72);
    vec3 axis_tint[6] = vec3[6](
        vec3(0.50, 0.64, 0.78),
        vec3(0.50, 0.64, 0.78),
        vec3(0.76, 0.48, 0.42),
        vec3(0.76, 0.48, 0.42),
        vec3(0.48, 0.70, 0.52),
        vec3(0.48, 0.70, 0.52)
    );

    vec3 color = mix(base, axis_tint[FaceId], 0.35);
    color = mix(color, vec3(0.88, 0.94, 0.96), minor_line * 0.28);
    color = mix(color, vec3(1.0), major_line * 0.55);

    float alpha = 0.11 + minor_line * 0.11 + major_line * 0.24;
    FragColor = vec4(color, alpha);
}
