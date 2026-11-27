# Third-party code built from pinned git submodules.

# Configure-time text patches of third-party sources: `var` holds a file's text, and each call replaces `from` with
# `to` in it, failing the configure if `from` is gone (the file changed upstream). The patched copy is written to the
# build directory and compiled instead of the original. (The strings contain semicolons, so they are passed quoted and
# never as CMake lists.)
function(ffcc_patch var file from to)
  string(FIND "${${var}}" "${from}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "${file} changed upstream: cannot apply our patch (looking for: ${from})")
  endif()
  string(REPLACE "${from}" "${to}" patched "${${var}}")
  set(${var} "${patched}" PARENT_SCOPE)
endfunction()

function(ffcc_write_patched var path)
  file(WRITE ${path}.tmp "${${var}}")
  configure_file(${path}.tmp ${path} COPYONLY)  # (rewritten only when it changes)
endfunction()

# ---- melonDS core (GPL-3.0-or-later) ----
# Built from our own source list rather than melonDS's CMake: we need the emulation core and its renderers only
# (no frontend, JIT or GDB stub), and the recompiled ARM9 replaces melonDS's ARM9 interpreter at run time.
# Nix (and other out-of-tree builds) can point at an unpacked copy of the same pinned commit.
set(MELONDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/melonDS" CACHE PATH "Path to the melonDS source tree (default: the git submodule)")
if(NOT EXISTS ${MELONDS_DIR}/src/NDS.cpp)
  message(FATAL_ERROR "melonDS is missing at ${MELONDS_DIR}: run `git submodule update --init --recursive` or set -DMELONDS_DIR=<path>")
endif()
set(MS ${MELONDS_DIR}/src)

# GPU3D.cpp: hooks that see every GX command the geometry engine executes and every VBlank (for frame interpolation,
# which records each scene's command stream and swaps in a blended polygon list), and a replay mode in which the
# engine can be fed commands without raising GX FIFO interrupts or DMA requests on the console. The runtime declares
# these in runtime/src/gx_hook.hpp.
file(READ ${MS}/GPU3D.cpp gpu3d_src)
string(REPLACE "\r\n" "\n" gpu3d_src "${gpu3d_src}")
ffcc_patch(gpu3d_src GPU3D.cpp "using Platform::LogLevel;\n"
  "using Platform::LogLevel;\n\n// ffcc patch\nint FfccWideExtra = 0;\nint FfccHudEdges = 0, FfccHudScale = 100;\nextern \"C\" int ffcc_wide_scene = 0; // set by the game while a scene uses the widened projection, counts down per VBlank\nextern \"C\" int ffcc_wide_block = 0;  // frames left in which the scene stays 4:3 (only orthographic geometry: the title)\nstatic int ffcc_ortho_verts = 0, ffcc_persp_verts = 0;\n#define FFCC_WIDE_ACTIVE (ffcc_wide_scene > 0 && ffcc_wide_block == 0)\nvoid (*FfccGxCommandHook)(void* context, u8 command, u32 param) = nullptr;\nvoid (*FfccGxVBlankHook)(void* context, bool flushed) = nullptr;\nvoid* FfccGxCommandHookContext = nullptr;\nbool FfccGxReplaying = false;\n")
ffcc_patch(gpu3d_src GPU3D.cpp "CmdFIFOEntry entry = CmdFIFORead();\n"
  "CmdFIFOEntry entry = CmdFIFORead();\n    if (FfccGxCommandHook && !FfccGxReplaying) FfccGxCommandHook(FfccGxCommandHookContext, entry.Command, entry.Param); // ffcc patch\n")
ffcc_patch(gpu3d_src GPU3D.cpp "void GPU3D::CheckFIFOIRQ() noexcept\n{\n"
  "void GPU3D::CheckFIFOIRQ() noexcept\n{\n    if (FfccGxReplaying) return; // ffcc patch\n")
ffcc_patch(gpu3d_src GPU3D.cpp "void GPU3D::CheckFIFODMA() noexcept\n{\n"
  "void GPU3D::CheckFIFODMA() noexcept\n{\n    if (FfccGxReplaying) return; // ffcc patch\n")
ffcc_patch(gpu3d_src GPU3D.cpp "void GPU3D::VBlank() noexcept\n{\n"
  "void GPU3D::VBlank() noexcept\n{\n    const bool ffcc_flushed = GeometryEnabled && RenderingEnabled && FlushRequest; // ffcc patch\n    if (ffcc_wide_scene > 0 && ffcc_flushed) ffcc_wide_scene--; // ffcc patch: counts scenes, not VBlanks, so a stall (a load) does not end it\n    if (ffcc_ortho_verts > 16 && ffcc_persp_verts == 0) ffcc_wide_block = 30; else if (ffcc_wide_block > 0) ffcc_wide_block--;\n    ffcc_ortho_verts = ffcc_persp_verts = 0;\n")
ffcc_patch(gpu3d_src GPU3D.cpp "            FlushRequest = 0;\n        }\n    }\n}\n"
  "            FlushRequest = 0;\n        }\n    }\n    if (FfccGxVBlankHook && !FfccGxReplaying) FfccGxVBlankHook(FfccGxCommandHookContext, ffcc_flushed); // ffcc patch\n}\n")
ffcc_patch(gpu3d_src GPU3D.cpp "            Viewport[4] = (Viewport[2] - Viewport[0] + 1) & 0x1FF;          // width\n"
  "            Viewport[4] = (Viewport[2] - Viewport[0] + 1) & 0x1FF;          // width\n            if (FfccWideExtra && Viewport[0] == 0 && Viewport[2] == 255) // ffcc patch: a widened scene fills the wide texture, any other (the title) sits in its middle\n            {\n                if (FFCC_WIDE_ACTIVE) Viewport[4] = 256 + 2 * FfccWideExtra;\n                else { Viewport[0] += FfccWideExtra; Viewport[2] += FfccWideExtra; }\n            }\n")
# An orthographic projection (no z in w) is not widened by the game: squeeze it back to 4:3 in the wide viewport, so it
# keeps its proportions. A frame with ortho geometry and no perspective at all (the title) stays 4:3 altogether, for
# a while (ffcc_wide_block), instead of widening a backdrop that has nothing for the sides.
ffcc_patch(gpu3d_src GPU3D.cpp "    vertextrans->Position[3] = (vertex[0]*ClipMatrix[3] + vertex[1]*ClipMatrix[7] + vertex[2]*ClipMatrix[11] + vertex[3]*ClipMatrix[15]) >> 12;\n"
  "    vertextrans->Position[3] = (vertex[0]*ClipMatrix[3] + vertex[1]*ClipMatrix[7] + vertex[2]*ClipMatrix[11] + vertex[3]*ClipMatrix[15]) >> 12;\n    if (FfccWideExtra && ProjMatrix[11] == 0 && ProjMatrix[15] == 0x1000) // ffcc patch\n    {\n        ffcc_ortho_verts++;\n        if (FFCC_WIDE_ACTIVE) vertextrans->Position[0] = (vertextrans->Position[0] * 256) / (256 + 2 * FfccWideExtra);\n    }\n    else ffcc_persp_verts++;\n")
# The vertices' X positions wrap at 9 bits (13 for the hi-res ones): a picture wider than 511 pixels needs more.
ffcc_patch(gpu3d_src GPU3D.cpp "vtx->FinalPosition[0] = posX & 0x1FF;"
  "vtx->FinalPosition[0] = posX & (FfccWideExtra ? 0x7FF : 0x1FF); // ffcc patch")
ffcc_patch(gpu3d_src GPU3D.cpp "vtx->HiresPosition[0] = posX & 0x1FFF;"
  "vtx->HiresPosition[0] = posX & (FfccWideExtra ? 0x7FFF : 0x1FFF); // ffcc patch")
# Stable texture mapping: quads whose texture coordinates no perspective transform can fit are cut into a grid (the code
# is in ffcc_stable_uv.inc, which is spliced into the start of SubmitPolygon).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ffcc_stable_uv.inc)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/ffcc_stable_uv.inc ffcc_stable_uv_body)
string(REPLACE "\r\n" "\n" ffcc_stable_uv_body "${ffcc_stable_uv_body}")
ffcc_patch(gpu3d_src GPU3D.cpp "void GPU3D::SubmitPolygon() noexcept\n{\n"
  "void GPU3D::SubmitPolygon() noexcept\n{\n${ffcc_stable_uv_body}")
ffcc_patch(gpu3d_src GPU3D.cpp "#include <algorithm>\n"
  "#include <algorithm>\n#include <cmath>\n")
ffcc_patch(gpu3d_src GPU3D.cpp "bool FfccGxReplaying = false;\n"
  "bool FfccGxReplaying = false;\nint FfccStableUV = 0; // ffcc patch\nint FfccSmoothTex = 0; // ffcc patch\nstatic bool ffcc_subdividing = false;\nstatic int ffcc_added_p = 0, ffcc_added_v = 0, ffcc_base_p = 0, ffcc_base_v = 0, ffcc_last_p = 0, ffcc_last_v = 0;\n#define FFCC_UV_GRID 4\n")
ffcc_write_patched(gpu3d_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU3D.cpp)

# GPU3D_OpenGL.cpp: with FfccWideExtra set, the 3D texture is (256 + 2 * extra) pixels wide (a widescreen picture; the
# geometry engine's viewport and the game's projection are widened to match, a game hook sets them; not used by Phantom Hourglass).
file(READ ${MS}/GPU3D_OpenGL.cpp gpu3d_gl_src)
string(REPLACE "\r\n" "\n" gpu3d_gl_src "${gpu3d_gl_src}")
ffcc_patch(gpu3d_gl_src GPU3D_OpenGL.cpp "    if (betterpolygons == BetterPolygons && scale == ScaleFactor)\n        return;\n"
  "    if (betterpolygons == BetterPolygons && scale == ScaleFactor && ScreenW == (256 + 2 * FfccWideExtra) * scale) // ffcc patch\n        return;\n")
ffcc_patch(gpu3d_gl_src GPU3D_OpenGL.cpp "    ScreenW = 256 * scale;\n"
  "    ScreenW = (256 + 2 * FfccWideExtra) * scale; // ffcc patch\n")
ffcc_patch(gpu3d_gl_src GPU3D_OpenGL.cpp "namespace melonDS
{
" "namespace melonDS
{
extern int FfccWideExtra; // ffcc patch
extern int FfccSmoothTex; // ffcc patch
")
# Texture smoothing: the setting goes to the 3D shader as the uSmoothTex uniform, set each time the render shader is used.
ffcc_patch(gpu3d_gl_src GPU3D_OpenGL.cpp "    if (CurShaderID == flags) return;
    glUseProgram(RenderShader[flags]);
    CurShaderID = flags;

    RenderModeULoc = glGetUniformLocation(RenderShader[flags], \"uRenderMode\");
"
"    if (CurShaderID != flags)
    {
        glUseProgram(RenderShader[flags]);
        CurShaderID = flags;

        RenderModeULoc = glGetUniformLocation(RenderShader[flags], \"uRenderMode\");
    }
    glUniform1i(glGetUniformLocation(RenderShader[flags], \"uSmoothTex\"), FfccSmoothTex); // ffcc patch
")
# The 3D textures' wrap modes through sampler objects (ph patch: smeared textures on a phone's GPU, see the code).
ffcc_patch(gpu3d_gl_src GPU3D_OpenGL.cpp "void GLRenderer3D::SetupPolygonTexture(const RendererPolygon* poly) const\n{\n"
  "// ph patch: the 3D's textures repeat, mirror or clamp through a sampler object per mode, bound for each draw, instead\n// of changing the texture's own wrap state between draws: a phone's GPU driver (Mali, which draws a frame's draws later\n// in one go) applied a later wrap state to earlier draws, and textures that repeat smeared as if clamped.\nstatic GLuint ph_wrap_samplers[9] = {};\nstatic void ph_bind_wrap_sampler(GLuint unit, GLint s, GLint t)\n{\n    if (!ph_wrap_samplers[0])\n    {\n        const GLint modes[3] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT};\n        glGenSamplers(9, ph_wrap_samplers);\n        for (int k = 0; k < 9; k++)\n        {\n            glSamplerParameteri(ph_wrap_samplers[k], GL_TEXTURE_WRAP_S, modes[k / 3]);\n            glSamplerParameteri(ph_wrap_samplers[k], GL_TEXTURE_WRAP_T, modes[k % 3]);\n            glSamplerParameteri(ph_wrap_samplers[k], GL_TEXTURE_MIN_FILTER, GL_NEAREST);\n            glSamplerParameteri(ph_wrap_samplers[k], GL_TEXTURE_MAG_FILTER, GL_NEAREST);\n        }\n    }\n    const int si = s == GL_CLAMP_TO_EDGE ? 0 : s == GL_REPEAT ? 1 : 2;\n    const int ti = t == GL_CLAMP_TO_EDGE ? 0 : t == GL_REPEAT ? 1 : 2;\n    glBindSampler(unit, ph_wrap_samplers[si * 3 + ti]);\n}\n// After the draw: the other passes read these texture units with their textures' own state.\nstatic void ph_unbind_wrap_samplers()\n{\n    for (GLuint unit = 0; unit < 3; unit++) glBindSampler(unit, 0);\n}\n\nvoid GLRenderer3D::SetupPolygonTexture(const RendererPolygon* poly) const\n{\n")
ffcc_patch(gpu3d_gl_src GPU3D_OpenGL.cpp "    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, repeatS);\n    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, repeatT);\n}\n"
  "    ph_bind_wrap_sampler(iscap ? (poly->TexID == (GLuint)-1 ? 1 : 2) : 0, repeatS, repeatT); // ph patch\n}\n")
ffcc_patch(gpu3d_gl_src GPU3D_OpenGL.cpp "    glDrawElements(rp->PrimType, rp->NumIndices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->IndicesOffset * 2));\n"
  "    glDrawElements(rp->PrimType, rp->NumIndices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->IndicesOffset * 2));\n    ph_unbind_wrap_samplers(); // ph patch\n")
ffcc_patch(gpu3d_gl_src GPU3D_OpenGL.cpp "    glDrawElements(primtype, numindices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->IndicesOffset * 2));\n"
  "    glDrawElements(primtype, numindices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->IndicesOffset * 2));\n    ph_unbind_wrap_samplers(); // ph patch\n")
ffcc_patch(gpu3d_gl_src GPU3D_OpenGL.cpp "    glDrawElements(GL_LINES, numindices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->EdgeIndicesOffset * 2));\n"
  "    glDrawElements(GL_LINES, numindices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->EdgeIndicesOffset * 2));\n    ph_unbind_wrap_samplers(); // ph patch\n")
ffcc_write_patched(gpu3d_gl_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU3D_OpenGL.cpp)

# GPU_OpenGL.cpp: with FfccWideExtra set, the final output (both screens' layers) is (256 + 2 * extra) pixels wide: engine
# A's picture fills it, and engine B's and the display modes' 256-wide pictures sit in the middle (FinalPassFS.glsl).
file(READ ${MS}/GPU_OpenGL.cpp gpu_gl_src)
string(REPLACE "\r\n" "\n" gpu_gl_src "${gpu_gl_src}")
ffcc_patch(gpu_gl_src GPU_OpenGL.cpp "void GLRenderer::SetScaleFactor(int scale)\n{\n    if (scale == ScaleFactor)\n        return;\n"
  "void GLRenderer::SetScaleFactor(int scale)\n{\n    if (scale == ScaleFactor && ScreenW == (256 + 2 * FfccWideExtra) * scale) // ffcc patch\n        return;\n")
ffcc_patch(gpu_gl_src GPU_OpenGL.cpp "    ScaleFactor = scale;\n    ScreenW = 256 * scale;\n    ScreenH = 192 * scale;\n\n    const GLenum fbassign2[]"
  "    ScaleFactor = scale;\n    ScreenW = (256 + 2 * FfccWideExtra) * scale; // ffcc patch\n    ScreenH = 192 * scale;\n\n    const GLenum fbassign2[]")
ffcc_patch(gpu_gl_src GPU_OpenGL.cpp "        glUseProgram(FPShader);\n\n        FinalPassConfig.uScaleFactor = ScaleFactor;\n"
  "        glUseProgram(FPShader);\n        glUniform1i(glGetUniformLocation(FPShader, \"uExtra\"), FfccWideExtra); // ffcc patch\n\n        FinalPassConfig.uScaleFactor = ScaleFactor;\n")
ffcc_patch(gpu_gl_src GPU_OpenGL.cpp "    glUseProgram(CaptureShader);\n\n    GLuint inputA;\n"
  "    glUseProgram(CaptureShader);\n    glUniform1i(glGetUniformLocation(CaptureShader, \"uExtra\"), FfccWideExtra); // ffcc patch\n\n    GLuint inputA;\n")
ffcc_patch(gpu_gl_src GPU_OpenGL.cpp "namespace melonDS\n{\n"
  "namespace melonDS\n{\nextern int FfccWideExtra; // ffcc patch\n")
ffcc_write_patched(gpu_gl_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU_OpenGL.cpp)

# GPU2D_OpenGL.cpp: the compositor reads the 3D layer's middle 256 pixels when that texture is wider.
file(READ ${MS}/GPU2D_OpenGL.cpp gpu2d_gl_src)
string(REPLACE "\r\n" "\n" gpu2d_gl_src "${gpu2d_gl_src}")
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "        cfg.BGOffset[0][0] = xpos - ((xpos & 0x100) << 1);\n"
  "        cfg.BGOffset[0][0] = xpos - ((xpos & 0x100) << 1) + FfccWideExtra; // ffcc patch\n")
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "            cfg.Size[0] = 256; cfg.Size[1] = 192;\n            cfg.Type = 6;\n"
  "            cfg.Size[0] = FfccComputeOutputWidth > 0 ? FfccComputeOutputWidth : 256 + 2 * FfccWideExtra; cfg.Size[1] = 192; // ph patch: sample the padded Compute texture at its real width\n            cfg.Type = 6;\n")
# Engine A's 2D (its sprite layer, composite and output) is (256 + 2 * extra) pixels wide: the DS's 256 are in the
# middle, backgrounds stay there, sprites can reach the sides, and the 3D layer fills it (see the shaders below).
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "namespace melonDS\n{\n"
  "namespace melonDS\n{\nextern int FfccWideExtra, FfccHudEdges, FfccHudScale; // ffcc patch\nextern int FfccComputeOutputWidth; // ph patch\n// The HUD's pickup text (a line of the engine A background layer 2 beside the HUD's item icon) follows the HUD: the\n// rectangle (x0, y0, x1, y1) of DS pixels is moved like the HUD cluster is (anchor.xy, x offset, scale), 0 if none.\nstatic float ffcc_text_hud[4] = {0, 192, 0, 1}, ffcc_text_rect[4] = {0, 0, 0, 0}, ffcc_text_none[4] = {0, 0, 0, 0};\n")
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "void GLRenderer2D::SetScaleFactor(int scale)\n{\n    if (scale == ScaleFactor)\n        return;\n\n    ScaleFactor = scale;\n    ScreenW = 256 * scale;\n"
  "void GLRenderer2D::SetScaleFactor(int scale)\n{\n    const int extra = (GPU2D.Num == 0) ? FfccWideExtra : 0; // ffcc patch\n    if (scale == ScaleFactor && ScreenW == (256 + 2 * extra) * scale)\n        return;\n\n    ScaleFactor = scale;\n    ScreenW = (256 + 2 * extra) * scale;\n")
# (engine B shares engine A's compositor and sprite programs, so the extra width is set per draw, from this renderer's)
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "    glUseProgram(SpriteShader);\n\n    glDisable(GL_DEPTH_TEST);\n"
  "    glUseProgram(SpriteShader);\n    glUniform1i(glGetUniformLocation(SpriteShader, \"uExtra\"), (ScreenW / ScaleFactor - 256) / 2); // ffcc patch\n\n    glDisable(GL_DEPTH_TEST);\n")
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "    glUseProgram(CompositorShader);\n\n    glBindBufferBase(GL_UNIFORM_BUFFER, 20, LayerConfigUBO);\n"
  "    glUseProgram(CompositorShader);\n    glUniform1i(glGetUniformLocation(CompositorShader, \"uExtra\"), (ScreenW / ScaleFactor - 256) / 2); // ffcc patch\n    glUniform4fv(glGetUniformLocation(CompositorShader, \"uTextHud\"), 1, ffcc_text_hud);\n    glUniform4fv(glGetUniformLocation(CompositorShader, \"uTextRect\"), 1, GPU2D.Num == 0 ? ffcc_text_rect : ffcc_text_none);\n\n    glBindBufferBase(GL_UNIFORM_BUFFER, 20, LayerConfigUBO);\n")
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "u16* vtxbuf = SpriteVtxData;\n    int vtxnum = 0;\n"
  "u16* vtxbuf = SpriteVtxData;\n    int vtxnum = 0;\n    // ffcc patch: the HUD of engine A. A cluster of touching sprites at the left or right edge of the screen is scaled\n    // (FfccHudScale %) about the screen's corner, and with FfccHudEdges moved out to the edge of the wide picture.\n    {\n        float hud[128][4];\n        for (int i = 0; i < 128; i++) { hud[i][0] = hud[i][1] = hud[i][2] = 0; hud[i][3] = 1; }\n        if (GPU2D.Num == 0 && !window) ffcc_text_rect[0] = ffcc_text_rect[2] = 0;\n        if (GPU2D.Num == 0 && !window && (FfccHudEdges || FfccHudScale != 100))\n        {\n            const int n = NumSprites;\n            int x0[128], y0[128], x1[128], y1[128], par[128];\n            bool ok[128];\n            for (int i = 0; i < n; i++)\n            {\n                auto& sp = SpriteConfig.uOAM[i];\n                par[i] = i;\n                x0[i] = sp.Position[0]; y0[i] = sp.Position[1] & 0xFF;  // (melonDS keeps the wrapped copy: -92 is 164)\n                if (y0[i] >= 224) y0[i] -= 256;\n                x1[i] = x0[i] + sp.BoundSize[0]; y1[i] = y0[i] + sp.BoundSize[1];\n                ok[i] = sp.OBJMode != 2 && x1[i] > 0 && x0[i] < 256 && y1[i] > 0 && y0[i] < 192;\n            }\n            auto find = [&](int a) { while (par[a] != a) a = par[a] = par[par[a]]; return a; };\n            for (int i = 0; i < n; i++)\n                for (int j = i + 1; j < n; j++)\n                    if (ok[i] && ok[j] && x0[i] <= x1[j] + 1 && x0[j] <= x1[i] + 1 && y0[i] <= y1[j] + 1 && y0[j] <= y1[i] + 1)\n                        par[find(j)] = find(i);\n            int cx0[128], cy0[128], cx1[128], cy1[128];\n            for (int i = 0; i < n; i++) { cx0[i] = cy0[i] = 1 << 20; cx1[i] = cy1[i] = -(1 << 20); }\n            for (int i = 0; i < n; i++)\n            {\n                if (!ok[i]) continue;\n                const int r = find(i);\n                cx0[r] = std::min(cx0[r], x0[i]); cy0[r] = std::min(cy0[r], y0[i]);\n                cx1[r] = std::max(cx1[r], x1[i]); cy1[r] = std::max(cy1[r], y1[i]);\n            }\n            const float scale = FfccHudScale / 100.0f;\n            for (int i = 0; i < n; i++)\n            {\n                if (!ok[i]) continue;\n                const int r = find(i);\n                if (cx1[r] - cx0[r] > 150 || cy1[r] - cy0[r] > 110) continue;\n                const bool left = cx0[r] <= 4, right = cx1[r] >= 252;\n                if (left == right) continue;\n                const bool top = cy0[r] <= 24, bottom = !top && cy1[r] >= 168;\n                hud[i][0] = left ? 0.0f : 256.0f;\n                hud[i][1] = top ? 0.0f : bottom ? 192.0f : (cy0[r] + cy1[r]) * 0.5f;\n                hud[i][2] = FfccHudEdges ? (left ? -(float)FfccWideExtra : (float)FfccWideExtra) : 0.0f;\n                hud[i][3] = scale;\n                // The item icon is up: the pickup text is beside it. (The HUD cluster is 145 pixels wide, a dialog's speaker\n                // portrait, also at the bottom left, only 64: its text must stay where the dialog put it.)\n                if (left && bottom && cy0[r] < 152 && cx1[r] - cx0[r] >= 100)\n                {\n                    ffcc_text_hud[0] = 0; ffcc_text_hud[1] = 192; ffcc_text_hud[2] = hud[i][2]; ffcc_text_hud[3] = scale;\n                    ffcc_text_rect[0] = 0; ffcc_text_rect[1] = (float)cy0[r]; ffcc_text_rect[2] = 200; ffcc_text_rect[3] = 162;\n                }\n            }\n        }\n        glUniform4fv(glGetUniformLocation(SpriteShader, \"uHud\"), 128, &hud[0][0]);\n    }\n")
# The HUD clusters moved out to the wide picture's edges this frame (engine A), so the frontend can map a touch on one
# back to the DS pixel the game sees: FfccHudMoved = x0, y0, x1, y1 (DS pixels, before the move), x offset.
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "extern int FfccWideExtra, FfccHudEdges, FfccHudScale; // ffcc patch\n"
  "extern int FfccWideExtra, FfccHudEdges, FfccHudScale; // ffcc patch\nint FfccHudMoved[32][5]; int FfccHudMovedCount = 0; // ph patch\nint FfccHudSides = 0; // ph patch\nint FfccHudSeen = 1000; // frames since the game's HUD was last on engine A's screen (the minimap hides without it)\n")
# FfccHudSides (Phantom Hourglass): in the HUD's bands (the top 56 rows, the bottom 42) a cluster goes to the side of
# the screen it is on even when it does not touch the edge (the rupee count beside its icon), and the menu bar that
# slides out at the bottom (as wide as the screen) stays with the Menu button, at the left.
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "                if (cx1[r] - cx0[r] > 150 || cy1[r] - cy0[r] > 110) continue;\n                const bool left = cx0[r] <= 4, right = cx1[r] >= 252;\n"
  "                const bool ph_bar = FfccHudSides && cy0[r] >= 150 && cy1[r] - cy0[r] <= 48; // ph patch\n                if ((cx1[r] - cx0[r] > 150 && !ph_bar) || cy1[r] - cy0[r] > 110) continue;\n                bool left = cx0[r] <= 4, right = cx1[r] >= 252;\n                if (FfccHudSides && !left && !right && (cy1[r] <= 56 || cy0[r] >= 150)) { left = cx1[r] <= 128; right = cx0[r] >= 128; }\n                if (ph_bar && left && right) right = false;\n")
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "                if (left && bottom && cy0[r] < 152 && cx1[r] - cx0[r] >= 100)\n"
  "                if (!FfccHudSides && left && bottom && cy0[r] < 152 && cx1[r] - cx0[r] >= 100) // (ph patch: FFCC's pickup text)\n")
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "        if (GPU2D.Num == 0 && !window) ffcc_text_rect[0] = ffcc_text_rect[2] = 0;\n"
  "        if (GPU2D.Num == 0 && !window) ffcc_text_rect[0] = ffcc_text_rect[2] = 0;\n        if (GPU2D.Num == 0 && !window) FfccHudMovedCount = 0; // ph patch\n        bool ph_recorded[128] = {};\n        if (GPU2D.Num == 0 && !window) // the HUD is a sprite at least 32 wide at a side edge in the top or bottom band (hearts, Menu button)\n        {\n            bool seen = false;\n            for (int i = 0; i < NumSprites && !seen; i++)\n            {\n                auto& sp = SpriteConfig.uOAM[i];\n                int sx0 = sp.Position[0], sy0 = sp.Position[1] & 0xFF;\n                if (sy0 >= 224) sy0 -= 256;\n                const int sx1 = sx0 + sp.BoundSize[0], sy1 = sy0 + sp.BoundSize[1];\n                if (sp.OBJMode == 2 || sx1 <= 0 || sx0 >= 256 || sy1 <= 0 || sy0 >= 192) continue;\n                if (sp.BoundSize[0] >= 32 && (sx0 <= 4 || sx1 >= 252) && (sy1 <= 56 || sy0 >= 150)) seen = true; // Menu / Items buttons (they slide away while Link walks)\n                if (sx0 < 64 && sy0 >= -4 && sy1 <= 28) seen = true; // the hearts, top left (always up in play; they sit at y -1)\n            }\n            FfccHudSeen = seen ? 0 : std::min(FfccHudSeen + 1, 1000);\n        }\n")
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "                hud[i][3] = scale;\n"
  "                hud[i][3] = scale;\n                if (FfccHudEdges && !window && !ph_recorded[r] && FfccHudMovedCount < 32) // ph patch\n                {\n                    ph_recorded[r] = true;\n                    int* m = FfccHudMoved[FfccHudMovedCount++];\n                    m[0] = cx0[r]; m[1] = cy0[r]; m[2] = cx1[r]; m[3] = cy1[r]; m[4] = (int)hud[i][2];\n                }\n")
