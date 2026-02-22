#version 430 core

layout(location = 0) out vec4 o_color;

uniform sampler2D u_transmittance_lut;
uniform sampler2D u_multiscattering_lut;

uniform vec3 u_sun_direction;
uniform float u_bottom_radius;
uniform float u_top_radius;
uniform vec3 u_rayleigh_scattering;
uniform vec3 u_mie_scattering;
uniform vec3 u_mie_extinction;
uniform vec3 u_mie_absorption;
uniform vec3 u_absorption_extinction;
uniform vec3 u_ground_albedo;
uniform float u_mie_phase_g;
uniform float u_rayleigh_density_exp_scale;
uniform float u_mie_density_exp_scale;
uniform float u_absorption_layer0_width;
uniform float u_absorption_layer0_linear_term;
uniform float u_absorption_layer0_constant_term;
uniform float u_absorption_layer1_linear_term;
uniform float u_absorption_layer1_constant_term;
uniform int u_transmittance_width;
uniform int u_transmittance_height;
uniform int u_multi_scattering_lut_res;
uniform int u_skyview_width;
uniform int u_skyview_height;
uniform float u_camera_height;
uniform vec3 u_camera_offset;
uniform float u_raymarch_spp_min;
uniform float u_raymarch_spp_max;
uniform float u_sun_illuminance;

const float PI = 3.1415926535897932384626433832795;
const float PLANET_RADIUS_OFFSET = 0.01;

float saturate1(float v) { return clamp(v, 0.0, 1.0); }
vec3 saturate3(vec3 v) { return clamp(v, vec3(0.0), vec3(1.0)); }
float safeSqrt(float v) { return sqrt(max(v, 0.0)); }

float fromSubUvsToUnit(float u, float resolution)
{
	return (u - 0.5 / resolution) * (resolution / (resolution - 1.0));
}

float fromUnitToSubUvs(float u, float resolution)
{
	return (u + 0.5 / resolution) * (resolution / (resolution + 1.0));
}

void UvToSkyViewLutParams(out float viewZenithCosAngle, out float lightViewCosAngle, in float viewHeight, in vec2 uv)
{
	uv = vec2(fromSubUvsToUnit(uv.x, 192.0), fromSubUvsToUnit(uv.y, 108.0));

	float Vhorizon = safeSqrt(viewHeight * viewHeight - u_bottom_radius * u_bottom_radius);
	float CosBeta = Vhorizon / viewHeight;
	float Beta = acos(CosBeta);
	float ZenithHorizonAngle = PI - Beta;

	if (uv.y < 0.5)
	{
		float coord = 2.0 * uv.y;
		coord = 1.0 - coord;
		coord *= coord;
		coord = 1.0 - coord;
		viewZenithCosAngle = cos(ZenithHorizonAngle * coord);
	}
	else
	{
		float coord = uv.y * 2.0 - 1.0;
		coord *= coord;
		viewZenithCosAngle = cos(ZenithHorizonAngle + Beta * coord);
	}

	float coord = uv.x;
	coord *= coord;
	lightViewCosAngle = -(coord * 2.0 - 1.0);
}

float raySphereIntersectNearest(vec3 r0, vec3 rd, vec3 s0, float sR)
{
	float a = dot(rd, rd);
	vec3 s0_r0 = r0 - s0;
	float b = 2.0 * dot(rd, s0_r0);
	float c = dot(s0_r0, s0_r0) - (sR * sR);
	float delta = b * b - 4.0 * a * c;
	if (delta < 0.0 || a == 0.0)
	{
		return -1.0;
	}
	float sol0 = (-b - sqrt(delta)) / (2.0 * a);
	float sol1 = (-b + sqrt(delta)) / (2.0 * a);
	if (sol0 < 0.0 && sol1 < 0.0)
	{
		return -1.0;
	}
	if (sol0 < 0.0)
	{
		return max(0.0, sol1);
	}
	if (sol1 < 0.0)
	{
		return max(0.0, sol0);
	}
	return max(0.0, min(sol0, sol1));
}

void LutTransmittanceParamsToUv(in float viewHeight, in float viewZenithCosAngle, out vec2 uv)
{
	float H = safeSqrt(u_top_radius * u_top_radius - u_bottom_radius * u_bottom_radius);
	float rho = safeSqrt(viewHeight * viewHeight - u_bottom_radius * u_bottom_radius);

	float discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0) + u_top_radius * u_top_radius;
	float d = max(0.0, (-viewHeight * viewZenithCosAngle + safeSqrt(discriminant)));

	float d_min = u_top_radius - viewHeight;
	float d_max = rho + H;
	float x_mu = (d - d_min) / max(d_max - d_min, 1e-6);
	float x_r = rho / max(H, 1e-6);
	uv = vec2(x_mu, x_r);
}

