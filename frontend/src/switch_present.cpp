// The display on Switch, v1: a no-op presenter. The OpenGL renderers are not built for Switch (no dynamic
// loading under libnx: glad.c needs <dlfcn.h>), and the thin presenter that shows the software renderer's
// frames is still owed (BUILDER-SPEC.md gap #6: Mesa GLES or deko3d). The game still runs — the runtime logs
// to stderr and the software renderer's frames are kept for screenshots — but nothing is drawn on screen yet.
// This file implements the same GlPresenter interface as gl_present.cpp, so main.cpp, menu.cpp and
// achievements.cpp need no Switch special cases; swapping in the real presenter is a one-line change in
// frontend/CMakeLists.txt (back to src/gl_present.cpp).
#include "gl_present.hpp"

namespace ph {

bool GlPresenter::init(SDL_Window* window, std::string& error) {
    (void)error;
    window_ = window;
    // (No GL context: nothing to load. gpu_ok_ stays false, so the runtime uses the software renderer.)
    version_ = "Switch stub presenter (no display yet)";
    return true;
}

void GlPresenter::shutdown() {}

void GlPresenter::release() {}

bool GlPresenter::acquire() { return true; }

bool GlPresenter::recover(std::string& error) {
    (void)error;
    return true;
}

bool GlPresenter::can_display(const ntr::Frame& frame) const {
    (void)frame;
    return true;  // (draw() runs its calls; they are all no-ops below)
}

void GlPresenter::begin(int width, int height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
}

void GlPresenter::quad(const Rect& r, float u0, float v0, float u1, float v1) {
    (void)r;
    (void)u0;
    (void)v0;
    (void)u1;
    (void)v1;
}

void GlPresenter::screen(const ntr::Frame& frame, int layer, const Rect& where, bool smooth, bool wide) {
    (void)frame;
    (void)layer;
    (void)where;
    (void)smooth;
    (void)wide;
}

void GlPresenter::draw_hd_text(int layer, const Rect& where, int wide_extra, const HudMove* hud, int hud_count) {
    (void)layer;
    (void)where;
    (void)wide_extra;
    (void)hud;
    (void)hud_count;
}

void GlPresenter::fill(const Rect& where, uint32_t argb) {
    (void)where;
    (void)argb;
}

float GlPresenter::text(float x, float y, float size, const std::string& s, uint32_t argb) {
    (void)x;
    (void)y;
    (void)size;
    (void)s;
    (void)argb;
    return 0;
}

void GlPresenter::end() {}

void GlPresenter::set_thumbnail(int slot, const uint32_t* pixels) {
    (void)slot;
    (void)pixels;
}

void GlPresenter::thumbnail(int slot, int layer, const Rect& where) {
    (void)slot;
    (void)layer;
    (void)where;
}

bool GlPresenter::read_screens(const ntr::Frame& frame, std::vector<uint32_t>& out, int& width, int& height) {
    (void)frame;
    (void)out;
    (void)width;
    (void)height;
    return false;  // (the caller falls back to the software renderer's pixels)
}

void set_gl_attributes() {}  // (no GL context on Switch yet)

}  // namespace ph