# FfccHudSides: a sprite moved last frame that is still in the same place with the same picture keeps its move, even
# when it now touches something too big to move (the Menu button under a dialogue box).
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "                }\n            }\n        }\n        glUniform4fv(glGetUniformLocation(SpriteShader, \"uHud\"), 128, &hud[0][0]);\n"
  "                }\n            }\n            if (FfccHudSides) // ph patch\n            {\n                static int ph_prev_n = 0;\n                static int ph_prev[128][5];  // tile, palette, x, y, offset\n                for (int i = 0; i < n; i++)\n                {\n                    if (!ok[i] || hud[i][2] != 0.0f) continue;\n                    const auto& sp = SpriteConfig.uOAM[i];\n                    for (int k = 0; k < ph_prev_n; k++)\n                    {\n                        if (ph_prev[k][0] != (int)sp.TileOffset || ph_prev[k][1] != (int)sp.PalOffset || ph_prev[k][2] != x0[i] || ph_prev[k][3] != y0[i]) continue;\n                        hud[i][0] = 0; hud[i][1] = 0; hud[i][2] = (float)ph_prev[k][4]; hud[i][3] = scale;\n                        if (FfccHudMovedCount < 32)\n                        {\n                            int* m = FfccHudMoved[FfccHudMovedCount++];\n                            m[0] = x0[i]; m[1] = y0[i]; m[2] = x1[i]; m[3] = y1[i]; m[4] = ph_prev[k][4];\n                        }\n                        break;\n                    }\n                }\n                ph_prev_n = 0;\n                for (int i = 0; i < n; i++)\n                {\n                    if (!ok[i] || hud[i][2] == 0.0f) continue;\n                    const auto& sp = SpriteConfig.uOAM[i];\n                    int* q = ph_prev[ph_prev_n++];\n                    q[0] = (int)sp.TileOffset; q[1] = (int)sp.PalOffset; q[2] = x0[i]; q[3] = y0[i]; q[4] = (int)hud[i][2];\n                }\n            }\n        }\n        glUniform4fv(glGetUniformLocation(SpriteShader, \"uHud\"), 128, &hud[0][0]);\n")
