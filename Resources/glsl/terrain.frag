#version 430 core

layout(location = 0) out vec4 o_hdr_color;
layout(location = 1) out vec4 o_linear_depth;

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;

uniform sampler2D u_transmittance_lut;
uniform sampler2DShadow u_shadowmap_tex;

uniform vec3 u_sun_direction;
uniform float u_bottom_radius;
uniform float u_top_radius;
uniform vec3 u_camera_world_pos;
uniform mat4 u_shadow_view_proj;

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

float sampleTerrainShadow(in vec3 worldPos)
{
	vec4 shadowClip = u_shadow_view_proj * vec4(worldPos, 1.0);
	vec3 shadowNdc = shadowClip.xyz / max(shadowClip.w, 1e-6);
	vec2 shadowUv = vec2(shadowNdc.x * 0.5 + 0.5, shadowNdc.y * 0.5 + 0.5);
	float shadowDepth = shadowNdc.z * 0.5 + 0.5;
	if (shadowUv.x < 0.0 || shadowUv.x >= 1.0 || shadowUv.y < 0.0 || shadowUv.y >= 1.0 || shadowDepth < 0.0 || shadowDepth >= 1.0)
	{
		return 1.0;
	}
	float shadow = texture(u_shadowmap_tex, vec3(shadowUv, shadowDepth));
	float count = 1.0;
	for (int y = -3; y <= 3; ++y)
	{
		for (int x = -3; x <= 3; ++x)
		{
			float offsetx = float(x) * 0.0001;
			float offsety = float(y) * 0.0001;
			vec2 uv = shadowUv + vec2(-offsetx, -offsety);
			shadow += texture(u_shadowmap_tex, vec3(uv, shadowDepth));
			count += 1.0;
		}
	}
	return shadow / max(count, 1.0);
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
	float sunShadow = sampleTerrainShadow(v_world_pos);
	vec3 terrainColor = vec3(0.05) * sunShadow * nDotL * transmittance;
	o_hdr_color = vec4(terrainColor, 1.0);

	float linearDepthKm = length(v_world_pos - u_camera_world_pos);
	o_linear_depth = vec4(linearDepthKm, 0.0, 0.0, 1.0);
}
