#version 430 core

in vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_transmittance_tex;

void main()
{
	vec2 uv = clamp(v_uv, vec2(0.0), vec2(1.0));
	vec3 color = texture(u_transmittance_tex, uv).rgb;
	color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
	o_color = vec4(color, 1.0);
}

