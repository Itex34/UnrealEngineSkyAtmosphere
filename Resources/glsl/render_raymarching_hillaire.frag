#version 430 core

in vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_transmittance_lut;
uniform sampler2D u_skyview_lut;
uniform sampler2D u_multiscattering_lut;
uniform sampler3D u_aerial_perspective_volume;
uniform sampler2D u_scene_color;
uniform sampler2D u_scene_linear_depth;
uniform sampler2DShadow u_shadowmap_tex;

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
uniform float u_camera_height;
uniform vec3 u_camera_offset;
uniform float u_raymarch_spp_min;
uniform float u_raymarch_spp_max;
uniform float u_sun_illuminance;
uniform float u_aspect;
uniform float u_fov_y_degrees;
uniform vec3 u_view_forward;
uniform vec3 u_view_right;
uniform vec3 u_view_up;
uniform mat4 u_shadow_view_proj;
uniform int u_multi_scattering_lut_res;
uniform int u_fast_sky;
uniform int u_fast_aerial_perspective;
uniform int u_colored_transmittance;
uniform float u_ap_debug_depth_km;

const float PI = 3.1415926535897932384626433832795;
const float PLANET_RADIUS_OFFSET = 0.01;
const float AP_SLICE_COUNT = 32.0;
const float AP_KM_PER_SLICE = 4.0;

float saturate1(float v) { return clamp(v, 0.0, 1.0); }
float safeSqrt(float v) { return sqrt(max(v, 0.0)); }

float fromUnitToSubUvs(float u, float resolution)
{
	return (u + 0.5 / resolution) * (resolution / (resolution + 1.0));
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
	if (sol0 < 0.0 && sol1 < 0.0) return -1.0;
	if (sol0 < 0.0) return max(0.0, sol1);
	if (sol1 < 0.0) return max(0.0, sol0);
	return max(0.0, min(sol0, sol1));
}

