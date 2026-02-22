#version 430 core

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;

uniform sampler2D u_heightmap_tex;
uniform mat4 u_view_proj;
uniform int u_terrain_resolution;

const float TERRAIN_WIDTH = 100.0;
const float MAX_TERRAIN_HEIGHT = 100.0;

float sampleTerrainHeight(vec2 uv)
{
	vec2 clampedUv = clamp(uv, vec2(0.0), vec2(1.0));
	const float offset = 0.0008;
	float heightAccum = texture(u_heightmap_tex, clampedUv + vec2(0.0, 0.0)).r;
	heightAccum += texture(u_heightmap_tex, clamp(clampedUv + vec2( offset, 0.0), vec2(0.0), vec2(1.0))).r;
	heightAccum += texture(u_heightmap_tex, clamp(clampedUv + vec2(-offset, 0.0), vec2(0.0), vec2(1.0))).r;
	heightAccum += texture(u_heightmap_tex, clamp(clampedUv + vec2(0.0,  offset), vec2(0.0), vec2(1.0))).r;
	heightAccum += texture(u_heightmap_tex, clamp(clampedUv + vec2(0.0, -offset), vec2(0.0), vec2(1.0))).r;
	return heightAccum / 5.0;
}

vec3 sampleTerrainWorld(float quadx, float quady, vec2 qp)
{
	float resolution = float(u_terrain_resolution);
	float quadWidth = TERRAIN_WIDTH / resolution;
	vec2 uv = (vec2(quadx, quady) + qp) / resolution;
	float h = sampleTerrainHeight(uv);
	vec3 worldPos = (vec3(quadx, quady, MAX_TERRAIN_HEIGHT * h) + vec3(qp, 0.0)) * quadWidth - 0.5 * vec3(TERRAIN_WIDTH, TERRAIN_WIDTH, 0.0);
	worldPos += vec3(-TERRAIN_WIDTH * 0.45, 0.4 * TERRAIN_WIDTH, 0.0);
	return worldPos;
}

void main()
{
	uint vertId = uint(gl_VertexID);
	uint instanceId = uint(gl_InstanceID);

	vec2 qp = vec2(0.0);
	if (vertId == 1u || vertId == 4u)
	{
		qp = vec2(1.0, 0.0);
	}
	else if (vertId == 2u || vertId == 3u)
	{
		qp = vec2(0.0, 1.0);
	}
	else if (vertId == 5u)
	{
		qp = vec2(1.0, 1.0);
	}

	uint terrainRes = uint(max(u_terrain_resolution, 1));
	float quadx = float(instanceId / terrainRes);
	float quady = float(instanceId % terrainRes);

	vec3 worldPos = sampleTerrainWorld(quadx, quady, qp);
	v_world_pos = worldPos;

	const float normalOffset = 5.0;
	vec3 worldPos0_ = sampleTerrainWorld(quadx + qp.x - normalOffset, quady + qp.y, vec2(0.0));
	vec3 worldPos1_ = sampleTerrainWorld(quadx + qp.x + normalOffset, quady + qp.y, vec2(0.0));
	vec3 worldPos_0 = sampleTerrainWorld(quadx + qp.x, quady + qp.y - normalOffset, vec2(0.0));
	vec3 worldPos_1 = sampleTerrainWorld(quadx + qp.x, quady + qp.y + normalOffset, vec2(0.0));
	v_normal = normalize(cross(normalize(worldPos1_ - worldPos0_), normalize(worldPos_1 - worldPos_0)));

	gl_Position = u_view_proj * vec4(worldPos, 1.0);
}
