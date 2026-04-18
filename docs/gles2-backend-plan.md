> **Note**: This is a rough plan. It has not been reviewed for correctness or completeness, and details will change as implementation progresses.
>
> **Progress**: Phase 1 complete (commit `2efe8e2`). Phase 2 complete (commit `8aa8146`). Phase 3 complete (commit `8aa8146`).

# OpenGL ES 2.0 Backend Port for dhewm3

## Context

dhewm3 currently renders exclusively via OpenGL 1.2 + ARB extensions (ARB assembly vertex/fragment programs, fixed-function pipeline fallbacks). The goal is to add an OpenGL ES 2.0 rendering backend to enable deployment on mobile platforms, WebGL, and modern embedded targets where desktop GL is unavailable. The architecture is designed to keep the existing ARB2 backend intact, adding GLES2 as a selectable parallel path.

## Current Architecture Summary

- **Function dispatch**: All GL calls go through `qgl`-prefixed function pointers declared in `renderer/qgl.h` and `renderer/qgl_proc.h` via macro expansion. These are loaded at runtime via `SDL_GL_GetProcAddress()` in `sys/glimp.cpp:1242`.
- **Shaders**: ARB assembly programs (`.vfp` files) loaded in `renderer/draw_arb2.cpp`. GLSL does not exist in the current codebase.
- **Backend enum**: `backEndName_t` in `renderer/tr_local.h:676` currently has only `BE_ARB2`.
- **Backend selection**: `idRenderSystemLocal::SetBackEndRenderer()` in `renderer/RenderSystem.cpp:523` checks `glConfig.allowARB2Path` and calls `R_ARB2_Init()`.
- **Context creation**: SDL2/SDL3 in `sys/glimp.cpp`. No profile or version hints are set today.
- **Stub pattern**: `sys/stub/stub_gl.cpp` provides no-op GL for dedicated server builds; same pattern will be used/referenced.
- **ImGui**: Uses `imgui_impl_opengl2.cpp` (fixed-function GL); needs replacement for GLES2.

## Implementation Plan

This is a large project structured into 7 phases. Each phase is independently buildable and testable. The ARB2 backend must remain functional throughout.

---

### Phase 1: Build System & CMake Infrastructure ✅ COMPLETE (commit `2efe8e2`)

**Goal**: Add a compile-time switch that selects between ARB2 and GLES2 backends without breaking the existing build.

**Completed**:
- ✅ `option(GLES2 ...)` added to `neo/CMakeLists.txt`; conditionally selects `draw_gles2.cpp` over `draw_arb2.cpp` and adds `-DGLES2_BACKEND` define
- ✅ `gles2-debug` and `gles2-release` presets added to `neo/CMakePresets.json`
- ✅ `neo/renderer/draw_gles2.cpp` stub created with stub symbols matching existing ARB2 call sites
- ✅ ImGui disabled for GLES2 builds (guarded in CMake; proper support deferred to Phase 7)
- ✅ `neo/sys/gles2_shim/GLES2/gl2.h` added for platforms lacking system GLES2 headers (macOS desktop); redirects to SDL's bundled definitions
- ✅ No `libGLESv2` linkage — all proc resolution via `SDL_GL_GetProcAddress` instead (better than originally planned)

**Also pulled forward from Phase 2**:
- ✅ `neo/renderer/qgl_proc_es2.h` — full GLES2 core proc list using the `QGLPROC` macro pattern
- ✅ `neo/renderer/qgl.h` — GLES2 section added; `qgl_proc_es2.h` declarations included under `#ifdef GLES2_BACKEND`
- ✅ `neo/renderer/RenderSystem_init.cpp` — proc loading split: `qgl_proc.h` loaded silently (desktop-only nulls OK on GLES2), `qgl_proc_es2.h` loaded with fatal checks
- ✅ `neo/sys/glimp.cpp` — `SDL_GL_CONTEXT_PROFILE_ES` + version 2.0 set when `GLES2_BACKEND` defined

**Verification**: Both `cmake --preset debug` (ARB2) and `cmake --preset gles2-debug` build cleanly.

---

### Phase 2: GL Context & Function Pointer Layer ✅ COMPLETE (commit `8aa8146`)

**Goal**: Create GLES2-aware context initialization, capability detection, and backend selection.

