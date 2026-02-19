#version 430 core

layout(location = 0) out vec4 o_hdr_color;
layout(location = 1) out vec4 o_linear_depth;

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;

uniform sampler2D u_transmittance_lut;

uniform vec3 u_sun_direction;
uniform float u_bottom_radius;
uniform float u_top_radius;
uniform vec3 u_camera_world_pos;

float safeSqrt(float v)
{
	return sqrt(max(v, 0.0));
}

void LutTransmittanceParamsToUv(in float viewHeight, in float viewZenithCosAngle, out vec2 uv)
{
	float H = safeSqrt(u_top_radius * u_top_radius - u_bottom_radius * u_bottom_radius);
	float rho = safeSqrt(viewHeight * viewHeight - u_bottom_radius * u_bottom_radius);
	float discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0) + u_top_radius * u_top_radius;
	float d = max(0.0, (-viewHeight * viewZenithCosAngle + safeSqrt(discriminant)));
	float dMin = u_top_radius - viewHeight;
	float dMax = rho + H;
	float xMu = (d - dMin) / max(dMax - dMin, 1e-6);
	float xR = rho / max(H, 1e-6);
	uv = vec2(xMu, xR);
}

void main()
{
	vec3 P0 = v_world_pos + vec3(0.0, 0.0, u_bottom_radius);
	float viewHeight = length(P0);
	vec3 upVector = P0 / max(viewHeight, 1e-6);
	float viewZenithCosAngle = dot(normalize(u_sun_direction), upVector);
	vec2 transUv;
	LutTransmittanceParamsToUv(viewHeight, viewZenithCosAngle, transUv);
	vec3 transmittance = texture(u_transmittance_lut, transUv).rgb;

	float nDotL = max(dot(normalize(v_normal), normalize(u_sun_direction)), 0.0);
	vec3 terrainColor = vec3(0.05) * nDotL * transmittance;
	o_hdr_color = vec4(terrainColor, 1.0);

	float linearDepthKm = length(v_world_pos - u_camera_world_pos);
	o_linear_depth = vec4(linearDepthKm, 0.0, 0.0, 1.0);
}