bool MoveToTopAtmosphere(inout vec3 worldPos, in vec3 worldDir)
{
	float viewHeight = length(worldPos);
	if (viewHeight > u_top_radius)
	{
		float tTop = raySphereIntersectNearest(worldPos, worldDir, vec3(0.0), u_top_radius);
		if (tTop >= 0.0)
		{
			vec3 upVector = worldPos / viewHeight;
			vec3 upOffset = upVector * -PLANET_RADIUS_OFFSET;
			worldPos = worldPos + worldDir * tTop + upOffset;
		}
		else
		{
			return false;
		}
	}
	return true;
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

float getTerrainShadow(in vec3 atmospherePos)
{
	vec3 terrainWorldPos = atmospherePos + vec3(0.0, 0.0, -u_bottom_radius);
	vec4 shadowClip = u_shadow_view_proj * vec4(terrainWorldPos, 1.0);
	vec3 shadowNdc = shadowClip.xyz / max(shadowClip.w, 1e-6);
	vec2 shadowUv = vec2(shadowNdc.x * 0.5 + 0.5, shadowNdc.y * 0.5 + 0.5);
	float shadowDepth = shadowNdc.z * 0.5 + 0.5;
	if (shadowUv.x < 0.0 || shadowUv.x >= 1.0 || shadowUv.y < 0.0 || shadowUv.y >= 1.0 || shadowDepth < 0.0 || shadowDepth >= 1.0)
	{
		return 1.0;
	}
	return texture(u_shadowmap_tex, vec3(shadowUv, shadowDepth));
}

vec3 IntegrateScatteredLuminance(in vec3 worldPos, in vec3 worldDir, in vec3 sunDir, in float tMaxLimit, out vec3 outTransmittance)
{
	outTransmittance = vec3(1.0);
	vec3 earthO = vec3(0.0);
	float tBottom = raySphereIntersectNearest(worldPos, worldDir, earthO, u_bottom_radius);
	float tTop = raySphereIntersectNearest(worldPos, worldDir, earthO, u_top_radius);
	float tMax = 0.0;
	if (tBottom < 0.0)
	{
		if (tTop < 0.0) return vec3(0.0);
		tMax = tTop;
	}
	else if (tTop > 0.0)
	{
		tMax = min(tTop, tBottom);
	}
	tMax = min(tMax, tMaxLimit);
	if (tMax <= 0.0)
	{
		return vec3(0.0);
	}

	float sampleCount = mix(u_raymarch_spp_min, u_raymarch_spp_max, saturate1(tMax * 0.01));
	float sampleCountFloor = floor(max(sampleCount, 1.0));
	float tMaxFloor = tMax * sampleCountFloor / max(sampleCount, 1.0);
	float dt = tMax / max(sampleCount, 1.0);

	float miePhaseValue = CornetteShanksMiePhaseFunction(u_mie_phase_g, -dot(sunDir, worldDir));
	float rayleighPhaseValue = RayleighPhase(dot(sunDir, worldDir));
	vec3 L = vec3(0.0);
	vec3 throughput = vec3(1.0);
	float t = 0.0;
	const float sampleSegmentT = 0.3;

	const int kMaxSamples = 128;
	for (int si = 0; si < kMaxSamples; ++si)
	{
		float s = float(si);
		if (s >= sampleCount) break;

		float t0 = s / max(sampleCountFloor, 1.0);
		float t1 = (s + 1.0) / max(sampleCountFloor, 1.0);
		t0 = t0 * t0;
		t1 = t1 * t1;
		t0 = tMaxFloor * t0;
		t1 = (t1 > 1.0) ? tMax : (tMaxFloor * t1);
		t = t0 + (t1 - t0) * sampleSegmentT;
		dt = t1 - t0;

		vec3 P = worldPos + t * worldDir;
		MediumSampleRGB medium = sampleMediumRGB(P);
		vec3 sampleOpticalDepth = medium.extinction * dt;
		vec3 sampleTransmittance = exp(-sampleOpticalDepth);

		float pHeight = length(P);
		vec3 upVector = P / max(pHeight, 1e-6);
		float sunZenithCosAngle = dot(sunDir, upVector);
		vec2 transUv;
		LutTransmittanceParamsToUv(pHeight, sunZenithCosAngle, transUv);
		vec3 transmittanceToSun = texture(u_transmittance_lut, transUv).rgb;

		vec3 phaseTimesScattering = medium.scatteringMie * miePhaseValue + medium.scatteringRay * rayleighPhaseValue;
		vec3 multiScatteredLuminance = GetMultipleScattering(P, sunZenithCosAngle);
		float tEarth = raySphereIntersectNearest(P, sunDir, earthO + PLANET_RADIUS_OFFSET * upVector, u_bottom_radius);
		float earthShadow = (tEarth >= 0.0) ? 0.0 : 1.0;
		float shadow = getTerrainShadow(P);
		vec3 S = u_sun_illuminance * (earthShadow * shadow * transmittanceToSun * phaseTimesScattering + multiScatteredLuminance * medium.scattering);

		vec3 Sint = (S - S * sampleTransmittance) / max(medium.extinction, vec3(1e-4));
		L += throughput * Sint;
		throughput *= sampleTransmittance;
	}

	outTransmittance = throughput;
	return L;
}

vec3 GetSunLuminance(in vec3 worldPos, in vec3 worldDir)
{
	float sunCos = cos(0.5 * 0.505 * PI / 180.0);
	if (dot(worldDir, normalize(u_sun_direction)) > sunCos)
	{
		float t = raySphereIntersectNearest(worldPos, worldDir, vec3(0.0), u_bottom_radius);
		if (t < 0.0)
		{
			return vec3(u_sun_illuminance);
		}
	}
	return vec3(0.0);
}

vec4 SampleAerialPerspectiveVolume(in vec2 uv, in float depthKm)
{
	float slice = depthKm / AP_KM_PER_SLICE;
	float weight = 1.0;
	if (slice < 0.5)
	{
		weight = saturate1(slice * 2.0);
		slice = 0.5;
	}
	float w = sqrt(clamp(slice / AP_SLICE_COUNT, 0.0, 1.0));
	return weight * texture(u_aerial_perspective_volume, vec3(uv, w));
}

void SkyViewLutParamsToUv(in bool intersectGround, in float viewZenithCosAngle, in float lightViewCosAngle, in float viewHeight, out vec2 uv)
{
	float Vhorizon = safeSqrt(viewHeight * viewHeight - u_bottom_radius * u_bottom_radius);
	float cosBeta = Vhorizon / max(viewHeight, 1e-6);
	float beta = acos(cosBeta);
	float zenithHorizonAngle = PI - beta;
	if (!intersectGround)
	{
		float coord = acos(clamp(viewZenithCosAngle, -1.0, 1.0)) / max(zenithHorizonAngle, 1e-6);
		coord = 1.0 - coord;
		coord = sqrt(max(coord, 0.0));
		coord = 1.0 - coord;
		uv.y = coord * 0.5;
	}
	else
	{
		float coord = (acos(clamp(viewZenithCosAngle, -1.0, 1.0)) - zenithHorizonAngle) / max(beta, 1e-6);
		coord = sqrt(max(coord, 0.0));
		uv.y = coord * 0.5 + 0.5;
	}
	float coord = -lightViewCosAngle * 0.5 + 0.5;
	coord = sqrt(max(coord, 0.0));
	uv.x = coord;
	uv = vec2(fromUnitToSubUvs(uv.x, 192.0), fromUnitToSubUvs(uv.y, 108.0));
}

void main()
{
	// Keep OpenGL Y as-is (no extra flip), do not reintroduce D3D screen inversion.
	vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, v_uv.y * 2.0 - 1.0);
	float tanHalfFov = tan(radians(u_fov_y_degrees) * 0.5);
	vec3 worldDir = normalize(
		u_view_right * (ndc.x * u_aspect * tanHalfFov) +
		u_view_forward +
		u_view_up * (ndc.y * tanHalfFov));
	vec3 worldPos = vec3(u_camera_offset.x, u_camera_offset.y, u_bottom_radius + u_camera_offset.z);
	float viewHeight = length(worldPos);
	vec3 sceneHdr = texture(u_scene_color, v_uv).rgb;
	float sceneDepthKm = texture(u_scene_linear_depth, v_uv).r;
	bool hasOpaque = sceneDepthKm > 0.0;
	float depthBufferValue = hasOpaque ? 0.0 : 1.0;

	vec3 L = vec3(0.0);
	vec3 sceneTransmittance = vec3(1.0);
	if (u_fast_sky != 0 && viewHeight < u_top_radius && !hasOpaque)
	{
		vec3 upVector = normalize(worldPos);
		float viewZenithCosAngle = dot(worldDir, upVector);
		vec3 sideVector = cross(upVector, worldDir);
		if (length(sideVector) < 1e-5)
		{
			sideVector = vec3(1.0, 0.0, 0.0);
		}
		else
		{
			sideVector = normalize(sideVector);
		}
		vec3 forwardVector = normalize(cross(sideVector, upVector));
		vec2 lightOnPlane = vec2(dot(normalize(u_sun_direction), forwardVector), dot(normalize(u_sun_direction), sideVector));
		if (length(lightOnPlane) < 1e-5)
		{
			lightOnPlane = vec2(1.0, 0.0);
		}
		else
		{
			lightOnPlane = normalize(lightOnPlane);
		}
		float lightViewCosAngle = lightOnPlane.x;
		bool intersectGround = raySphereIntersectNearest(worldPos, worldDir, vec3(0.0), u_bottom_radius) >= 0.0;
		vec2 skyUv;
		SkyViewLutParamsToUv(intersectGround, viewZenithCosAngle, lightViewCosAngle, viewHeight, skyUv);
		L = texture(u_skyview_lut, skyUv).rgb + GetSunLuminance(worldPos, worldDir);
	}
	else
	{
		if (depthBufferValue >= 0.9999)
		{
			L += GetSunLuminance(worldPos, worldDir);
		}

		if (u_fast_aerial_perspective != 0)
		{
			if (hasOpaque)
			{
				vec4 ap = SampleAerialPerspectiveVolume(v_uv, max(sceneDepthKm, 0.0));
				L += ap.rgb;
				float transmittance = clamp(1.0 - ap.a, 0.0, 1.0);
				sceneTransmittance = vec3(transmittance);
			}
			else
			{
				vec3 marchStart = worldPos;
				if (MoveToTopAtmosphere(marchStart, worldDir))
				{
					vec3 throughput = vec3(1.0);
					L += IntegrateScatteredLuminance(marchStart, worldDir, normalize(u_sun_direction), 9000000.0, throughput);
				}
				else
				{
					L += GetSunLuminance(worldPos, worldDir);
				}
			}
		}
		else
		{
			vec3 marchStart = worldPos;
			if (MoveToTopAtmosphere(marchStart, worldDir))
			{
				vec3 throughput = vec3(1.0);
				float depthLimit = hasOpaque ? sceneDepthKm : 9000000.0;
				L += IntegrateScatteredLuminance(marchStart, worldDir, normalize(u_sun_direction), depthLimit, throughput);
				if (hasOpaque)
				{
					if (u_colored_transmittance != 0)
					{
						sceneTransmittance = clamp(throughput, vec3(0.0), vec3(1.0));
					}
					else
					{
						float transmittance = clamp(dot(throughput, vec3(1.0 / 3.0)), 0.0, 1.0);
						sceneTransmittance = vec3(transmittance);
					}
				}
			}
			else
			{
				L += GetSunLuminance(worldPos, worldDir);
				if (hasOpaque)
				{
					sceneTransmittance = vec3(0.0);
				}
			}
		}
	}

	vec3 hdr = hasOpaque ? (sceneHdr * sceneTransmittance + L) : L;
	o_color = vec4(hdr, 1.0);
}
