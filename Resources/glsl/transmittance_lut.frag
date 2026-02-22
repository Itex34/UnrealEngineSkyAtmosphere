#version 430 core

layout(location = 0) out vec4 o_color;

uniform float u_bottom_radius;
uniform float u_top_radius;
uniform vec3 u_rayleigh_scattering;
uniform vec3 u_mie_extinction;
uniform vec3 u_absorption_extinction;
uniform float u_rayleigh_density_exp_scale;
uniform float u_mie_density_exp_scale;
uniform float u_absorption_layer0_width;
uniform float u_absorption_layer0_linear_term;
uniform float u_absorption_layer0_constant_term;
uniform float u_absorption_layer1_linear_term;
uniform float u_absorption_layer1_constant_term;
uniform int u_transmittance_width;
uniform int u_transmittance_height;

float clampCosine(float mu)
{
	return clamp(mu, -1.0, 1.0);
}

float safeSqrt(float x)
{
	return sqrt(max(x, 0.0));
}

float distanceToTopAtmosphereBoundary(float r, float mu)
{
	float discriminant = r * r * (mu * mu - 1.0) + u_top_radius * u_top_radius;
	return max(-r * mu + safeSqrt(discriminant), 0.0);
}

float rayleighDensity(float altitude)
{
	return clamp(exp(u_rayleigh_density_exp_scale * altitude), 0.0, 1.0);
}

float mieDensity(float altitude)
{
	return clamp(exp(u_mie_density_exp_scale * altitude), 0.0, 1.0);
}

float absorptionDensity(float altitude)
{
	if (altitude < u_absorption_layer0_width)
	{
		return clamp(u_absorption_layer0_linear_term * altitude + u_absorption_layer0_constant_term, 0.0, 1.0);
	}
	return clamp(u_absorption_layer1_linear_term * altitude + u_absorption_layer1_constant_term, 0.0, 1.0);
}

float unitRangeFromTextureCoord(float u, int size)
{
	float fs = float(size);
	return (u - 0.5 / fs) / (1.0 - 1.0 / fs);
}

void getRMuFromTransmittanceTextureUv(vec2 uv, out float r, out float mu)
{
	float x_mu = unitRangeFromTextureCoord(uv.x, u_transmittance_width);
	float x_r = unitRangeFromTextureCoord(uv.y, u_transmittance_height);

	float H = safeSqrt(u_top_radius * u_top_radius - u_bottom_radius * u_bottom_radius);
	float rho = H * x_r;
	r = safeSqrt(rho * rho + u_bottom_radius * u_bottom_radius);

	float d_min = u_top_radius - r;
	float d_max = rho + H;
	float d = d_min + x_mu * (d_max - d_min);
	mu = (d == 0.0) ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
	mu = clampCosine(mu);
}

vec3 computeTransmittanceToTopAtmosphereBoundary(float r, float mu)
{
	const int sampleCount = 128;
	float dx = distanceToTopAtmosphereBoundary(r, mu) / float(sampleCount);

	float opticalRayleigh = 0.0;
	float opticalMie = 0.0;
	float opticalAbsorption = 0.0;

	for (int i = 0; i <= sampleCount; ++i)
	{
		float di = float(i) * dx;
		float ri = safeSqrt(di * di + 2.0 * r * mu * di + r * r);
		float altitude = ri - u_bottom_radius;
		float w = (i == 0 || i == sampleCount) ? 0.5 : 1.0;

		opticalRayleigh += rayleighDensity(altitude) * w * dx;
		opticalMie += mieDensity(altitude) * w * dx;
		opticalAbsorption += absorptionDensity(altitude) * w * dx;
	}

	vec3 extinction = u_rayleigh_scattering * opticalRayleigh +
		u_mie_extinction * opticalMie +
		u_absorption_extinction * opticalAbsorption;

	return exp(-extinction);
}

void main()
{
	vec2 uv = gl_FragCoord.xy / vec2(float(u_transmittance_width), float(u_transmittance_height));
	float r = 0.0;
	float mu = 1.0;
	getRMuFromTransmittanceTextureUv(uv, r, mu);
	vec3 transmittance = computeTransmittanceToTopAtmosphereBoundary(r, mu);
	o_color = vec4(transmittance, 1.0);
}

