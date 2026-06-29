#version 330 core
in vec2 TexCoords;
in vec3 Velocity;
in float Highlight;
out vec4 FragColor;

uniform sampler2D circleTex;
uniform float max_speed; // maximum expected velocity magnitude
uniform bool use_solid_color;
uniform vec3 solid_color;

void main()
{
    float alpha = texture(circleTex, TexCoords).r;

    // Compute color based on velocity magnitude
    float speed = length(Velocity);
    float t = clamp(speed / max_speed, 0.0, 1.0);

    // Keep 3D comparison imagery visually neutral. The renderer enables the
    // solid branch there; 2D retains its diagnostic speed/highlight colours.
    vec3 color = solid_color;
    if (!use_solid_color)
    {
        color = mix(vec3(0.2, 0.5, 1.0), vec3(1.0, 0.2, 0.0), t);
        if (Highlight > 2.5)
            color = vec3(1.0, 1.0, 1.0);
        else if (Highlight > 1.5)
            color = vec3(1.0, 0.0, 0.9);
        else if (Highlight > 0.5)
            color = vec3(1.0, 1.0, 0.0);
    }

    FragColor = vec4(color, alpha);
}
