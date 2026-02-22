#version 430 core

in vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_hdr_tex;

void main()
{
	vec3 hdr = texture(u_hdr_tex, clamp(v_uv, vec2(0.0), vec2(1.0))).rgb;
	const vec3 white_point = vec3(1.08241, 0.96756, 0.95003);
	const float exposure = 10.0;
	vec3 color = vec3(1.0) - exp(-max(hdr, vec3(0.0)) / white_point * exposure);
	color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
	o_color = vec4(color, 1.0);
}