**Completed**:
- ✅ `neo/renderer/qgl_proc_es2.h` created (pulled into Phase 1)
- ✅ `neo/renderer/qgl.h` GLES2 section added (pulled into Phase 1)
- ✅ `neo/renderer/RenderSystem_init.cpp` proc loading split (pulled into Phase 1)
- ✅ `neo/sys/glimp.cpp` ES context profile attributes (pulled into Phase 1)
- ✅ `neo/renderer/RenderSystem.h` — `glconfig_t` extended with `isGLES2`, `oes_packed_depth_stencil`, `oes_depth_texture`, `ext_texture_compression_s3tc`
- ✅ `neo/renderer/RenderSystem_init.cpp` — `R_CheckPortableExtensions()` detects OES/EXT extensions and sets `glConfig.isGLES2 = true`
- ✅ `neo/renderer/tr_local.h` — `BE_GLES2` added to `backEndName_t`
- ✅ `neo/renderer/RenderSystem.cpp` — `SetBackEndRenderer()` routes to `BE_GLES2` when `glConfig.isGLES2` is set (primary + fallback + switch case)

**Verification**: Application starts and creates a GLES2 context; `glGetString(GL_VERSION)` returns an ES string. Blank screen is acceptable.

---

### Phase 3: Runtime ARB→GLSL ES Translator ✅ COMPLETE (commit `8aa8146`)

**Goal**: Translate ARB assembly programs to GLSL ES 1.00 at runtime so the existing `.vfp` game data is reused directly. No hand-written GLSL shader files.

**Completed**:
- ✅ `neo/renderer/arb2glsl.h` — public interface: `ARB2GLSL_Translate(arbSrc, isFragment, outGLSL)`
- ✅ `neo/renderer/arb2glsl.cpp` — self-contained two-pass translator, no external dependencies:
  - First pass collects `TEMP` registers, `PARAM` literals/aliases, vertex attribute usage, varying outputs, and `texture[N]` samplers
  - Second pass translates instructions one-to-one: `MOV ADD SUB MUL MAD DP3 DP4 DPH RCP RSQ POW EXP LOG MIN MAX ABS FRC FLR CMP LRP SLT SGE XPD TEX TXP` plus `_SAT` variants (wrapped in `clamp()`)
  - Bare numeric literals wrapped in `vec4()` to satisfy GLSL ES 1.00 type rules
  - `OPTION ARB_position_invariant` emits `uMVP` uniform + `gl_Position = uMVP * aPosition;` epilogue
  - `PARAM name = {a,b,c,d}` → `const vec4`; `PARAM name = program.env[N]` → alias to `uProgramEnv[N]`
  - Swizzles and write masks pass through verbatim (identical syntax in GLSL)
- ✅ `neo/renderer/draw_gles2.cpp` — full program management replacing Phase 1 stubs:
  - `ApplyGammaHack()` — reproduces `draw_arb2.cpp:574` logic; translator receives already-patched ARB text so `dhewm3tmpres` is treated as a plain TEMP
  - `R_GLES2_CompileGLSL` / `R_GLES2_LinkProgram` with full GLSL compiler/linker error logging
  - `R_GLES2_InitPrograms()` — loads all `.vfp` files, applies gamma hack, translates, compiles, pairs vertex+fragment by filename, links with fixed attribute locations, resolves `uProgramEnv` / `uMVP` / `uTexN` uniform locations
  - `R_ReloadARBPrograms_f` — deletes and rebuilds all linked programs
  - `GLES2_SetProgramEnv()` — CPU-side mirror of `program.env[]` for Phase 4 upload
  - Fixed attribute locations bound before link: `aPosition`=0, `aTexCoord0`=1, `aTexCoord1`=2, `aNormal`=3, `aTangent`=4, `aBitangent`=5, `aColor`=6
- ✅ `neo/CMakeLists.txt` — `arb2glsl.cpp` added to GLES2 source list

**Verification**: Both `cmake -DGLES2=ON` and default ARB2 builds pass cleanly. All `.vfp` shaders translate and compile without GLSL errors on startup; errors are logged with full GLSL source via `glGetShaderInfoLog`.

---

### Phase 4: Vertex Array & Draw Call Layer

**Goal**: Replace ARB-style vertex attribute calls and fixed-function client state with GLES2 VAO-less attribute setup.

**Context**: GLES2 has no `glVertexPointer`, `glTexCoordPointer`, `glClientActiveTexture` (fixed-function client state is gone). It uses `glVertexAttribPointer` + `glEnableVertexAttribArray`. GLES2 also has no VAOs in core (only via `OES_vertex_array_object` extension).

