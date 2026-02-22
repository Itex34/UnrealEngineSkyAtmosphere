#version 430 core

out vec2 v_uv;

void main()
{
	const vec2 vertices[3] = vec2[3](
		vec2(-1.0, -1.0),
		vec2(3.0, -1.0),
		vec2(-1.0, 3.0)
	);
	vec2 p = vertices[gl_VertexID];
	gl_Position = vec4(p, 0.0, 1.0);
	v_uv = p * 0.5 + 0.5;
}