# The 2018 Adreno 5xx driver (LG V30, Snapdragon 835: OpenGL ES 3.2 V@251.0) crashes inside its own buffer checks
# (EsxContext::ValidateBufferBindings / BufferObjectDisableReorderCheck, a null pointer) on the compositor's second draw of
# a frame, with the GPU and Compute renderers alike. Sending the queued work first (glFlush) avoids it; only on Adreno 5xx,
# where it was found (nds-recomp-manager tools/phone-survey, 2026-10-29). Other GPUs draw as before.
ffcc_patch(gpu2d_gl_src GPU2D_OpenGL.cpp "    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);\n    glBindVertexArray(Parent.RectVtxArray);\n    glDrawArrays(GL_TRIANGLES, 0, 2*3);\n\n    glDisable(GL_SCISSOR_TEST);\n}"
  "    // ph patch: Adreno 5xx drivers crash on this draw unless the queued work is sent first (third_party/CMakeLists.txt)\n    static const bool ph_adreno5 = [] { const char* r = (const char*)glGetString(GL_RENDERER); return r && std::string(r).find(\"Adreno (TM) 5\") != std::string::npos; }();\n    if (ph_adreno5) glFlush();\n    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);\n    glBindVertexArray(Parent.RectVtxArray);\n    glDrawArrays(GL_TRIANGLES, 0, 2*3);\n\n    glDisable(GL_SCISSOR_TEST);\n}")
ffcc_write_patched(gpu2d_gl_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU2D_OpenGL.cpp)

# NDSCart.cpp: fast cartridge transfers. The game reads the card with the CPU waiting on every word, at the card's
# speed (about 26 KB a frame with its gap settings), so loading an effect in combat stalls the game for frames. With
# FfccFastCart set, a transfer has no gaps and a word takes 4 cycles. Declared in runtime/src/gx_hook.hpp.
file(READ ${MS}/NDSCart.cpp ndscart_src)
string(REPLACE "\r\n" "\n" ndscart_src "${ndscart_src}")
ffcc_patch(ndscart_src NDSCart.cpp "using Platform::LogLevel;\n"
  "using Platform::LogLevel;\n\n// ffcc patch\nbool FfccFastCart = false;\n")
ffcc_patch(ndscart_src NDSCart.cpp "    if (datasize) cmddelay += ((ROMCnt >> 16) & 0x3F);\n"
  "    if (datasize) cmddelay += ((ROMCnt >> 16) & 0x3F);\n    if (FfccFastCart) { xfercycle = 1; cmddelay = 8; } // ffcc patch\n")
ffcc_patch(ndscart_src NDSCart.cpp "        delay += ((ROMCnt >> 16) & 0x3F);\n\n    Parent.NDS.ScheduleEvent(ROMTransferEvent, false, xfercycle * delay, ROMTransfer_ReceiveData, 0);\n"
  "        delay += ((ROMCnt >> 16) & 0x3F);\n    if (FfccFastCart) { xfercycle = 1; delay = 4; } // ffcc patch\n\n    Parent.NDS.ScheduleEvent(ROMTransferEvent, false, xfercycle * delay, ROMTransfer_ReceiveData, 0);\n")
ffcc_write_patched(ndscart_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/NDSCart.cpp)

# The compute renderer, also on the phone (ph patch): OpenGL ES 3.2 shaders in GPU3D_Compute.cpp, and its shader header
# made GLSL ES by gles_compute_fixes.cmake (written beside it, so its #include "GPU3D_Compute_shaders.h" finds that).
# tools/check_gles_compute.py checks them.
file(READ ${MS}/GPU3D_Compute.cpp gpu3d_compute_src)
string(REPLACE "\r\n" "\n" gpu3d_compute_src "${gpu3d_compute_src}")
ffcc_patch(gpu3d_compute_src GPU3D_Compute.cpp "namespace melonDS\n{\n\nComputeRenderer3D::ComputeRenderer3D"
  "namespace melonDS\n{\n\nextern int FfccWideExtra; // ph patch\nint FfccComputeOutputWidth = 0; // ph patch: padded width in DS pixels for the 2D compositor's texture coordinates\n\nComputeRenderer3D::ComputeRenderer3D")
ffcc_patch(gpu3d_compute_src GPU3D_Compute.cpp "    shaderSource += \"#version 430 core\\n\";\n"
  "#ifdef __ANDROID__\n    // ph patch: OpenGL ES 3.2 on the phone (its shaders made GLSL ES by third_party/gles_compute_fixes.cmake).\n    // Every sampler and image type needs a precision.\n    shaderSource += \"#version 320 es\\n\"\n                    \"precision highp float;\\nprecision highp int;\\nprecision highp usampler2D;\\n\"\n                    \"precision highp usampler2DArray;\\nprecision highp sampler2DArray;\\n\"\n                    \"precision highp uimageBuffer;\\nprecision highp image2D;\\n\";\n#else\n    shaderSource += \"#version 430 core\\n\";\n#endif\n")
# (The final pass writes the 3D picture as an image; the 2D compositor then samples it as a texture, which needs the
# texture-fetch barrier too. Desktop drivers forgive its absence; a phone GPU's deferred work need not.)
ffcc_patch(gpu3d_compute_src GPU3D_Compute.cpp "    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);\n"
  "    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT); // ph patch\n")
ffcc_patch(gpu3d_compute_src GPU3D_Compute.cpp "    ScaleFactor = scale;\n    ScreenWidth = 256 * ScaleFactor;\n    ScreenHeight = 192 * ScaleFactor;\n"
  "    ScaleFactor = scale;\n    ScreenWidth = (256 + 2 * FfccWideExtra) * ScaleFactor; // ph patch: keep the compute target aligned with the widened 3D viewport\n    ScreenHeight = 192 * ScaleFactor;\n")
ffcc_patch(gpu3d_compute_src GPU3D_Compute.cpp "    CoarseTileW = CoarseTileCountX * TileSize;\n    CoarseTileH = CoarseTileCountY * TileSize;\n"
  "    CoarseTileW = CoarseTileCountX * TileSize;\n    CoarseTileH = CoarseTileCountY * TileSize;\n    ScreenWidth = ((ScreenWidth + CoarseTileW - 1) / CoarseTileW) * CoarseTileW; // ph patch: pad the hidden edge for whole compute tiles\n    FfccComputeOutputWidth = ScreenWidth / ScaleFactor; // ph patch\n")
ffcc_patch(gpu3d_compute_src GPU3D_Compute.cpp "    glDispatchCompute(TilesPerLine*TileLines/ClearCoarseBinMaskLocalSize, 1, 1);\n"
  "    glDispatchCompute((TilesPerLine*TileLines + ClearCoarseBinMaskLocalSize - 1)/ClearCoarseBinMaskLocalSize, 1, 1); // ph patch: clear the padded tile range\n")