**Files to modify**:
- `neo/renderer/draw_common.cpp` — in `#ifdef GLES2_BACKEND` blocks, replace `RB_T_FillDepthBuffer`, `RB_SetupForFastPath`, etc. to use `glVertexAttribPointer` instead of `glVertexPointer`/`glTexCoordPointer`
- `neo/renderer/VertexCache.cpp` — VBO management already uses `qglBindBufferARB`; add `#ifdef GLES2_BACKEND` paths that use core GLES2 `glBindBuffer` / `glGenBuffers`
- `neo/renderer/tr_backend.cpp` — `RB_SetDefaultGLState()` uses `glEnableClientState(GL_VERTEX_ARRAY)` etc.; guard with `#ifndef GLES2_BACKEND`, add GLES2 replacement that sets up attribute arrays
- `neo/renderer/draw_gles2.cpp` — implement `RB_GLES2_DrawInteraction()`, `RB_GLES2_DrawAmbient()`, `RB_GLES2_FillDepthBuffer()`, routing to the translated programs from Phase 3

**Attribute binding convention** (standardize across all shaders):
- attrib 0: position (`aPosition`)
- attrib 1: texcoord0 (`aTexCoord0`)
- attrib 2: normal (`aNormal`)
- attrib 3: tangent (`aTangent`)
- attrib 4: bitangent (`aBitangent`)
- attrib 5: color (`aColor`)

**Verification**: Depth-only pass renders (geometry visible in stencil buffer); some surfaces draw with correct geometry.

---

### Phase 5: Texture System Adaptation

**Goal**: Handle GLES2 texture format constraints.

**Key issues**:
- S3TC (DXT) compression: only available via `GL_EXT_texture_compression_s3tc` (OES variant); must fall back to uncompressed RGBA if absent
- BPTC (BC7): not available on GLES2; always fall back
- 3D textures: not in GLES2 core; `GL_OES_texture_3D` if needed, or eliminate usage
- NPOT textures: GLES2 supports NPOT but restricts mipmap filtering (must use `GL_CLAMP_TO_EDGE` + no mipmaps, or `GL_OES_texture_npot`)
- `glTexGen`: removed in GLES2 — texture coordinate generation must move to vertex shaders

**Files to modify**:
- `neo/renderer/Image_load.cpp` — in `SelectInternalFormat()`, add `#ifdef GLES2_BACKEND` block: skip BPTC/S3TC selection unless OES extension present; use `GL_RGBA` / `GL_RGB` as fallback; check `glConfig.oes_packed_depth_stencil` for depth textures
- `neo/renderer/Image_load.cpp` — in `GenerateImage()`, add mipmap parameter clamping for NPOT on GLES2
- `neo/renderer/draw_common.cpp` — remove `qglTexGenfv` calls under `GLES2_BACKEND`; move texture matrix / coordinate generation into shader uniforms (pass as `uTextureMatrix` uniforms in interaction/environment shaders)
- `neo/renderer/Image_load.cpp` — guard `qglTexImage3D` calls with `#ifndef GLES2_BACKEND`

**Verification**: All textures upload without GL errors; surfaces display with correct texturing.

---

### Phase 6: Stencil Shadow Volumes on GLES2

**Goal**: Implement Carmack's Reverse shadow volumes without `GL_EXT_stencil_two_side` or depth bounds test.

**Context**: GLES2 core has `glStencilFuncSeparate` and `glStencilOpSeparate` (both promoted to core in GLES2 spec). The `GL_EXT_depth_bounds_test` is not available.

**Files to modify**:
- `neo/renderer/draw_common.cpp` — `RB_StencilShadowPass()`: under `#ifdef GLES2_BACKEND`:
  - Remove `qglActiveStencilFaceEXT` / `qglDepthBoundsEXT` calls
  - Use `glStencilFuncSeparate(GL_FRONT, ...)` + `glStencilOpSeparate(GL_BACK, ...)` (these are core GLES2)
  - Bind the translated shadow vertex program (from Phase 3) instead of ARB vertex program
  - Disable depth bounds optimization (accept minor performance regression)
- `neo/renderer/draw_gles2.cpp` — implement `RB_GLES2_StencilShadowPass()`

**Note**: `GL_INCR_WRAP` and `GL_DECR_WRAP` are core in GLES2. The shadow volume approach itself works; only the extension mechanism differs.

**Verification**: Shadows render correctly in lit scenes.

---

### Phase 7: Remaining Fixed-Function Removal & Polish

**Goal**: Eliminate all remaining desktop-GL-only calls from the GLES2 path.

