# OpenGL Port TODO (Hillaire Path Only)

This roadmap covers what is needed to better implement the OpenGL port as a full-featured version of the current Hillaire raymarching path.

Out of scope:
- Bruneton 2017 comparison path
- Path-traced rendering path

## 1. Core Rendering Parity (Highest Priority)

1. Add shadow map support to OpenGL.
- Implement a depth-only shadow pass for terrain.
- Feed shadow texture into terrain and sky/aerial shaders.
- Match current D3D11 behavior where terrain and atmosphere can be shadowed by sun direction.
- Touch points:
  - `Application/GameGl.cpp` (new shadow resource creation + pass order)
  - `Application/GameGl.h` (shadow textures/program IDs)
  - `Resources/glsl/terrain.vert`
  - `Resources/glsl/terrain.frag`
  - `Resources/glsl/render_raymarching_hillaire.frag`

2. Tighten terrain + atmosphere compositing correctness.
- Verify linear depth convention and units end-to-end (km vs world units).
- Ensure aerial perspective compositing over opaque geometry behaves consistently across camera ranges.
- Add edge-case handling for near-ground camera, horizon rays, and terrain intersection transitions.
- Touch points:
  - `Resources/glsl/terrain.frag`
  - `Resources/glsl/render_raymarching_hillaire.frag`
  - `Resources/glsl/aerial_perspective_volume.comp`

3. Remove unused present path or make it a real debug mode.
- `mPresentProgram` is compiled/validated but currently not used in frame rendering.
- Either:
  - delete it and simplify init failure conditions, or
  - expose a runtime toggle to render via `render_sky_from_lut.frag` for debugging.
- Touch points:
  - `Application/GameGl.cpp`
  - `Application/GameGl.h`
  - `Application/WinMainGlfw.cpp`

## 2. Runtime Feature Completeness

1. Implement screenshot capture for OpenGL (`C` key parity).
- Save HDR framebuffer to EXR (or at minimum LDR PNG + optional HDR path).
- Include optional "atmosphere-only" capture mode if desired.
- Touch points:
  - `Application/WinMainGlfw.cpp`
  - `Application/GameGl.cpp`

2. Implement state save/load (`F5`/`F9` parity).
- Persist atmosphere parameters and camera/sun settings.
- Reuse the same approach as DX11 (`state.txt`) or JSON.
- Reload should dirty LUTs and dependent resources.
- Touch points:
  - `Application/GameGl.cpp`
  - `Application/GameGl.h`
  - `Application/WinMainGlfw.cpp`

3. Implement shader hot-reload (`Ctrl+S` parity).
- Recompile/link GLSL programs without restarting.
- Keep old programs alive on compile failure (do not break current frame).
- Touch points:
  - `Application/WinMainGlfw.cpp`
  - `Application/GameGl.cpp`

4. Wire `mUseAerialPerspectiveDebug` to actual render behavior.
- It exists but currently has no effect.
- Add a mode to visualize AP slices/depth and validate blending.
- Touch points:
  - `Application/GameGl.h`
  - `Application/GameGl.cpp`
  - `Resources/glsl/render_raymarching_hillaire.frag`

## 3. Performance and Stability

1. Reduce expensive CPU readbacks in normal frames.
- `glGetTexImage` for LUT/volume min-max is expensive and can stall.
- Move debug stats behind a toggle or run at low frequency.
- Optionally compute stats on GPU.
- Touch points:
  - `Application/GameGl.cpp`

2. Cache uniform locations and avoid per-frame `glGetUniformLocation`.
- Uniform lookups currently happen repeatedly in hot paths (`uploadAtmosphereUniforms`, terrain, present).
- Cache per-program uniform locations after link and reuse on each draw/dispatch.
- Keep fallback behavior for missing uniforms (`-1`) to preserve shader flexibility.
- Touch points:
  - `Application/GameGl.cpp`
  - `Application/GameGl.h`

3. Avoid rebuilding shadow map every frame when not needed.
- Track dirty state for sun direction / terrain parameters that affect shadows.
- Re-render shadow map only when required (or at fixed cadence while camera-only moves).
- Touch points:
  - `Application/GameGl.cpp`
  - `Application/GameGl.h`

4. Add GL debug output and pass-level error diagnostics.
- Enable `GL_KHR_debug` callback in debug builds.
- Tag objects/passes for easier GPU debugging.
- Touch points:
  - `Application/WinMainGlfw.cpp`
  - `Application/GameGl.cpp`

5. Reduce terrain pass cost (high-vertex shader texture sampling).
- `terrain.vert` currently performs multiple heightmap samples per vertex for smoothing + normal estimation.
- Add quality tiers: cheaper normal approximation, reduced terrain resolution, or optional lower-cost shading path.
- Longer-term: move terrain generation/normal prep to precompute/compute path and consume compact buffers.
- Touch points:
  - `Resources/glsl/terrain.vert`
  - `Application/GameGl.cpp`
  - `Application/WinMainGlfw.cpp`

6. Add runtime scalability knobs for heavy resources.
- Expose shadow map resolution (`mShadowMapSize`) and aerial perspective volume dimensions in UI/config.
- Use presets (low/medium/high) to quickly trade quality for frame time.
- Touch points:
  - `Application/GameGl.h`
  - `Application/GameGl.cpp`
  - `Application/WinMainGlfw.cpp`

7. Harden resize/resource recreation.
- Check all create/destroy paths for failure handling.
- Ensure no stale bindings across resize and shutdown.
- Touch points:
  - `Application/GameGl.cpp`

## 4. Build and Project Hygiene

1. Add all runtime GLSL files to the VS project for discoverability.
- Currently some shaders used by `GameGl` are not listed in `.vcxproj`.
- Add at least:
  - `Resources/glsl/terrain.vert`
  - `Resources/glsl/terrain.frag`
  - `Resources/glsl/new_multi_scattering_lut.comp`
  - `Resources/glsl/aerial_perspective_volume.comp`
- Touch points:
  - `Application/Application.vcxproj`
  - `Application/Application.vcxproj.filters`

2. Add a small "OpenGL Known Limitations" section to main README.
- Document current required GL version/features.
- Document which debug/runtime controls are implemented.
- Touch points:
  - `README.md`

## 5. Validation Checklist (Done Criteria)

1. Visual checks:
- Terrain receives directional shadowing from sun.
- Fast sky on/off and fast AP on/off are visually stable.
- No popping at atmosphere entry/exit or horizon.

2. Functional checks:
- Screenshot, save/load, and shader hot-reload all work.
- Resize and fullscreen/windowed changes do not leak or break rendering.

3. Performance checks:
- Debug readbacks disabled by default.
- Frame time stable during camera movement and sun animation.