ffcc_write_patched(gpu3d_compute_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU3D_Compute.cpp)
file(READ ${MS}/GPU3D_Compute_shaders.h gpu3d_compute_sh)
string(REPLACE "\r\n" "\n" gpu3d_compute_sh "${gpu3d_compute_sh}")
ffcc_patch(gpu3d_compute_sh GPU3D_Compute_shaders.h "const std::string ClearCoarseBinMask =\n    BinningBuffer + R\"(\nlayout (local_size_x = ClearCoarseBinMaskLocalSize) in;\n\nvoid main()\n{\n    BinningMaskAndOffset[BinningCoarseMaskStart + gl_GlobalInvocationID.x*CoarseBinStride+0]"
  "const std::string ClearCoarseBinMask =\n    BinningBuffer + R\"(\nlayout (local_size_x = ClearCoarseBinMaskLocalSize) in;\n\nvoid main()\n{\n    if (gl_GlobalInvocationID.x >= uint(TilesPerLine*TileLines)) return; // ph patch: rounded dispatch\n    BinningMaskAndOffset[BinningCoarseMaskStart + gl_GlobalInvocationID.x*CoarseBinStride+0]")
include(${CMAKE_CURRENT_SOURCE_DIR}/gles_compute_fixes.cmake)  # (the GLSL ES fixes, for the phone)
ffcc_write_patched(gpu3d_compute_sh ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU3D_Compute_shaders.h)

# GPU.cpp: the constructor's SetRenderer() syncs VRAM captures for the *old* renderer before any is set, and
# VRAMCaptureBlockFlags is not cleared until Reset(). Leftover heap bytes in it made that call go through a null Rend:
# SIGSEGV at start on some launches (LG V30 OpenGL, tombstone 2026-11-03). No renderer = nothing to sync.
file(READ ${MS}/GPU.cpp gpu_src)
string(REPLACE "\r\n" "\n" gpu_src "${gpu_src}")
ffcc_patch(gpu_src GPU.cpp "void GPU::SyncAllVRAMCaptures()\n{\n"
  "void GPU::SyncAllVRAMCaptures()\n{\n    if (!Rend) return; // ffcc patch: called from the constructor before a renderer exists\n")
ffcc_write_patched(gpu_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU.cpp)

# NDS.cpp: frame-time probe, the sys split (runtime/src/runtime.cpp). RunSystem fires the scheduler's events; the
# patched loop times each firing by event id into FfccEventTime/FfccEventCalls (seconds, since boot), read-and-reset
# by the runtime every 60 frames. The flag gates the clock reads, so the probe costs nothing when it is off.
# Declared in runtime/src/runtime.cpp; set from RuntimeOptions::frame_time at construction. The two dispatch
# anchors have no leading spaces, so they land on BOTH dispatch sites — RunSystem and RunSystemSleep (one if
# deeper, and the fork never runs the sleep path, but its SPU/RTC firings belong in the same per-event arrays
# if it ever does). Indented anchors were tried first and silently matched only the deeper site (a substring
# match: a 16-space anchor fits inside a 20-space line); the count check below turns any other match count
# into a loud configure failure instead.
file(READ ${MS}/NDS.cpp nds_src)
string(REPLACE "\r\n" "\n" nds_src "${nds_src}")
ffcc_patch(nds_src NDS.cpp "#include \"NDS.h\"\n"
  "#include \"NDS.h\"\n#include <chrono> // ffcc patch: steady_clock for the frame-time event timers\n")
ffcc_patch(nds_src NDS.cpp "namespace melonDS\n{\n"
  "namespace melonDS\n{\n\n// ffcc patch (frame-time probe): per-event host time inside RunSystem, read by the runtime.\n// FfccFrameTimeEvents gates the clock reads (set by the runtime from RuntimeOptions::frame_time); the arrays\n// accumulate seconds per event id since boot (Event_MAX ids) and are read-and-reset by the runtime every 60\n// frames. When the probe is off the arrays stay zero.\nbool FfccFrameTimeEvents = false;\ndouble FfccEventTime[Event_MAX] = {};\nu64 FfccEventCalls[Event_MAX] = {};\n")
ffcc_patch(nds_src NDS.cpp "EventFunc func = evt.Funcs[evt.FuncID];\n"
  "EventFunc func = evt.Funcs[evt.FuncID];\n\n                // ffcc patch (frame-time probe): time each fired event by id.\n                std::chrono::steady_clock::time_point ev_t0;\n                if (FfccFrameTimeEvents) ev_t0 = std::chrono::steady_clock::now();\n")
ffcc_patch(nds_src NDS.cpp "func(evt.That, evt.Param);\n"
  "func(evt.That, evt.Param);\n                if (FfccFrameTimeEvents)\n                {\n                    FfccEventTime[i] += std::chrono::duration<double>(std::chrono::steady_clock::now() - ev_t0).count();\n                    FfccEventCalls[i]++;\n                }\n")
# (exactly two dispatch sites: RunSystem and RunSystemSleep. Any other count means the anchors matched
# something unexpected — fail the configure rather than ship a half-patched scheduler.)
set(_ft_n 0)
set(_ft_rest "${nds_src}")
while(TRUE)
  string(FIND "${_ft_rest}" "time each fired event by id" _ft_at)
  if(_ft_at EQUAL -1)
    break()
  endif()
  math(EXPR _ft_n "${_ft_n} + 1")
  string(LENGTH "time each fired event by id" _ft_nl)
  math(EXPR _ft_next "${_ft_at} + ${_ft_nl}")
  string(SUBSTRING "${_ft_rest}" ${_ft_next} -1 _ft_rest)
endwhile()
if(NOT _ft_n EQUAL 2)
  message(FATAL_ERROR "NDS.cpp frame-time patch applied ${_ft_n} times, want 2 (RunSystem + RunSystemSleep)")
endif()
ffcc_write_patched(nds_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/NDS.cpp)

set(melonDS_HOMEPAGE_URL "https://melonds.kuribo64.net")
set(melonDS_VERSION "1.1")
set(MELONDS_VERSION_SUFFIX "-ffcc")
configure_file(${MS}/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/melonds_gen/version.h)

# OpenGL renderers (classic and compute-shader 3D, upscaled 2D) and the GL function loader they use.
# Not built on Switch: there is no dynamic loading under libnx (glad.c needs <dlfcn.h>, which does not exist
# there), and v1 is the software renderer only — the same call melonDS Switch makes with
# ENABLE_OGLRENDERER=OFF. The configure-time patch generation for these files above is untouched (it only
# writes files; nothing here compiles them on Switch).
if(NOT PH_SWITCH)
  set(melonds_ogl_sources
    ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU_OpenGL.cpp ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU2D_OpenGL.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU3D_OpenGL.cpp ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU3D_Compute.cpp
    ${MS}/GPU3D_TexcacheOpenGL.cpp ${MS}/OpenGLSupport.cpp ${MS}/frontend/glad/glad.c)
endif()