**Items**:
- `glMatrixMode` / `glLoadMatrix` / `glOrtho` / `glFrustum`: these appear in `tr_backend.cpp` and `draw_common.cpp`. Under GLES2: compute matrices in C++ (already have `idMath`, `idVec4`, `idMat4`) and pass as shader uniforms (`uModelViewProjection`, `uTextureMatrix`)
- `glColor4f` / `glColor4ub`: pass as vertex attribute or uniform `uColor` in the color shader
- `glPolygonOffset`: available in GLES2 core — no change needed
- `glScissor`, `glViewport`, `glDepthRange`: available in GLES2 core — no change needed
- `glLineWidth`: available but behavior may differ; acceptable
- `glReadPixels`: available in GLES2 (limited formats) — screenshot code may need format adjustment
- **ImGui**: swap to `imgui_impl_opengl3.cpp` compiled with `IMGUI_IMPL_OPENGL_ES2` define; add `#define IMGUI_IMPL_OPENGL_ES2` before include in the GLES2 CMake path
- **Gamma/brightness**: dhewm3's gamma-in-shader hack patches the ARB text before it reaches the translator (`draw_arb2.cpp:574`); the translator handles the patched instructions automatically — no extra work needed

**Files to modify**:
- `neo/renderer/tr_backend.cpp`
- `neo/renderer/draw_common.cpp`
- `neo/renderer/RenderSystem_init.cpp`
- `neo/CMakeLists.txt` (ImGui backend swap)

**Verification**: Full scene renders without GL errors (`GL_NO_ERROR` each frame); all shadow, interaction, ambient, and GUI passes work.

---

## Critical Files Reference

| File | Role | Change Type |
|------|------|-------------|
| `neo/CMakeLists.txt` | Build orchestration | Add `GLES2` option, conditional source/lib |
| `neo/renderer/qgl.h` | GL dispatch declarations | Guard with `#ifndef GLES2_BACKEND` |
| `neo/renderer/qgl_proc.h` | GL function list | Guard, add ES2 variant |
| `neo/renderer/RenderSystem.h` | `glconfig_t` struct | Add GLES2 capability flags |
| `neo/renderer/RenderSystem.cpp:523` | Backend selection | Add `BE_GLES2` branch |
| `neo/renderer/RenderSystem_init.cpp:392` | Extension detection | Add GLES2 OES checks |
| `neo/renderer/draw_arb2.cpp` | ARB2 backend | No changes (guarded out) |
| `neo/renderer/draw_common.cpp` | Shared draw calls | Add `#ifdef GLES2_BACKEND` throughout |
| `neo/renderer/tr_backend.cpp` | GL state machine | Replace fixed-function client state |
| `neo/renderer/tr_local.h:676` | Backend enum | Add `BE_GLES2` |
| `neo/renderer/Image_load.cpp` | Texture uploads | Format fallbacks for GLES2 |
| `neo/renderer/VertexCache.cpp` | VBO management | Core GLES2 calls |
| `neo/sys/glimp.cpp:182` | Context creation | Add ES profile attributes |
| **New**: `neo/renderer/draw_gles2.cpp` | GLES2 backend impl | New file |
| **New**: `neo/renderer/arb2glsl.cpp` / `.h` | Runtime ARB→GLSL ES translator | New file; no hand-written shaders |

## Existing Utilities to Reuse

- `idMat4`, `idVec3`, `idVec4` in `idlib/` — use for CPU-side matrix computation replacing `glMatrixMode`
- `R_CheckExtension()` at `RenderSystem_init.cpp:376` — reuse for OES extension checking
- `GLimp_ExtensionPointer()` at `sys/glimp.cpp:1242` — already uses `SDL_GL_GetProcAddress`, works unchanged for GLES2
- `glConfig_t` flags — extend rather than replace
- `VertexCache` VBO infrastructure — already abstracted enough to adapt

## Verification Strategy

1. **Unit**: Each phase compiles with both `GLES2=OFF` (existing ARB2 path unchanged) and `GLES2=ON`
2. **Shader validation**: Translator output is compiled against the GLES2 context on startup; abort with error log (including translated GLSL source) on failure
3. **Runtime**: Run the game demo with `GLES2=ON` on a platform with GLES2 support (iOS simulator, Android emulator, or desktop via ANGLE)
4. **Regression**: ARB2 path must pass its existing CI builds (macOS, Linux, Windows) unmodified
5. **Progressive**: After each phase, the game should start and show incrementally more correct output (blank → geometry → textured → lit → shadowed)

## Estimated Scope

~2,500–3,500 lines of new code (translator + draw_gles2.cpp + CMake) plus ~500 lines of guarded modifications to existing files. Eliminating hand-written GLSL reduces scope significantly and makes the shader path future-proof for mods. This is a multi-week project best executed phase by phase with the ARB2 backend as a reference for correctness.
