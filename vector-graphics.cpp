#include "vector-graphics.h"

#include <SDL_video.h>
#include <cairo/cairo-gl.h>
#include <cairo/cairo.h>

#include <iostream>

#include "assert.h"

// #include <GL/gl.h>
// #include <GL/glu.h>

#define NANOVG_GL2_IMPLEMENTATION
#include "nanovg/nanovg.h"
#include "nanovg/nanovg_gl.h"

NVGcontext *g_vg = NULL;

void vg_init(SDL_GLContext gl_context) {
    if (g_vg) { nvgDeleteGL2(g_vg); }
    g_vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
    nvgLineCap(g_vg, NVG_ROUND);
    nvgLineJoin(g_vg, NVG_ROUND);
}

void vg_draw_line(int x1, int y1, int x2, int y2, int radius, Color c) {
    nvgStrokeColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
    nvgStrokeWidth(g_vg, radius);

    nvgBeginPath(g_vg);
    // nvgCircle(g_vg, x1, y1, radius);
    // nvgFillColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
    // nvgFill(g_vg);
    nvgMoveTo(g_vg, x1, y1);
    nvgLineTo(g_vg, x2, y2);
    // nvgFillColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
    nvgStroke(g_vg);
}

void vg_draw_rect(int x, int y, int w, int h, Color c) {
    nvgBeginPath(g_vg);
    nvgRect(g_vg, x, y, w, h);
    nvgFillColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
    nvgFill(g_vg);
}
void vg_draw_circle(int x, int y, int r, Color c) {
    nvgBeginPath(g_vg);
    nvgCircle(g_vg, x, y, r);
    nvgFillColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
    nvgFill(g_vg);
}

void vg_draw_lines(const std::vector<V2<int>> &vs,
                   const std::vector<bool> &visible, int radius, Color c,
                   V2<int> offset, float zoom) {
    assert(vs.size() == visible.size());
    // TODO: use `visible`vs!
    nvgStrokeColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
    nvgStrokeWidth(g_vg, radius);

    int l = 0;
    while (l + 1 < vs.size()) {
        if (!(visible[l] && visible[l + 1])) {
            l++;
            continue;
        }
        nvgBeginPath(g_vg);
        nvgMoveTo(g_vg, zoom * (vs[l].x - offset.x),
                  zoom * (vs[l].y - offset.y));
        int r = l + 1;
        for (; r < vs.size() && visible[r]; ++r) {
            nvgLineTo(g_vg, zoom * (vs[r].x - offset.x),
                      zoom * (vs[r].y - offset.y));
        }
        nvgStrokeColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
        nvgStroke(g_vg);
        l = r;
    }
}

void vg_begin_frame(int w, int h) { nvgBeginFrame(g_vg, w, h, (float)w/h); };
void vg_end_frame() { nvgEndFrame(g_vg); };