# ---- ARM7 JIT spike (PH_ARM7_JIT, Switch only, default OFF) ----
# The melonDS ARM7 JIT, enabled for the ARM7 only (the ARM9 stays on the interpreter: the fork's
# recompiled ARM9 replaces melonDS's ARM9 at run time, and the JIT would only ever run the ARM7).
# Gate: arm7 was 12.16 ms/f on the title screen and 11.71 in-game, ~42% of sys (Phase N sys-split
# probe) — GO. The spike is the enablement plus the must-fixes the design record found; the parity
# and timing apparatus comes later. Default build (option OFF) is identical to today: no JIT sources,
# no JIT_ENABLED, and runtime.cpp keeps args.JIT = std::nullopt with the interpreter Execute site.
# The upstream Switch JIT path does not compile or run as written at pin 906e9eb — the patches below
# fix what the design record and the Phase 0 desk greps found (see ARM7-JIT-IMPL-RECORD-20261127.md):
# an undeclared JitMemSize, a declared-but-never-defined HandleFault, mprotect/sigaction (which do
# not exist on Switch) in the global init/fault-handler registration, a Switch fault handler that
# reads per-instance members as statics and calls a nonexistent Melon::FaultHandler, a dtor that
# frees an unassigned CodeMemBase, bare assert()s guarding every svc result, a Reset() that writes
# through an unset code base on a failed init, and a ctor tail that overwrites the Switch fastmem
# window bases with Linux-layout values.
option(PH_ARM7_JIT "Enable the melonDS ARM7 JIT spike (Switch only)" OFF)
set(melonds_jit_sources "")
if(PH_SWITCH AND PH_ARM7_JIT)
  enable_language(ASM)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/ARMJIT_A64)
  # ARMJIT_A64/ARMJIT_Compiler.cpp: JitMemSize definition, ctor hardening (null members first, every
  # svc result checked with cleanup + early return), dtor FreeCodeMem/DeInit guard, Reset() guard.
  file(READ ${MS}/ARMJIT_A64/ARMJIT_Compiler.cpp arm7jit_compiler_src)
  string(REPLACE "\r\n" "\n" arm7jit_compiler_src "${arm7jit_compiler_src}")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "Compiler::Compiler(melonDS::NDS& nds) : Arm64Gen::ARM64XEmitter(), NDS(nds)\n{\n" "// ffcc patch (ARM7 JIT spike): JitMemSize is used in this file's ctor and dtor but is never declared anywhere in the melonDS tree (a latent upstream defect — the Switch path never compiled cleanly). 32 MB, the same as ARMJIT_Global::CodeMemorySliceSize (ARMJIT_Global.h), the per-slice size upstream itself uses for a JIT code region.\nstatic constexpr u32 JitMemSize = 1024*1024*32;\nCompiler::Compiler(melonDS::NDS& nds) : Arm64Gen::ARM64XEmitter(), NDS(nds)\n{\n")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "    JitRWBase = aligned_alloc(0x1000, JitMemSize);\n" "    // ffcc patch (ARM7 JIT spike): the members start null so a failed init is detectable (the fork gates the JIT on JitRWStart) and the dtor is safe. The sizes start 0 so Reset(), which fills (JitMemMainSize + JitMemSecondarySize)/4 words through GetRWPtr(), is a no-op when init failed — Reset() runs at NDS::Reset() and on savestate load even when the fork falls back to the interpreter.\n    JitRWBase = nullptr;\n    JitRWStart = nullptr;\n    JitRXStart = nullptr;\n    CodeMemBase = nullptr;\n    JitMemMainSize = 0;\n    JitMemSecondarySize = 0;\n    JitRWBase = aligned_alloc(0x1000, JitMemSize);\n    if (JitRWBase == nullptr)\n    {\n        Log(LogLevel::Error, \"ARM7 JIT: aligned_alloc failed for code memory\\n\");\n        return;\n    }\n")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "    JitRWStart = virtmemFindAslr(JitMemSize, 0x1000);\n" "    JitRWStart = virtmemFindAslr(JitMemSize, 0x1000);\n    if (JitRWStart == nullptr)\n    {\n        Log(LogLevel::Error, \"ARM7 JIT: virtmemFindAslr failed for the RW window\\n\");\n        free(JitRWBase);\n        JitRWBase = nullptr;\n        virtmemUnlock();\n        return;\n    }\n")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "    assert(JitRXStart != NULL);\n" "    if (JitRXStart == nullptr)\n    {\n        // (the search loop above already logged \"couldn't find unmapped place for jit memory\")\n        free(JitRWBase);\n        JitRWBase = nullptr;\n        virtmemUnlock();\n        return;\n    }\n")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "    bool succeded = R_SUCCEEDED(svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)JitRXStart, (u64)JitRWBase, JitMemSize));\n    assert(succeded);\n    succeded = R_SUCCEEDED(svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)JitRXStart, JitMemSize, Perm_Rx));\n    assert(succeded);\n    succeded = R_SUCCEEDED(svcMapProcessMemory(JitRWStart, envGetOwnProcessHandle(), (u64)JitRXStart, JitMemSize));\n    assert(succeded);\n" "    // ffcc patch (ARM7 JIT spike): every svc result here was guarded only by a bare assert(), which vanishes in a Release build — a failed mapping would then be used anyway and crash (or worse). Each failure is logged with its result code, undoes the mappings already made, and returns with JitRWStart null so the fork falls back to the interpreter.\n    Result res = svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)JitRXStart, (u64)JitRWBase, JitMemSize);\n    if (R_FAILED(res))\n    {\n        Log(LogLevel::Error, \"ARM7 JIT: svcMapProcessCodeMemory failed (%08x)\\n\", res);\n        free(JitRWBase);\n        JitRWBase = nullptr;\n        virtmemUnlock();\n        return;\n    }\n    res = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)JitRXStart, JitMemSize, Perm_Rx);\n    if (R_FAILED(res))\n    {\n        Log(LogLevel::Error, \"ARM7 JIT: svcSetProcessMemoryPermission (Rx) failed (%08x)\\n\", res);\n        svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)JitRXStart, (u64)JitRWBase, JitMemSize);\n        free(JitRWBase);\n        JitRWBase = nullptr;\n        virtmemUnlock();\n        return;\n    }\n    res = svcMapProcessMemory(JitRWStart, envGetOwnProcessHandle(), (u64)JitRXStart, JitMemSize);\n    if (R_FAILED(res))\n    {\n        Log(LogLevel::Error, \"ARM7 JIT: svcMapProcessMemory failed (%08x)\\n\", res);\n        svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)JitRXStart, (u64)JitRWBase, JitMemSize);\n        free(JitRWBase);\n        JitRWBase = nullptr;\n        virtmemUnlock();\n        return;\n    }\n")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "        bool succeded = R_SUCCEEDED(svcUnmapProcessMemory(JitRWStart, envGetOwnProcessHandle(), (u64)JitRXStart, JitMemSize));\n        assert(succeded);\n        succeded = R_SUCCEEDED(svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)JitRXStart, (u64)JitRWBase, JitMemSize));\n        assert(succeded);\n        free(JitRWBase);\n" "        // ffcc patch (ARM7 JIT spike): same assert() problem as the ctor — a failed unmap is logged rather than silently ignored in Release.\n        Result res = svcUnmapProcessMemory(JitRWStart, envGetOwnProcessHandle(), (u64)JitRXStart, JitMemSize);\n        if (R_FAILED(res)) Log(LogLevel::Error, \"ARM7 JIT: svcUnmapProcessMemory failed (%08x)\\n\", res);\n        res = svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)JitRXStart, (u64)JitRWBase, JitMemSize);\n        if (R_FAILED(res)) Log(LogLevel::Error, \"ARM7 JIT: svcUnmapProcessCodeMemory failed (%08x)\\n\", res);\n        free(JitRWBase);\n")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "    ARMJIT_Global::FreeCodeMem(CodeMemBase);\n    ARMJIT_Global::DeInit();\n}\n" "#ifndef __SWITCH__\n    // ffcc patch (ARM7 JIT spike): on Switch CodeMemBase is never assigned (the Switch path maps its own dual RW/RX windows) and ARMJIT_Global::Init() is never called, so the unconditional FreeCodeMem(CodeMemBase) here would munmap a garbage pointer and DeInit() would decrement a RefCounter Init() never raised. The Switch path's code memory is unmapped and freed by the __SWITCH__ branch above; Init()/DeInit() are pure refcounting there.\n    ARMJIT_Global::FreeCodeMem(CodeMemBase);\n    ARMJIT_Global::DeInit();\n#endif\n}\n")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "void Compiler::Reset()\n{\n    LoadStorePatches.clear();\n" "void Compiler::Reset()\n{\n#ifdef __SWITCH__\n    // ffcc patch (ARM7 JIT spike): Reset() fills (JitMemMainSize + JitMemSecondarySize)/4 words through GetRWPtr() and runs at NDS::Reset() and on savestate load even when the fork fell back to the interpreter after a failed init. With a failed init the code base is unset and the sizes are indeterminate, so a null JitRWStart means: nothing to reset.\n    if (JitRWStart == nullptr) return;\n#endif\n    LoadStorePatches.clear();\n")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "#include \"../ARMJIT_Internal.h\"\n#include \"../ARMInterpreter.h\"\n#include \"../ARMJIT.h\"\n#include \"../NDS.h\"\n#include \"../ARMJIT_Global.h\"\n" "// ffcc patch (ARM7 JIT fix): the five ../ includes cannot resolve from the patched copy's directory (melonds_patched/ARMJIT_A64/): a quoted include first tries the including file's directory (melonds_patched/, where the headers are not), then appends the path as written - including the ../ - to each -I dir, so MS/../X.h lands in third_party/, not in the melonDS source dir. The impl record's 'fall through to MS' claim was wrong; the LOQ probe died on the first of them (ARMJIT_Internal.h). Plain names resolve via the melonDS include dir, where all five headers live; the ARMJIT_Compiler.h include at the top of this file is untouched and still resolves to the top-level dispatcher.\n#include \"ARMJIT_Internal.h\"\n#include \"ARMInterpreter.h\"\n#include \"ARMJIT.h\"\n#include \"NDS.h\"\n#include \"ARMJIT_Global.h\"\n")
  ffcc_patch(arm7jit_compiler_src ARMJIT_Compiler.cpp "extern \"C\" void ARM_Ret();\n" "// ffcc patch (ARM7 JIT fix): __start__ (the image base the Switch ctor searches down from for an unmapped JIT window) is used below but declared nowhere in this TU - the only declaration in the melonDS tree is in ARMJIT_Memory.cpp, invisible here. The symbol itself exists at link time (libnx switch.ld: PROVIDE_HIDDEN(__start__ = 0x0)), so only the declaration is missing; same extern-C-char form as ARMJIT_Memory.cpp.\nextern \"C\" char __start__;\nextern \"C\" void ARM_Ret();\n")
  ffcc_write_patched(arm7jit_compiler_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/ARMJIT_A64/ARMJIT_Compiler.cpp)
  # ARMJIT_Memory.cpp: HandleFault definition, Switch handler rewrite (trampoline + HandleSwitchFault
  # member), mprotect/sigaction no-ops, ctor hardening (null prologue, every svc result checked), and
  # the fastmem-window tail guard. ARMJIT_Memory.h: the HandleSwitchFault declaration.
  file(READ ${MS}/ARMJIT_Memory.cpp arm7jit_memory_src)
  string(REPLACE "\r\n" "\n" arm7jit_memory_src "${arm7jit_memory_src}")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "#if defined(__SWITCH__)\n#include <switch.h>\n" "// ffcc patch (ARM7 JIT spike): include the patched ARMJIT_Memory.h (from this same directory) before ARMJIT.h. ARMJIT.h also includes ARMJIT_Memory.h, and the include guard makes the FIRST inclusion win — without this line the original header (reached through ARMJIT.h) would win and the HandleSwitchFault declaration below would be invisible in this TU.\n#include \"ARMJIT_Memory.h\"\n#if defined(__SWITCH__)\n#include <switch.h>\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "void HandleFault(u64 pc, u64 lr, u64 fp, u64 faultAddr, u32 desc);\n" "// ffcc patch (ARM7 JIT spike): HandleFault was declared here and called by the Switch handler but never defined anywhere in the melonDS tree — a link error. A fault reaching it is unrecoverable: the JIT could not map the faulting address, so returning would re-execute the faulting instruction and spin. Log what we know and abort.\nvoid HandleFault(u64 pc, u64 lr, u64 fp, u64 faultAddr, u32 desc)\n{\n    Log(LogLevel::Error, \"ARM7 JIT: unrecoverable fastmem fault pc=%016llx lr=%016llx fp=%016llx addr=%016llx desc=%08x\\n\", (unsigned long long)pc, (unsigned long long)lr, (unsigned long long)fp, (unsigned long long)faultAddr, desc);\n    abort();\n}\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "void __libnx_exception_handler(ThreadExceptionDump* ctx)\n{\n    ARMJIT_Memory::FaultDescription desc;\n    u8* curArea = (u8*)(NDS::CurCPU == 0 ? ARMJIT_Memory::FastMem9Start : ARMJIT_Memory::FastMem7Start);\n    desc.EmulatedFaultAddr = (u8*)ctx->far.x - curArea;\n    desc.FaultPC = (u8*)ctx->pc.x;\n\n    u64 integerRegisters[33];\n    memcpy(integerRegisters, &ctx->cpu_gprs[0].x, 8*29);\n    integerRegisters[29] = ctx->fp.x;\n    integerRegisters[30] = ctx->lr.x;\n    integerRegisters[31] = ctx->sp.x;\n    integerRegisters[32] = ctx->pc.x;\n\n    if (Melon::FaultHandler(desc))\n    {\n        integerRegisters[32] = (u64)desc.FaultPC;\n\n        ARM_RestoreContext(integerRegisters);\n    }\n\n    HandleFault(ctx->pc.x, ctx->lr.x, ctx->fp.x, ctx->far.x, ctx->error_desc);\n}\n\n}\n" "// ffcc patch (ARM7 JIT spike): the upstream Switch handler does not compile as written — it reads NDS::CurCPU and ARMJIT_Memory::FastMem9Start/FastMem7Start as if they were statics (they are per-instance members), calls Melon::FaultHandler (there is no Melon namespace — the real form is ARMJIT_Memory::FaultHandler(desc, nds)), and uses the private FaultDescription/FaultHandler from a free function. The body now lives in the public static member ARMJIT_Memory::HandleSwitchFault (declared in ARMJIT_Memory.h, same patch series), which can reach the private members; this function is only the libnx trampoline.\nvoid __libnx_exception_handler(ThreadExceptionDump* ctx)\n{\n    ARMJIT_Memory::HandleSwitchFault(ctx);\n}\n\n}\n\n// ffcc patch (ARM7 JIT spike): the Switch fault handler's body. Runs the fault through FaultHandler — the same path the Win32/POSIX handlers use — and either restores the faulting context or aborts via HandleFault. ARM_RestoreContext is noreturn; HandleFault aborts; the trailing abort() only exists so the function can never fall through.\nvoid ARMJIT_Memory::HandleSwitchFault(ThreadExceptionDump* ctx)\n{\n    melonDS::NDS* nds = melonDS::NDS::Current;\n    if (!nds)\n    {\n        // No NDS on this thread. The fork sets Current on the game thread at boot, and the JIT only runs there, so this means something is badly wrong; there is nothing to resume.\n        HandleFault(ctx->pc.x, ctx->lr.x, ctx->fp.x, ctx->far.x, ctx->error_desc);\n        abort();\n    }\n    FaultDescription desc;\n    u8* curArea = (u8*)(nds->CurCPU == 0 ? nds->JIT.Memory.FastMem9Start : nds->JIT.Memory.FastMem7Start);\n    desc.EmulatedFaultAddr = (u8*)ctx->far.x - curArea;\n    desc.FaultPC = (u8*)ctx->pc.x;\n\n    u64 integerRegisters[33];\n    memcpy(integerRegisters, &ctx->cpu_gprs[0].x, 8*29);\n    integerRegisters[29] = ctx->fp.x;\n    integerRegisters[30] = ctx->lr.x;\n    integerRegisters[31] = ctx->sp.x;\n    integerRegisters[32] = ctx->pc.x;\n\n    if (FaultHandler(desc, *nds))\n    {\n        integerRegisters[32] = (u64)desc.FaultPC;\n\n        ARM_RestoreContext(integerRegisters);\n    }\n\n    HandleFault(ctx->pc.x, ctx->lr.x, ctx->fp.x, ctx->far.x, ctx->error_desc);\n    abort();\n}\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "    if (!virtualAlloc2Ptr)\n    {\n        Log(LogLevel::Error, \"Could not load new Windows virtual memory functions, fast memory is disabled.\\n\");\n    }\n#else\n" "    if (!virtualAlloc2Ptr)\n    {\n        Log(LogLevel::Error, \"Could not load new Windows virtual memory functions, fast memory is disabled.\\n\");\n    }\n#elif defined(__SWITCH__)\n    // ffcc patch (ARM7 JIT spike): no registration call on Switch. The fault handler is libnx's __libnx_exception_handler, which this file defines (strong symbol, overriding libnx's weak default); sigaction does not exist in libnx/newlib for this target, so the POSIX branch below would fail to link.\n#else\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "#else\n    sigaction(SIGSEGV, &OldSaSegv, nullptr);\n#ifdef __APPLE__\n" "#elif defined(__SWITCH__)\n    // ffcc patch (ARM7 JIT spike): nothing to unregister on Switch — see RegisterFaultHandler.\n#else\n    sigaction(SIGSEGV, &OldSaSegv, nullptr);\n#ifdef __APPLE__\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "ARMJIT_Memory::ARMJIT_Memory(melonDS::NDS& nds) : NDS(nds)\n{\n    ARMJIT_Global::Init();\n#if defined(__SWITCH__)\n    MemoryBase = (u8*)aligned_alloc(0x1000, MemoryTotalSize);\n" "ARMJIT_Memory::ARMJIT_Memory(melonDS::NDS& nds) : NDS(nds)\n{\n    ARMJIT_Global::Init();\n#if defined(__SWITCH__)\n    // ffcc patch (ARM7 JIT spike): these members have no default initializer upstream, so null them first. The dtor reads FastMem9Reservation/FastMem7Reservation (null-checked) and passes MemoryBaseCodeMem to svcUnmapProcessCodeMemory even when the ctor fails part-way, so every early return below must leave them in a state the dtor tolerates.\n    FastMem9Start = nullptr;\n    FastMem7Start = nullptr;\n    FastMem9Reservation = nullptr;\n    FastMem7Reservation = nullptr;\n    MemoryBaseCodeMem = nullptr;\n    MemoryBase = (u8*)aligned_alloc(0x1000, MemoryTotalSize);\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "    MemoryBase = (u8*)aligned_alloc(0x1000, MemoryTotalSize);\n    virtmemLock();\n    MemoryBaseCodeMem = (u8*)virtmemFindCodeMemory(MemoryTotalSize, 0x1000);\n" "    MemoryBase = (u8*)aligned_alloc(0x1000, MemoryTotalSize);\n    if (MemoryBase == nullptr)\n    {\n        Log(LogLevel::Error, \"ARM7 JIT: aligned_alloc failed for fastmem backing store\\n\");\n        // (the windows and reservations stay null from the prologue above; the Memory dtor tolerates that, and the JIT is gated off by the Compiler ctor failing the same way — the interpreter never touches fastmem.)\n        return;\n    }\n    virtmemLock();\n    MemoryBaseCodeMem = (u8*)virtmemFindCodeMemory(MemoryTotalSize, 0x1000);\n    if (MemoryBaseCodeMem == nullptr)\n    {\n        Log(LogLevel::Error, \"ARM7 JIT: virtmemFindCodeMemory failed for fastmem backing store\\n\");\n        virtmemUnlock();\n        free(MemoryBase);\n        MemoryBase = nullptr;\n        return;\n    }\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "    bool succeded = R_SUCCEEDED(svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)MemoryBaseCodeMem,\n        (u64)MemoryBase, MemoryTotalSize));\n    assert(succeded);\n    succeded = R_SUCCEEDED(svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)MemoryBaseCodeMem,\n        MemoryTotalSize, Perm_Rw));\n    assert(succeded);\n" "    // ffcc patch (ARM7 JIT spike): the svc results were guarded only by bare assert(), which vanishes in Release. A failed mapping is logged and the ctor returns with the fastmem windows unmapped; the JIT is gated off by the Compiler ctor failing the same way, and the interpreter never touches fastmem.\n    Result res = svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)MemoryBaseCodeMem,\n        (u64)MemoryBase, MemoryTotalSize);\n    if (R_FAILED(res))\n    {\n        Log(LogLevel::Error, \"ARM7 JIT: fastmem svcMapProcessCodeMemory failed (%08x)\\n\", res);\n        virtmemUnlock();\n        free(MemoryBase);\n        MemoryBase = nullptr;\n        return;\n    }\n    res = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)MemoryBaseCodeMem,\n        MemoryTotalSize, Perm_Rw);\n    if (R_FAILED(res))\n    {\n        Log(LogLevel::Error, \"ARM7 JIT: fastmem svcSetProcessMemoryPermission failed (%08x)\\n\", res);\n        svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)MemoryBaseCodeMem, (u64)MemoryBase, MemoryTotalSize);\n        virtmemUnlock();\n        free(MemoryBase);\n        MemoryBase = nullptr;\n        return;\n    }\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "    // 8 GB of address space, just don't ask...\n    FastMem9Start = virtmemFindAslr(AddrSpaceSize, 0x1000);\n    assert(FastMem9Start);\n    FastMem7Start = virtmemFindAslr(AddrSpaceSize, 0x1000);\n    assert(FastMem7Start);\n\n    FastMem9Reservation = virtmemAddReservation(FastMem9Start, AddrSpaceSize);\n    FastMem7Reservation = virtmemAddReservation(FastMem7Start, AddrSpaceSize);\n    virtmemUnlock();\n\n    u8* basePtr = MemoryBaseCodeMem;\n" "    // 8 GB of address space, just don't ask...\n    FastMem9Start = virtmemFindAslr(AddrSpaceSize, 0x1000);\n    FastMem7Start = virtmemFindAslr(AddrSpaceSize, 0x1000);\n    if (FastMem9Start == nullptr || FastMem7Start == nullptr)\n    {\n        // ffcc patch (ARM7 JIT spike): the ASLR reservations were guarded only by assert(). A null window is logged and the ctor returns with the windows unmapped; the JIT is gated off by the Compiler ctor failing the same way, and the interpreter never touches fastmem.\n        Log(LogLevel::Error, \"ARM7 JIT: virtmemFindAslr failed for a fastmem window\\n\");\n        svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)MemoryBaseCodeMem, (u64)MemoryBase, MemoryTotalSize);\n        virtmemUnlock();\n        free(MemoryBase);\n        MemoryBase = nullptr;\n        FastMem9Start = nullptr;\n        FastMem7Start = nullptr;\n        return;\n    }\n\n    FastMem9Reservation = virtmemAddReservation(FastMem9Start, AddrSpaceSize);\n    FastMem7Reservation = virtmemAddReservation(FastMem7Start, AddrSpaceSize);\n    virtmemUnlock();\n\n    u8* basePtr = MemoryBaseCodeMem;\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "#endif\n    FastMem9Start = MemoryBase+MemoryTotalSize;\n    FastMem7Start = static_cast<u8*>(FastMem9Start)+AddrSpaceSize;\n}\n" "#endif\n#ifndef __SWITCH__\n    // ffcc patch (ARM7 JIT spike): these two lines ran on ALL platforms, overwriting the Switch branch's ASLR reservation bases (set above via virtmemFindAslr) with the Linux-layout values (backing store + windows in one contiguous mapping). On Switch MapIntoRange would then target heap addresses instead of the reservations and fail. The Switch keeps its ASLR bases.\n    FastMem9Start = MemoryBase+MemoryTotalSize;\n    FastMem7Start = static_cast<u8*>(FastMem9Start)+AddrSpaceSize;\n#endif\n}\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "                bool success = memory.UnmapFromRange(Addr + segmentOffset, Num, segmentOffset + LocalOffset + OffsetsPerRegion[region], segmentSize);\n" "                // ffcc patch (ARM7 JIT fix): upstream calls memory.UnmapFromRange here, but there is no 'memory' in scope in Mapping::Unmap (its parameters are region and nds; the only object with UnmapFromRange is the ARMJIT_Memory instance). The non-Switch branch just below calls nds.JIT.Memory.UnmapFromRange - the same object, the same method - so this is that call, and it compiles: Mapping is nested in ARMJIT_Memory, so it can reach the private UnmapFromRange, exactly as the non-Switch branch already does.\n                bool success = nds.JIT.Memory.UnmapFromRange(Addr + segmentOffset, Num, segmentOffset + LocalOffset + OffsetsPerRegion[region], segmentSize);\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "#if defined(__SWITCH__)\n#include <switch.h>\n" "#if defined(__SWITCH__)\n#include <switch.h>\n// ffcc patch (ARM7 JIT fix): sysconf(_SC_PAGESIZE) in IsFastMemSupported is compiled on Switch but declared nowhere in this TU on Switch - the <unistd.h> that declares it sits in the non-Switch branch of the original include block, and no header in the include chain provides it (checked: the whole melonDS chain plus libnx). newlib ships unistd.h, so this is a no-op if the declaration already resolves some other way; without it the LOQ build would fail on sysconf after the four probe defects are fixed.\n#include <unistd.h>\n")
  ffcc_patch(arm7jit_memory_src ARMJIT_Memory.cpp "#else\n        PageSize = sysconf(_SC_PAGESIZE);\n        isSupported = PageSize == RegularPageSize || PageSize == LargePageSize;\n#endif\n" "#elif defined(__SWITCH__)\n        // ffcc patch (ARM7 JIT sysconf fix): newlib on Switch declares sysconf in <unistd.h> (so this TU compiles) but does not link it - the Phase P probe failed at link with 'undefined reference to sysconf' here, the only sysconf call in the JIT sources (tree-wide grep at pin 906e9eb). The call is a page-size query only: sysconf(_SC_PAGESIZE) returns 4096 on this target (devkitPro newlib newlib/libc/sys/arm/sysconf.c, 128 only under SMALL_MEMORY, not defined for the Switch build), and Horizon pages are 4096 - the fork's own Switch paths already assume 0x1000 everywhere (aligned_alloc and virtmemFindAslr in the Memory ctor above). 4096 == RegularPageSize, so fastmem is supported exactly as the query would have said; PageShift below becomes 12, as the MappingStatus arrays assume.\n        PageSize = 4096;\n        isSupported = PageSize == RegularPageSize || PageSize == LargePageSize;\n#else\n        PageSize = sysconf(_SC_PAGESIZE);\n        isSupported = PageSize == RegularPageSize || PageSize == LargePageSize;\n#endif\n")
  ffcc_write_patched(arm7jit_memory_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/ARMJIT_Memory.cpp)
  # ARMJIT_Global.cpp: skip the mprotect in Init() on Switch (there is no mprotect on Horizon).
  file(READ ${MS}/ARMJIT_Global.cpp arm7jit_global_src)
  string(REPLACE "\r\n" "\n" arm7jit_global_src "${arm7jit_global_src}")
  ffcc_patch(arm7jit_global_src ARMJIT_Global.cpp "        #elif defined(APPLE_AARCH64) || defined(__NetBSD__) || defined(__OpenBSD__)\n            // Apple aarch64 always uses dynamic allocation\n        #else\n            mprotect(GetAlignedCodeMemoryStart(), CodeMemoryAlignedSize, PROT_EXEC | PROT_READ | PROT_WRITE);\n        #endif\n" "        #elif defined(APPLE_AARCH64) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__SWITCH__)\n            // Apple aarch64 always uses dynamic allocation\n            // (ffcc patch, ARM7 JIT spike: Switch too — there is no mprotect on Horizon. Code memory is mapped RW/RX through svcMapProcessCodeMemory, and the fault handler is libnx's __libnx_exception_handler, overridden by ARMJIT_Memory.cpp, not a sigaction.)\n        #else\n            mprotect(GetAlignedCodeMemoryStart(), CodeMemoryAlignedSize, PROT_EXEC | PROT_READ | PROT_WRITE);\n        #endif\n")
  ffcc_patch(arm7jit_global_src ARMJIT_Global.cpp "#else\n#include <sys/mman.h>\n#include <unistd.h>\n#endif\n" "#else\n#ifndef __SWITCH__\n// (ffcc patch, ARM7 JIT fix: newlib on Switch has no sys/mman.h - the LOQ probe died on this include. The only mman symbols this TU uses are mprotect, already guarded out on Switch in Init(), and mmap/munmap, guarded out below; unistd.h exists in newlib and stays.)\n#include <sys/mman.h>\n#endif\n#include <unistd.h>\n#endif\n")
  ffcc_patch(arm7jit_global_src ARMJIT_Global.cpp "#else\n    //printf(\"mmaping...\\n\");\n    return mmap(nullptr, CodeMemorySliceSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\n#endif\n" "#elif defined(__SWITCH__)\n    // ffcc patch (ARM7 JIT fix): no mmap on Switch - code memory comes from the svc dual-mapping in the Compiler ctor, never from here; the slice path above serves any caller (AllocateCodeMem has no callers in the melonDS tree at this pin outside the x64/A64 Compiler non-Switch branches). Returning null is the honest failure.\n    return nullptr;\n#else\n    //printf(\"mmaping...\\n\");\n    return mmap(nullptr, CodeMemorySliceSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\n#endif\n")
  ffcc_patch(arm7jit_global_src ARMJIT_Global.cpp "#else\n    munmap(codeMem, CodeMemorySliceSize);\n#endif\n" "#elif defined(__SWITCH__)\n    // ffcc patch (ARM7 JIT fix): no munmap on Switch - nothing is ever mmaped through this TU on Switch (slice pointers return early above; the Compiler dtor's FreeCodeMem call is itself guarded out on Switch). Nothing to release.\n#else\n    munmap(codeMem, CodeMemorySliceSize);\n#endif\n")
  ffcc_write_patched(arm7jit_global_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/ARMJIT_Global.cpp)
  # ARMJIT_Memory.h: the HandleSwitchFault declaration (the only header patch; a member-function
  # declaration changes no data layout or ABI).
  file(READ ${MS}/ARMJIT_Memory.h arm7jit_memory_h_src)
  string(REPLACE "\r\n" "\n" arm7jit_memory_h_src "${arm7jit_memory_h_src}")
  ffcc_patch(arm7jit_memory_h_src ARMJIT_Memory.h "    static void RegisterFaultHandler();\n    static void UnregisterFaultHandler();\n" "    static void RegisterFaultHandler();\n    static void UnregisterFaultHandler();\n\n#ifdef __SWITCH__\n    // ffcc patch (ARM7 JIT spike): the Switch fault handler is a free function (libnx calls the C symbol __libnx_exception_handler), so it cannot reach the private FaultHandler/FaultDescription. (ARM7 JIT fix: the parameter was declared as 'struct ThreadExceptionDump*' - but libnx defines ThreadExceptionDump as a typedef of an anonymous struct (switch/arm/thread_context.h), so the elaborated specifier is ill-formed; the bare typedef name is used. The typedef is visible here because this header itself includes <switch.h> under __SWITCH__.) This public static member is its body: it runs the fault through FaultHandler and either restores the faulting context or aborts via HandleFault. Declared here so the free function can call it; defined in ARMJIT_Memory.cpp. A member-function declaration changes no data layout or ABI, so TUs that see the original header (via ARMJIT.h) are unaffected.\n    static void HandleSwitchFault(ThreadExceptionDump* ctx);\n#endif\n")
  ffcc_write_patched(arm7jit_memory_h_src ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/ARMJIT_Memory.h)
  # The 12 JIT sources: 3 patched copies above, 9 used as-is (the A64 backend files, the dolphin
  # emitter, and the shared ARMJIT core). ARMJIT_Linkage.S needs the ASM language (enabled above);
  # its relative include of ../ARMJIT_x64/ARMJIT_Offsets.h resolves from its own directory, as usual.
  set(melonds_jit_sources
    ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/ARMJIT_A64/ARMJIT_Compiler.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/ARMJIT_Memory.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/ARMJIT_Global.cpp
    ${MS}/ARM_InstrInfo.cpp ${MS}/ARMJIT.cpp
    ${MS}/dolphin/CommonFuncs.cpp ${MS}/dolphin/Arm64Emitter.cpp ${MS}/dolphin/MathUtil.cpp
    ${MS}/ARMJIT_A64/ARMJIT_ALU.cpp ${MS}/ARMJIT_A64/ARMJIT_LoadStore.cpp ${MS}/ARMJIT_A64/ARMJIT_Branch.cpp
    ${MS}/ARMJIT_A64/ARMJIT_Linkage.S)