struct MediumSampleRGB
{
	vec3 scattering;
	vec3 absorption;
	vec3 extinction;

	vec3 scatteringMie;
	vec3 absorptionMie;
	vec3 extinctionMie;

	vec3 scatteringRay;
	vec3 absorptionRay;
	vec3 extinctionRay;
};

MediumSampleRGB sampleMediumRGB(in vec3 worldPos)
{
	float viewHeight = length(worldPos) - u_bottom_radius;

	float densityMie = exp(u_mie_density_exp_scale * viewHeight);
	float densityRay = exp(u_rayleigh_density_exp_scale * viewHeight);
	float densityOzo = saturate1(viewHeight < u_absorption_layer0_width ?
		u_absorption_layer0_linear_term * viewHeight + u_absorption_layer0_constant_term :
		u_absorption_layer1_linear_term * viewHeight + u_absorption_layer1_constant_term);

	MediumSampleRGB s;
	s.scatteringMie = densityMie * u_mie_scattering;
	s.absorptionMie = densityMie * u_mie_absorption;
	s.extinctionMie = densityMie * u_mie_extinction;

	s.scatteringRay = densityRay * u_rayleigh_scattering;
	s.absorptionRay = vec3(0.0);
	s.extinctionRay = s.scatteringRay;

	vec3 absorptionOzo = densityOzo * u_absorption_extinction;
	s.scattering = s.scatteringMie + s.scatteringRay;
	s.absorption = s.absorptionMie + absorptionOzo;
	s.extinction = s.extinctionMie + s.extinctionRay + absorptionOzo;
	return s;
}

