#version 330 core
in vec3 ViewDir;
out vec4 FragColor;

uniform vec3 cam_pos;
uniform vec3 sun_dir; // normalized direction TO the sun

void main()
{
    vec3 dir = normalize(ViewDir - cam_pos);

    // Vertical gradient: how far above/below horizon
    float horizon = dir.y;

    // Sky colours
    vec3 zenith  = vec3(0.20, 0.40, 0.85);
    vec3 horiz   = vec3(0.60, 0.75, 0.90);
    vec3 ground  = vec3(0.25, 0.22, 0.20);

    // Blend sky
    vec3 sky = mix(horiz, zenith, clamp(horizon, 0.0, 1.0));
    // Below horizon: darken toward ground colour
    sky = mix(sky, ground, clamp(-horizon * 4.0, 0.0, 1.0));

    // Sun disc
    float sun_dot = max(dot(dir, sun_dir), 0.0);
    float sun_disc = smoothstep(0.997, 0.999, sun_dot);
    vec3 sun_color = vec3(1.0, 0.95, 0.8);
    sky += sun_disc * sun_color;

    // Sun glow halo
    float glow = pow(sun_dot, 128.0);
    sky += glow * sun_color * 0.4;

    // Horizon haze
    float haze = 1.0 - abs(horizon);
    haze = pow(haze, 8.0);
    sky = mix(sky, vec3(0.75, 0.82, 0.9), haze * 0.5);

    FragColor = vec4(sky, 1.0);
}