endif()

add_library(melonds_core STATIC
  ${MS}/ARCodeFile.cpp ${MS}/ARDatabaseDAT.cpp ${MS}/AREngine.cpp ${MS}/ARM.cpp ${MS}/ARMInterpreter.cpp
  ${MS}/ARMInterpreter_ALU.cpp ${MS}/ARMInterpreter_Branch.cpp ${MS}/ARMInterpreter_LoadStore.cpp ${MS}/CP15.cpp
  ${MS}/CRC32.cpp ${MS}/DMA.cpp ${MS}/DMA_Timings.cpp ${MS}/DSi.cpp ${MS}/DSi_AES.cpp ${MS}/DSi_Camera.cpp
  ${MS}/DSi_DSP.cpp ${MS}/DSi_I2C.cpp ${MS}/DSi_I2S.cpp ${MS}/DSi_NAND.cpp ${MS}/DSi_NDMA.cpp ${MS}/DSi_NWifi.cpp
  ${MS}/DSi_SD.cpp ${MS}/DSi_SPI_TSC.cpp ${MS}/FATIO.cpp ${MS}/FATStorage.cpp ${MS}/GBACart.cpp
  ${MS}/GBACartMotionPak.cpp ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU.cpp ${MS}/GPU_Soft.cpp ${MS}/GPU2D.cpp ${MS}/GPU2D_Soft.cpp ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/GPU3D.cpp
  ${MS}/GPU3D_Soft.cpp ${MS}/GPU3D_Texcache.cpp ${MS}/Mic.cpp ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/NDS.cpp ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/NDSCart.cpp ${MS}/ROMList.cpp
  ${MS}/FreeBIOS.cpp ${MS}/RTC.cpp ${MS}/Savestate.cpp ${MS}/SPI.cpp ${MS}/SPI_Firmware.cpp ${MS}/SPU.cpp
  ${MS}/Utils.cpp ${MS}/Wifi.cpp ${MS}/WifiAP.cpp
  ${MS}/DSP_HLE/UcodeBase.cpp ${MS}/DSP_HLE/AACUcode.cpp ${MS}/DSP_HLE/G711Ucode.cpp ${MS}/DSP_HLE/GraphicsUcode.cpp
  ${MS}/NDSCart/CartCommon.cpp ${MS}/NDSCart/CartRetail.cpp ${MS}/NDSCart/CartRetailNAND.cpp
  ${MS}/NDSCart/CartRetailIR.cpp ${MS}/NDSCart/CartRetailBT.cpp ${MS}/NDSCart/CartSD.cpp
  ${MS}/NDSCart/CartHomebrew.cpp ${MS}/NDSCart/CartR4.cpp
  ${MS}/fatfs/ff.c ${MS}/fatfs/ffsystem.c ${MS}/fatfs/ffunicode.c
  ${MS}/sha1/sha1.c ${MS}/tiny-AES-c/aes.c ${MS}/xxhash/xxhash.c ${MS}/blip-buf/blip_buf.c
  ${melonds_ogl_sources} ${melonds_jit_sources})