float RayleighPhase(float cosTheta)
{
	return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

float CornetteShanksMiePhaseFunction(float g, float cosTheta)
{
	float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
	return k * (1.0 + cosTheta * cosTheta) / pow(1.0 + g * g - 2.0 * g * -cosTheta, 1.5);
}

bool MoveToTopAtmosphere(inout vec3 WorldPos, in vec3 WorldDir, in float AtmosphereTopRadius)
{
	float viewHeight = length(WorldPos);
	if (viewHeight > AtmosphereTopRadius)
	{
		float tTop = raySphereIntersectNearest(WorldPos, WorldDir, vec3(0.0), AtmosphereTopRadius);
		if (tTop >= 0.0)
		{
			vec3 UpVector = WorldPos / viewHeight;
			vec3 UpOffset = UpVector * -PLANET_RADIUS_OFFSET;
			WorldPos = WorldPos + WorldDir * tTop + UpOffset;
		}
		else
		{
			return false;
		}
	}
	return true;
}

vec3 GetMultipleScattering(in vec3 worldPos, in float viewZenithCosAngle)
{
	float lutRes = float(u_multi_scattering_lut_res);
	vec2 uv = clamp(
		vec2(
			viewZenithCosAngle * 0.5 + 0.5,
			(length(worldPos) - u_bottom_radius) / max(u_top_radius - u_bottom_radius, 1e-6)),
		vec2(0.0),
		vec2(1.0));
	uv = vec2(fromUnitToSubUvs(uv.x, lutRes), fromUnitToSubUvs(uv.y, lutRes));
	return texture(u_multiscattering_lut, uv).rgb;
}

vec3 IntegrateScatteredLuminance(
	in vec3 WorldPos, in vec3 WorldDir, in vec3 SunDir,
	in bool ground, in float SampleCountIni, in bool VariableSampleCount, in bool MieRayPhase)
{
	vec3 earthO = vec3(0.0);
	float tBottom = raySphereIntersectNearest(WorldPos, WorldDir, earthO, u_bottom_radius);
	float tTop = raySphereIntersectNearest(WorldPos, WorldDir, earthO, u_top_radius);
	float tMax = 0.0;
	if (tBottom < 0.0)
	{
		if (tTop < 0.0)
		{
			return vec3(0.0);
		}
		tMax = tTop;
	}
	else if (tTop > 0.0)
	{
		tMax = min(tTop, tBottom);
	}

	float SampleCount = SampleCountIni;
	float SampleCountFloor = SampleCountIni;
	float tMaxFloor = tMax;
	if (VariableSampleCount)
	{
		SampleCount = mix(u_raymarch_spp_min, u_raymarch_spp_max, saturate1(tMax * 0.01));
		SampleCountFloor = floor(SampleCount);
		tMaxFloor = tMax * SampleCountFloor / max(SampleCount, 1.0);
	}

	float dt = tMax / max(SampleCount, 1.0);
	float MiePhaseValue = CornetteShanksMiePhaseFunction(u_mie_phase_g, -dot(SunDir, WorldDir));
	float RayleighPhaseValue = RayleighPhase(dot(SunDir, WorldDir));
	vec3 L = vec3(0.0);
	vec3 throughput = vec3(1.0);
	float t = 0.0;
	const float SampleSegmentT = 0.3;

	const int kMaxSamples = 128;
	for (int si = 0; si < kMaxSamples; ++si)
	{
		float s = float(si);
		if (s >= SampleCount) break;

		if (VariableSampleCount)
		{
			float t0 = s / max(SampleCountFloor, 1.0);
			float t1 = (s + 1.0) / max(SampleCountFloor, 1.0);
			t0 = t0 * t0;
			t1 = t1 * t1;
			t0 = tMaxFloor * t0;
			t1 = (t1 > 1.0) ? tMax : (tMaxFloor * t1);
			t = t0 + (t1 - t0) * SampleSegmentT;
			dt = t1 - t0;
		}
		else
		{
			float NewT = tMax * (s + SampleSegmentT) / max(SampleCount, 1.0);
			dt = NewT - t;
			t = NewT;
		}

		vec3 P = WorldPos + t * WorldDir;
		MediumSampleRGB medium = sampleMediumRGB(P);
		vec3 SampleOpticalDepth = medium.extinction * dt;
		vec3 SampleTransmittance = exp(-SampleOpticalDepth);

		float pHeight = length(P);
		vec3 UpVector = P / max(pHeight, 1e-6);
		float SunZenithCosAngle = dot(SunDir, UpVector);
		vec2 transUv;
		LutTransmittanceParamsToUv(pHeight, SunZenithCosAngle, transUv);
		vec3 TransmittanceToSun = texture(u_transmittance_lut, transUv).rgb;

		vec3 PhaseTimesScattering = MieRayPhase ?
			(medium.scatteringMie * MiePhaseValue + medium.scatteringRay * RayleighPhaseValue) :
			(medium.scattering * (1.0 / (4.0 * PI)));
		vec3 multiScatteredLuminance = GetMultipleScattering(P, SunZenithCosAngle);

		float tEarth = raySphereIntersectNearest(P, SunDir, earthO + PLANET_RADIUS_OFFSET * UpVector, u_bottom_radius);
		float earthShadow = (tEarth >= 0.0) ? 0.0 : 1.0;
		vec3 S = u_sun_illuminance * (earthShadow * TransmittanceToSun * PhaseTimesScattering + multiScatteredLuminance * medium.scattering);

		vec3 Sint = (S - S * SampleTransmittance) / max(medium.extinction, vec3(1e-4));
		L += throughput * Sint;
		throughput *= SampleTransmittance;
	}

	if (ground && tMax == tBottom && tBottom > 0.0)
	{
		vec3 P = WorldPos + tBottom * WorldDir;
		float pHeight = length(P);
		vec3 UpVector = P / max(pHeight, 1e-6);
		float SunZenithCosAngle = dot(SunDir, UpVector);
		vec2 transUv;
		LutTransmittanceParamsToUv(pHeight, SunZenithCosAngle, transUv);
		vec3 TransmittanceToSun = texture(u_transmittance_lut, transUv).rgb;
		float NdotL = max(dot(normalize(UpVector), normalize(SunDir)), 0.0);
		L += u_sun_illuminance * TransmittanceToSun * throughput * NdotL * u_ground_albedo / PI;
	}

	return L;
}

void main()
{
	vec2 pixPos = gl_FragCoord.xy;
	vec2 uv = pixPos / vec2(float(u_skyview_width), float(u_skyview_height));

	vec3 WorldPos = vec3(u_camera_offset.x, u_camera_offset.y, u_bottom_radius + u_camera_offset.z);
	float viewHeight = length(WorldPos);

	float viewZenithCosAngle = 0.0;
	float lightViewCosAngle = 0.0;
	UvToSkyViewLutParams(viewZenithCosAngle, lightViewCosAngle, viewHeight, uv);

	vec3 UpVector = WorldPos / max(viewHeight, 1e-6);
	vec3 inputSunDir = normalize(u_sun_direction);
	float sunZenithCosAngle = dot(UpVector, inputSunDir);
	vec3 SunDir = normalize(vec3(safeSqrt(1.0 - sunZenithCosAngle * sunZenithCosAngle), 0.0, sunZenithCosAngle));

	WorldPos = vec3(0.0, 0.0, viewHeight);
	float viewZenithSinAngle = safeSqrt(1.0 - viewZenithCosAngle * viewZenithCosAngle);
	vec3 WorldDir = vec3(
		viewZenithSinAngle * lightViewCosAngle,
		viewZenithSinAngle * safeSqrt(1.0 - lightViewCosAngle * lightViewCosAngle),
		viewZenithCosAngle);

	if (!MoveToTopAtmosphere(WorldPos, WorldDir, u_top_radius))
	{
		o_color = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	vec3 L = IntegrateScatteredLuminance(WorldPos, WorldDir, SunDir, false, 30.0, true, true);
	o_color = vec4(L, 1.0);
}
