#version 430 core

in vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_skyview_lut;
uniform vec3 u_sun_direction;
uniform float u_bottom_radius;
uniform float u_top_radius;
uniform float u_camera_height;
uniform float u_aspect;
uniform float u_fov_y_degrees;
uniform float u_sun_illuminance;

const float PI = 3.1415926535897932384626433832795;

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

void SkyViewLutParamsToUv(in bool IntersectGround, in float viewZenithCosAngle, in float lightViewCosAngle, in float viewHeight, out vec2 uv)
{
	float Vhorizon = sqrt(max(viewHeight * viewHeight - u_bottom_radius * u_bottom_radius, 0.0));
	float CosBeta = Vhorizon / max(viewHeight, 1e-6);
	float Beta = acos(CosBeta);
	float ZenithHorizonAngle = PI - Beta;

	if (!IntersectGround)
	{
		float coord = acos(clamp(viewZenithCosAngle, -1.0, 1.0)) / max(ZenithHorizonAngle, 1e-6);
		coord = 1.0 - coord;
		coord = sqrt(max(coord, 0.0));
		coord = 1.0 - coord;
		uv.y = coord * 0.5;
	}
	else
	{
		float coord = (acos(clamp(viewZenithCosAngle, -1.0, 1.0)) - ZenithHorizonAngle) / max(Beta, 1e-6);
		coord = sqrt(max(coord, 0.0));
		uv.y = coord * 0.5 + 0.5;
	}

	{
		float coord = -lightViewCosAngle * 0.5 + 0.5;
		coord = sqrt(max(coord, 0.0));
		uv.x = coord;
	}

	uv = vec2(fromUnitToSubUvs(uv.x, 192.0), fromUnitToSubUvs(uv.y, 108.0));
}

vec3 GetSunLuminance(vec3 WorldPos, vec3 WorldDir)
{
	float sunCos = cos(0.5 * 0.505 * PI / 180.0);
	if (dot(WorldDir, normalize(u_sun_direction)) > sunCos)
	{
		float t = raySphereIntersectNearest(WorldPos, WorldDir, vec3(0.0), u_bottom_radius);
		if (t < 0.0)
		{
			return vec3(u_sun_illuminance);
		}
	}
	return vec3(0.0);
}

void main()
{
	// OpenGL texture/frag-space is bottom-left origin; keep Y increasing upward here.
	vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, v_uv.y * 2.0 - 1.0);
	float tanHalfFov = tan(radians(u_fov_y_degrees) * 0.5);
	// Match the original sample convention: Z-up, camera forward along +Y.
	vec3 WorldDir = normalize(vec3(ndc.x * u_aspect * tanHalfFov, 1.0, ndc.y * tanHalfFov));
	vec3 WorldPos = vec3(0.0, 0.0, u_bottom_radius + u_camera_height);

	float viewHeight = length(WorldPos);
	vec3 UpVector = normalize(WorldPos);
	float viewZenithCosAngle = dot(WorldDir, UpVector);

	vec3 sideVector = cross(UpVector, WorldDir);
	if (length(sideVector) < 1e-5)
	{
		sideVector = vec3(1.0, 0.0, 0.0);
	}
	else
	{
		sideVector = normalize(sideVector);
	}
	vec3 forwardVector = normalize(cross(sideVector, UpVector));
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

	bool intersectGround = raySphereIntersectNearest(WorldPos, WorldDir, vec3(0.0), u_bottom_radius) >= 0.0;
	vec2 skyUv;
	SkyViewLutParamsToUv(intersectGround, viewZenithCosAngle, lightViewCosAngle, viewHeight, skyUv);

	vec3 color = texture(u_skyview_lut, skyUv).rgb + GetSunLuminance(WorldPos, WorldDir);
	const vec3 white_point = vec3(1.08241, 0.96756, 0.95003);
	const float exposure = 10.0;
	color = vec3(1.0) - exp(-max(color, vec3(0.0)) / white_point * exposure);
	color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
	o_color = vec4(color, 1.0);
}
