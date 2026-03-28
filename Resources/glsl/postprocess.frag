#version 430 core

in vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_hdr_tex;
uniform int u_enable_tonemap;
uniform int u_tonemap_mode;
uniform float u_exposure;
uniform int u_enable_gamma;
uniform float u_output_gamma;

vec3 tonemapExponential(vec3 linearColor)
{
	const vec3 whitePoint = vec3(1.08241, 0.96756, 0.95003);
	return vec3(1.0) - exp(-linearColor / whitePoint);
}

vec3 agxDefaultContrastApprox(vec3 x)
{
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;
	return 15.5 * x4 * x2
		- 40.14 * x4 * x
		+ 31.96 * x4
		- 6.868 * x2 * x
		+ 0.4298 * x2
		+ 0.1191 * x
		- 0.00232;
}

vec3 tonemapAgxBase(vec3 linearColor)
{
	const mat3 agxMat = mat3(
		0.842479062253094, 0.0784335999999992, 0.0792237451477643,
		0.0423282422610123, 0.878468636469772, 0.0791661274605434,
		0.0423756549057051, 0.0784336,          0.879142973793104);

	const mat3 agxMatInv = mat3(
		1.19687900512017,  -0.0980208811401368, -0.0990297440797205,
		-0.0528968517574562, 1.15190312990417,  -0.0989611768448433,
		-0.0529716355144438, -0.0980434501171241, 1.15107367264116);

	const float minEv = -12.47393;
	const float maxEv = 4.026069;
	const float evRange = maxEv - minEv;

	vec3 color = agxMat * max(linearColor, vec3(0.0));
	color = clamp((log2(max(color, vec3(1e-6))) - minEv) / evRange, vec3(0.0), vec3(1.0));
	color = agxDefaultContrastApprox(color);
	color = agxMatInv * color;
	return clamp(color, vec3(0.0), vec3(1.0));
}

vec3 agxLookPunchy(vec3 color)
{
	const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
	const float slope = 1.0;
	const float power = 1.35;
	const float saturation = 1.4;

	float luma = dot(color, lumaWeights);
	vec3 looked = pow(max(color * slope, vec3(0.0)), vec3(power));
	return clamp(vec3(luma) + saturation * (looked - vec3(luma)), vec3(0.0), vec3(1.0));
}

void main()
{
	vec3 hdr = texture(u_hdr_tex, clamp(v_uv, vec2(0.0), vec2(1.0))).rgb;
	vec3 linearColor = max(hdr, vec3(0.0)) * max(u_exposure, 0.0);
	vec3 color = linearColor;

	if (u_enable_tonemap != 0)
	{
		if (u_tonemap_mode == 0)
		{
			color = tonemapExponential(linearColor);
		}
		else if (u_tonemap_mode == 1)
		{
			color = tonemapAgxBase(linearColor);
		}
		else
		{
			color = agxLookPunchy(tonemapAgxBase(linearColor));
		}
	}

	if (u_enable_gamma != 0)
	{
		float invGamma = 1.0 / max(u_output_gamma, 1e-4);
		color = pow(max(color, vec3(0.0)), vec3(invGamma));
	}

	o_color = vec4(color, 1.0);
}
