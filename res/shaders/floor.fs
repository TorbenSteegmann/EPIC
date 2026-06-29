#version 330 core
in vec3 WorldPos;
out vec4 FragColor;

uniform vec3 cam_pos;
uniform vec2 grid_min;
uniform vec2 grid_max;
uniform int show_grid_plate;

float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i)
    {
        v += a * noise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

float grid_line(vec2 p, float spacing, float width)
{
    vec2 coord = p / spacing;
    vec2 cell = abs(fract(coord - 0.5) - 0.5);
    vec2 aa = fwidth(coord) * 1.5;
    vec2 line = 1.0 - smoothstep(vec2(width), vec2(width) + aa, cell);
    return max(line.x, line.y);
}

float plate_mask(vec2 p)
{
    vec2 edge = vec2(3.0);
    vec2 lo = smoothstep(grid_min, grid_min + edge, p);
    vec2 hi = 1.0 - smoothstep(grid_max - edge, grid_max, p);
    return lo.x * lo.y * hi.x * hi.y * float(show_grid_plate);
}

void main()
{
    vec2 xz = WorldPos.xz;

    float terrain = fbm(xz * 0.012);
    float fine = fbm(xz * 0.052 + vec2(18.7, 4.3));
    float dist = length(cam_pos.xz - xz);
    float fade = smoothstep(260.0, 1700.0, dist);

    vec3 col = mix(vec3(0.12, 0.18, 0.16), vec3(0.25, 0.31, 0.26), terrain);
    col += (fine - 0.5) * 0.035;
    col = mix(col, vec3(0.40, 0.45, 0.44), fade);

    float plate = plate_mask(xz);
    float minor = grid_line(xz, 5.0, 0.014) * plate;
    float major = grid_line(xz, 25.0, 0.018) * plate;
    float x_axis = (1.0 - smoothstep(0.0, 0.20, abs(WorldPos.z))) * plate;
    float z_axis = (1.0 - smoothstep(0.0, 0.20, abs(WorldPos.x))) * plate;

    col = mix(col, vec3(0.22, 0.29, 0.30), plate * 0.42);
    col = mix(col, vec3(0.56, 0.68, 0.70), minor * 0.38);
    col = mix(col, vec3(0.90, 0.96, 0.97), major * 0.58);
    col = mix(col, vec3(0.84, 0.46, 0.38), z_axis * 0.55);
    col = mix(col, vec3(0.40, 0.67, 0.88), x_axis * 0.55);

    FragColor = vec4(col, 1.0);
}