set_target_properties(melonds_core PROPERTIES CXX_STANDARD 17)
if(PH_SWITCH AND PH_ARM7_JIT)
  # (ARM7 JIT spike: the JIT class exists only under JIT_ENABLED — ARM.h's CPUExecuteMode::JIT, the
  # real ARMJIT class in ARMJIT.h, and the JIT Execute path in ARMv4::Execute are all behind it.
  # PUBLIC so it propagates to ntr_runtime, which links melonds_core PUBLIC, and the runtime.cpp
  # gating in the fork.)
  target_compile_definitions(melonds_core PUBLIC JIT_ENABLED)
endif()
target_include_directories(melonds_core SYSTEM PUBLIC ${MS} ${CMAKE_CURRENT_BINARY_DIR}/melonds_gen)
# (PlatformOGL.h's fallback for MELONDS_GL_HEADER is double-quoted, so define it as melonDS's frontend does.
# Only with the OpenGL renderers: on Switch nothing in the core includes the GL headers, and PlatformOGL.h
# defaults MELONDS_GL_HEADER to glad.h itself when it is not defined.)
if(NOT PH_SWITCH)
  target_compile_definitions(melonds_core PUBLIC OGLRENDERER_ENABLED MELONDS_GL_HEADER=\"frontend/glad/glad.h\")
endif()

# The GL renderers' GLSL sources, embedded as C string headers (as melonDS's own OpenGL_shaders/CMakeLists.txt does).
set(MELONDS_SHADERS 2DLayerPreVS 2DLayerPreFS 2DSpritePreVS 2DSpritePreFS 2DSpriteVS 2DSpriteFS 2DCompositorVS
  2DCompositorFS FinalPassVS FinalPassFS CaptureVS CaptureFS CaptureDownscaleVS CaptureDownscaleFS 3DClearVS 3DClearFS
  3DClearBitmapVS 3DClearBitmapFS 3DRenderVS 3DRenderFS 3DFinalPassVS 3DFinalPassEdgeFS 3DFinalPassFogFS)
# Widescreen (FfccWideExtra): the 2D compositor, the sprite pass, the final pass and the display capture work on
# (256 + 2 * uExtra) pixels across for engine A. In the compositor, fTexcoord.xy is the DS pixel (x runs from -uExtra to
# 256 + uExtra - 1: the backgrounds only have 0-255) and fTexcoord.zw the pixel of the wide texture.
file(READ ${MS}/OpenGL_shaders/2DCompositorVS.glsl shader_2DCompositorVS)
string(REPLACE "\r\n" "\n" shader_2DCompositorVS "${shader_2DCompositorVS}")
ffcc_patch(shader_2DCompositorVS 2DCompositorVS.glsl "uniform int uScaleFactor;\n"
  "uniform int uScaleFactor;\nuniform int uExtra;\n")
ffcc_patch(shader_2DCompositorVS 2DCompositorVS.glsl "    fTexcoord.xy = vPosition * vec2(256, 192);\n    fTexcoord.zw = fTexcoord.xy * uScaleFactor;\n"
  "    vec2 wide = vPosition * vec2(256 + 2 * uExtra, 192);\n    fTexcoord.xy = wide - vec2(uExtra, 0);\n    fTexcoord.zw = wide * uScaleFactor;\n")
ffcc_write_patched(shader_2DCompositorVS ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/OpenGL_shaders/2DCompositorVS.glsl)
file(READ ${MS}/OpenGL_shaders/2DCompositorFS.glsl shader_2DCompositorFS)
string(REPLACE "\r\n" "\n" shader_2DCompositorFS "${shader_2DCompositorFS}")
ffcc_patch(shader_2DCompositorFS 2DCompositorFS.glsl "uniform int uScaleFactor;\n"
  "uniform int uScaleFactor;\nuniform int uExtra;\n")
ffcc_patch(shader_2DCompositorFS 2DCompositorFS.glsl "            vec4 color = texelFetch(OBJLayerTex, ivec3(curpos * uScaleFactor, 0), 0);\n            ivec4 flags = ivec4(texelFetch(OBJLayerTex, ivec3(curpos * uScaleFactor, 1), 0) * 255.0);\n"
  "            vec4 color = texelFetch(OBJLayerTex, ivec3((curpos + ivec2(uExtra, 0)) * uScaleFactor, 0), 0);\n            ivec4 flags = ivec4(texelFetch(OBJLayerTex, ivec3((curpos + ivec2(uExtra, 0)) * uScaleFactor, 1), 0) * 255.0);\n")
ffcc_patch(shader_2DCompositorFS 2DCompositorFS.glsl "    if (uScanline[line].MosaicSize.x > 0)\n        MosaicX = texelFetch(MosaicTex, ivec2(bgcoord.x, uScanline[line].MosaicSize.x), 0).r;\n"
  "    // (the wide sides have no background pixels, only the 3D layer and sprites)\n    bool outside = (fTexcoord.x < 0.0) || (fTexcoord.x >= 256.0);\n    if (uScanline[line].MosaicSize.x > 0 && !outside)\n        MosaicX = texelFetch(MosaicTex, ivec2(bgcoord.x, uScanline[line].MosaicSize.x), 0).r;\n")
ffcc_patch(shader_2DCompositorFS 2DCompositorFS.glsl "    layercol[3] = BG3CalcAndFetch(bgcoord, line);\n"
  "    layercol[3] = BG3CalcAndFetch(bgcoord, line);\n    if (outside)\n    {\n        layercol[1] = vec4(0);\n        layercol[2] = vec4(0);\n        layercol[3] = vec4(0);\n        if (!uEnable3D) layercol[0] = vec4(0);\n    }\n")
ffcc_patch(shader_2DCompositorFS 2DCompositorFS.glsl "uniform int uExtra;\n"
  "uniform int uExtra;\nuniform vec4 uTextHud, uTextRect; // the pickup text: (anchor.xy, x offset, scale), (x0, y0, x1, y1)\n")
ffcc_patch(shader_2DCompositorFS 2DCompositorFS.glsl "        if (!uEnable3D) layercol[0] = vec4(0);\n    }\n"
  "        if (!uEnable3D) layercol[0] = vec4(0);\n    }\n    if (uTextRect.z > uTextRect.x)  // (the pickup text of background 2, moved like the HUD)\n    {\n        vec2 a = uTextHud.xy, o = vec2(uTextHud.z, 0);\n        vec2 lo = a + (uTextRect.xy - a) * uTextHud.w + o, hi = a + (uTextRect.zw - a) * uTextHud.w + o;\n        vec2 p = fTexcoord.xy;\n        if (all(greaterThanEqual(p, lo)) && all(lessThan(p, hi)))\n        {\n            vec2 q = a + (p - o - a) / uTextHud.w;\n            layercol[2] = BG2CalcAndFetch(vec2(q.x, fract(q.y)), clamp(int(q.y), 0, 191));\n        }\n        else if (all(greaterThanEqual(p, uTextRect.xy)) && all(lessThan(p, uTextRect.zw)))\n            layercol[2] = vec4(0);\n    }\n")
ffcc_patch(shader_2DCompositorFS 2DCompositorFS.glsl "    if (uScanline[line].MosaicSize.z > 0)\n    {\n        CalcSpriteMosaic("
  "    if (uScanline[line].MosaicSize.z > 0 && !outside)\n    {\n        CalcSpriteMosaic(")
ffcc_write_patched(shader_2DCompositorFS ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/OpenGL_shaders/2DCompositorFS.glsl)
file(READ ${MS}/OpenGL_shaders/2DSpriteVS.glsl shader_2DSpriteVS)
string(REPLACE "\r\n" "\n" shader_2DSpriteVS "${shader_2DSpriteVS}")
ffcc_patch(shader_2DSpriteVS 2DSpriteVS.glsl "in ivec2 vPosition;\n"
  "uniform int uExtra;\nuniform vec4 uHud[128]; // per sprite: anchor.xy, x offset, scale\nin ivec2 vPosition;\n")
ffcc_patch(shader_2DSpriteVS 2DSpriteVS.glsl "    vec2 fbsize = vec2(256, 192);\n"
  "    vec2 fbsize = vec2(256 + 2 * uExtra, 192);\n")
ffcc_patch(shader_2DSpriteVS 2DSpriteVS.glsl "    gl_Position = vec4(((vec2(vPosition) * 2) / fbsize) - 1, z, 1);\n"
  "    vec4 hud = uHud[vSpriteIndex];\n    vec2 pos = hud.xy + (vec2(vPosition) - hud.xy) * hud.w + vec2(hud.z, 0);\n    gl_Position = vec4((((pos + vec2(uExtra, 0)) * 2) / fbsize) - 1, z, 1);\n")
ffcc_write_patched(shader_2DSpriteVS ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/OpenGL_shaders/2DSpriteVS.glsl)
file(READ ${MS}/OpenGL_shaders/FinalPassFS.glsl shader_FinalPassFS)
string(REPLACE "\r\n" "\n" shader_FinalPassFS "${shader_FinalPassFS}")
ffcc_patch(shader_FinalPassFS FinalPassFS.glsl "uniform sampler2DArray AuxInputTex;\n"
  "uniform sampler2DArray AuxInputTex;\nuniform int uExtra;\n")
ffcc_patch(shader_FinalPassFS FinalPassFS.glsl "    ivec4 col_main = ivec4(texture(MainInputTexA, fTexcoord.xy, 0) * 255.0) >> 2;\n    ivec4 col_sub = ivec4(texture(MainInputTexB, fTexcoord.xy, 0) * 255.0) >> 2;\n"
  "    // Engine A's picture fills the (wide) output; engine B's and the display modes' are 256 wide, in the middle.\n    float xs = (fTexcoord.x * (256.0 + 2.0 * uExtra) - uExtra) / 256.0;\n    bool sides = (xs < 0.0) || (xs > 1.0);\n    ivec4 col_main = ivec4(texture(MainInputTexA, fTexcoord.xy, 0) * 255.0) >> 2;\n    ivec4 col_sub = sides ? ivec4(0) : (ivec4(texture(MainInputTexB, vec2(xs, fTexcoord.y), 0) * 255.0) >> 2);\n")
ffcc_patch(shader_FinalPassFS FinalPassFS.glsl "texture(AuxInputTex, vec3(fTexcoord.xz, uAuxLayer))"
  "(sides ? vec4(0) : texture(AuxInputTex, vec3(xs, fTexcoord.z, uAuxLayer)))")
ffcc_write_patched(shader_FinalPassFS ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/OpenGL_shaders/FinalPassFS.glsl)
file(READ ${MS}/OpenGL_shaders/CaptureFS.glsl shader_CaptureFS)
string(REPLACE "\r\n" "\n" shader_CaptureFS "${shader_CaptureFS}")
ffcc_patch(shader_CaptureFS CaptureFS.glsl "uniform sampler2DArray InputTexB;\n"
  "uniform sampler2DArray InputTexB;\nuniform int uExtra;\n")
ffcc_patch(shader_CaptureFS CaptureFS.glsl "    if (uDstMode == 0)\n    {\n        // source A only\n"
  "    // (engine A's picture and the 3D are wide: the capture is of the middle 256 pixels)\n    coordA.x = (coordA.x * 256.0 + uExtra) / (256.0 + 2.0 * uExtra);\n\n    if (uDstMode == 0)\n    {\n        // source A only\n")
ffcc_write_patched(shader_CaptureFS ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/OpenGL_shaders/CaptureFS.glsl)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ffcc_smooth_tex.glsl)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/ffcc_smooth_tex.glsl ffcc_smooth_tex_body)
string(REPLACE "\r\n" "\n" ffcc_smooth_tex_body "${ffcc_smooth_tex_body}")
file(READ ${MS}/OpenGL_shaders/3DRenderFS.glsl shader_3DRenderFS)
string(REPLACE "\r\n" "\n" shader_3DRenderFS "${shader_3DRenderFS}")
ffcc_patch(shader_3DRenderFS 3DRenderFS.glsl "vec4 FinalColor()\n{\n" "${ffcc_smooth_tex_body}vec4 FinalColor()\n{\n")
ffcc_patch(shader_3DRenderFS 3DRenderFS.glsl "            tcol = vec4(texture(CurTexture, texcoord)) / vec4(63,63,63,31);\n"
  "            tcol = (uSmoothTex == 0) ? (vec4(texture(CurTexture, texcoord)) / vec4(63,63,63,31)) : SmoothTexel(texcoord);\n")
