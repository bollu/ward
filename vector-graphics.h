#pragma once
#include <GL/glew.h>
#include <GL/glx.h>
#include <SDL_syswm.h>
#include <SDL_video.h>
#include "SDL.h"

// cairo_set_line_cap(cr, cairo_line_cap_t::CAIRO_LINE_CAP_ROUND);
// cairo_set_line_join(cr, cairo_line_join_t::CAIRO_LINE_JOIN_ROUND);

struct Color {
    int r, g, b;
    explicit Color() { this->r = this->g = this->b = 0; };

    static Color RGB(int r, int g, int b) { return Color(r, g, b); }

   private:
    Color(int r, int g, int b) : r(r), g(g), b(b) {}
};


void vg_init(SDL_SysWMinfo sysinfo, SDL_GLContext gl_context, int width, int height);
void vg_draw_line(int x1, int y1, int x2, int y2, int radius, Color c);
void vg_draw_rect(int x1, int y1, int x2,  int y2, Color c);
void vg_draw_circle(int x, int y, int r, Color c);
void vg_begin_frame();
void vg_end_frame();
