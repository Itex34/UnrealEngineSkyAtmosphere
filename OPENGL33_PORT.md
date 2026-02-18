# Porting This Sky Atmosphere to OpenGL 3.3

This repository has two atmosphere paths:

- LUT precompute + LUT render (`Application/RenderWithLuts.cpp`)
- New ray-marching path using a compute shader (`NewMultiScattCS`)

For an OpenGL 3.3 voxel engine, port the LUT path first. It matches the Hillaire/Bruneton pipeline and avoids compute-shader requirements.

## 1) Use This Path

DX11 flow to mirror:

1. `generateSkyAtmosphereLUTs()` in `Application/RenderWithLuts.cpp:12`
2. `generateSkyAtmosphereCameraVolumes()` in `Application/RenderWithLuts.cpp:270`
3. `renderSkyAtmosphereUsingLUTs()` in `Application/RenderWithLuts.cpp:310`

Avoid porting `NewMultiScattCS` initially (`Resources/RenderSkyRayMarching.hlsl:418`) since GL 3.3 has no core compute shaders.

## 2) Keep the Same LUT Dimensions

From `Application/SkyAtmosphereCommon.h:27`:

- Transmittance: `256 x 64`
- Irradiance: `64 x 16`
- Scattering 3D:
  - `R = 32`
  - `MU = 128`
  - `MU_S = 32`
  - `NU = 8`
  - Width = `NU * MU_S`
  - Height = `MU`
  - Depth = `R`

## 3) Texture Formats (GL Equivalents)

Recommended:

- Transmittance LUT: `GL_RGBA16F` (or `GL_RGBA32F` for highest stability)
- Irradiance LUT: `GL_RGBA16F` (or `GL_RGBA32F`)
- Scattering LUT (3D): `GL_RGBA16F` (or `GL_RGBA32F`)
- Camera scattering volume (3D): `GL_RGBA16F`
- Camera transmittance volume (3D): `GL_R11F_G11F_B10F` or `GL_RGBA16F`

The original code can run in 16F but comments note fewer artifacts in 32F (`Application/SkyAtmosphereCommon.cpp`).

## 4) Shader Stage Mapping

Reuse shader logic in:

- `Resources/Bruneton17/functions.glsl`
- `Resources/Bruneton17/definitions.glsl`

Port pass entry points from:

- `Resources/SkyAtmosphereTransmittanceLut.hlsl`
- `Resources/SkyAtmosphereDirectIrradianceLut.hlsl`
- `Resources/SkyAtmosphereSingleScatteringLut.hlsl`
- `Resources/SkyAtmosphereScatteringDensity.hlsl`
- `Resources/SkyAtmosphereIndirectIrradiance.hlsl`
- `Resources/SkyAtmosphereMultipleScattering.hlsl`
- `Resources/RenderWithLuts.hlsl`

## 5) 3D LUT Rendering in GL 3.3

DX11 writes 3D textures using a geometry shader (`Resources/Common.hlsl:90`).

In GL 3.3, do the same:

- Full-screen triangle VS
- GS sets `gl_Layer = instanceID`
- Fragment shader computes one slice
- Draw with `glDrawArraysInstanced(GL_TRIANGLES, 0, 3, depth)`

Attach layered 3D texture using `glFramebufferTexture`.

## 6) Blend Behavior You Must Reproduce

Some passes need additive blend only on MRT target 1 in DX11 (`Application/Game.cpp:386`).

In GL:

- If `ARB_draw_buffers_blend` is available: use `glBlendFunci/glBlendEquationi`
- If not: split into two passes and avoid per-target blend dependency

Also replicate premultiplied blending used when compositing sky over scene (`BlendPreMutlAlpha`, `BlendLuminanceTransmittance` in `Application/RenderSky.cpp:227` and `Application/RenderSky.cpp:231`).

## 7) Voxel Renderer Integration

Per-frame:

1. Update atmosphere UBO (sun dir, camera, radii, densities)
2. If atmosphere params changed: regenerate LUTs
3. Generate camera volumes (or skip at first and sample LUTs directly)
4. During sky pass:
   - Draw sky where depth == far plane
5. During voxel shading:
   - Use depth/world position to apply aerial perspective
   - Blend in-scattering + transmittance over voxel color

## 8) Practical Advice

- Start without shadows, then add shadow-map sampling later.
- Start with LUT sky only, then add camera aerial perspective volume.
- Keep the exact transmittance/scattering parameterization; most visual mismatches come from coordinate mapping errors, not coefficients.
- Validate each pass by visualizing LUT textures directly.

## 9) Minimum Bring-Up Checklist

1. Full-screen triangle + UBOs working
2. Transmittance LUT pass correct
3. Direct irradiance pass correct
4. Single scattering 3D pass correct (layered render)
5. Multi-order loop (density -> indirect irradiance -> multiple scattering)
6. Final sky shading pass using LUTs
7. Aerial perspective over voxels