ffcc_write_patched(shader_3DRenderFS ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/OpenGL_shaders/3DRenderFS.glsl)
# 3DRenderVS: the DS's "depth equal" test (polygon attribute bit 14) passes within a margin (0x200 in Z-buffer mode, 0xFF
# in W-buffer mode); melonDS's OpenGL renderer uses plain GL_LEQUAL ("TODO: proper 'equal' depth test!" in
# GPU3D_OpenGL.cpp), so a coplanar piece a hair behind wins and loses from frame to frame: flicker on objects such as
# PH's cut grass (user report, 2026-11-03). Moving those polygons toward the camera by the margin lets GL_LEQUAL pass
# where the DS's test passes. OpenGL renderer only (Compute and software emulate the test exactly).
file(READ ${MS}/OpenGL_shaders/3DRenderVS.glsl shader_3DRenderVS)
string(REPLACE "\r\n" "\n" shader_3DRenderVS "${shader_3DRenderVS}")
ffcc_patch(shader_3DRenderVS 3DRenderVS.glsl "    fpos.z = (float(vPosition.z << zshift) / 8388608.0) - 1.0;\n#endif\n"
  "    fpos.z = (float(vPosition.z << zshift) / 8388608.0) - 1.0;\n#endif\n    if ((attr & 0x4000) != 0) // ffcc patch: depth-equal margin\n    {\n#ifdef WBuffer\n        fZ -= 255.0 / 16777216.0;\n#else\n        fpos.z -= 1024.0 / 16777216.0;\n#endif\n    }\n")
ffcc_write_patched(shader_3DRenderVS ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/OpenGL_shaders/3DRenderVS.glsl)

# The phone: OpenGL ES 3.2 copies of all of them (ours where patched above), made by gles_shaders.cmake. PH_GLES_SHADERS
# builds the PC program with them too (the gles-test preset: NVIDIA's and AMD's drivers take ES shaders), to test the
# phone's renderer on the PC.
option(PH_GLES_SHADERS "Use the OpenGL ES copies of melonDS's shaders on PC too (testing the phone's renderer)" OFF)
if(ANDROID OR PH_GLES_SHADERS)
  set(GLES_SRC_DIR ${MS}/OpenGL_shaders)
  set(GLES_PATCHED_DIR ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/OpenGL_shaders)
  set(GLES_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/melonds_gles/OpenGL_shaders)
  file(MAKE_DIRECTORY ${GLES_OUT_DIR})
  include(${CMAKE_CURRENT_SOURCE_DIR}/gles_shaders.cmake)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gles_shaders.cmake
               ${CMAKE_CURRENT_SOURCE_DIR}/gles_shader_fixes.cmake)
endif()

set(melonds_shader_headers)
foreach(sh ${MELONDS_SHADERS})
  set(out ${CMAKE_CURRENT_BINARY_DIR}/melonds_gen/OpenGL_shaders/${sh}.h)
  set(in ${MS}/OpenGL_shaders/${sh}.glsl)
  if(sh MATCHES "^(2DCompositorVS|2DCompositorFS|2DSpriteVS|FinalPassFS|CaptureFS|3DRenderVS|3DRenderFS)$")
    set(in ${CMAKE_CURRENT_BINARY_DIR}/melonds_patched/OpenGL_shaders/${sh}.glsl)
  endif()
  if(ANDROID OR PH_GLES_SHADERS)
    set(in ${GLES_OUT_DIR}/${sh}.glsl)
  endif()
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -D INPUT_FILE=${in} -D OUTPUT_FILE=${out} -D VAR_NAME=${sh}
            -P ${MELONDS_DIR}/cmake/MakeEmbed.cmake
    DEPENDS ${in} ${MELONDS_DIR}/cmake/MakeEmbed.cmake
    VERBATIM)
  list(APPEND melonds_shader_headers ${out})
endforeach()
add_custom_target(melonds_shaders DEPENDS ${melonds_shader_headers})
add_dependencies(melonds_core melonds_shaders)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
  target_compile_definitions(melonds_core PUBLIC ARCHITECTURE_x86_64=1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
  target_compile_definitions(melonds_core PUBLIC ARCHITECTURE_ARM64=1)
endif()

# DSi DSP interpreter (unused by NDS games, but referenced by the DSi code that NDS.cpp links against).
add_library(melonds_teakra STATIC
  ${MS}/teakra/src/ahbm.cpp ${MS}/teakra/src/apbp.cpp ${MS}/teakra/src/btdmp.cpp ${MS}/teakra/src/disassembler.cpp
  ${MS}/teakra/src/dma.cpp ${MS}/teakra/src/timer.cpp ${MS}/teakra/src/memory_interface.cpp ${MS}/teakra/src/mmio.cpp
  ${MS}/teakra/src/parser.cpp ${MS}/teakra/src/processor.cpp ${MS}/teakra/src/teakra.cpp)
set_target_properties(melonds_teakra PROPERTIES CXX_STANDARD 17)
target_include_directories(melonds_teakra PUBLIC ${MS}/teakra/include PRIVATE ${MS}/teakra/src)
target_link_libraries(melonds_core PRIVATE melonds_teakra)

if(MSVC)
  # Third-party code: no warnings, and the GNU semantics melonDS expects (wrapping signed overflow).
  target_compile_options(melonds_core PRIVATE /w /clang:-fwrapv)
  target_compile_options(melonds_teakra PRIVATE /w)
  target_compile_definitions(melonds_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES)
  target_include_directories(melonds_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compat)
  # sha1.c detects endianness from POSIX headers only.
  set_source_files_properties(${MS}/sha1/sha1.c PROPERTIES COMPILE_DEFINITIONS
    "LITTLE_ENDIAN=1234;BIG_ENDIAN=4321;PDP_ENDIAN=3412;BYTE_ORDER=1234")
else()
  target_compile_options(melonds_core PRIVATE -w -fwrapv)
  target_compile_options(melonds_teakra PRIVATE -w)
endif()
if(WIN32)
  target_link_libraries(melonds_core PRIVATE ole32 comctl32 wsock32 ws2_32)
else()
  find_package(Threads REQUIRED)
  # Switch: libdl does not exist under newlib/libnx (no dynamic loading on
  # Horizon), and nothing in the Switch binary calls dlopen -- the game
  # module is statically linked (game_module.cpp, #ifdef __SWITCH__) and
  # SDL_LoadObject lives only in the non-Switch branch. Linking
  # ${CMAKE_DL_LIBS} (-ldl) makes the final ph-recomp link fail with
  # "cannot find -ldl" (Phase F rebuild, step 450/453).
  if(PH_SWITCH)
    target_link_libraries(melonds_core PRIVATE Threads::Threads)
  else()
    target_link_libraries(melonds_core PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  target_link_libraries(melonds_teakra PRIVATE Threads::Threads)
endif()

# ---- SDL3 (zlib licence): window, input and audio for the game executable ----
# Downloaded at configure time into the build directory (not vendored).
# Switch: SDL3 comes from devkitPro's `switch-sdl-3.4` branch (confirmed from
# Oracles NX's CMakeLists.txt, a primary source; the commit was verified with
# `git ls-remote` before it was written here). Upstream SDL3 requires
# pthreads, which libnx does not provide, so the upstream fetch dies at
# configure time (sdl3-src/CMakeLists.txt:3696, "Threads are needed by many
# SDL subsystems"). No GIT_SHALLOW on the Switch fetch: the tag is a full
# commit SHA, and a shallow clone cannot reliably check it out.
# TODO (Switch): confirm the SDL3-nx build provides working video, audio,
# gamepad AND touch drivers on hardware before trusting it. Fallback if
# SDL3-nx proves painful: downgrade the frontend to SDL2 (switch-sdl2 is a
# stock portlib).
if(FFCC_BUILD_GAME)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  if(PH_SWITCH)
    FetchContent_Declare(SDL3
      GIT_REPOSITORY https://github.com/devkitPro/SDL.git
      GIT_TAG c329016c0e338ab4397ad930f83330c9fe058348
      EXCLUDE_FROM_ALL)
  else()
    FetchContent_Declare(SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-3.4.18
      GIT_SHALLOW TRUE
      EXCLUDE_FROM_ALL)
  endif()
  FetchContent_MakeAvailable(SDL3)

endif()
