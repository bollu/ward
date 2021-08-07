#include "vector-graphics.h"
#include "assert.h"
#include <SDL_video.h>
#include <cairo/cairo.h>
#include <cairo/cairo-gl.h>

cairo_device_t* g_cr_device = nullptr;
// SDL_Surface *g_sdl_surface = nullptr;
cairo_surface_t *g_cr_surface = nullptr;
cairo_t *g_cr = nullptr;


void vg_init(SDL_SysWMinfo sysinfo, SDL_GLContext gl_context, int width, int height) {
    g_cr_device = cairo_glx_device_create(sysinfo.info.x11.display,
        reinterpret_cast<GLXContext>(gl_context));
    assert(g_cr_device && "unable to create cairo device from openGL context!");
   
        g_cr_surface = cairo_gl_surface_create_for_window(
        g_cr_device,
        sysinfo.info.x11.window,
	width,
	height

    );

    assert(g_cr_surface && "unable to create cairo-GL surface!");
    g_cr = cairo_create(g_cr_surface);
    cairo_set_line_cap(g_cr, cairo_line_cap_t::CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(g_cr, cairo_line_join_t::CAIRO_LINE_JOIN_ROUND);

}

void vg_draw_line(int x1, int y1, int x2, int y2, int radius, Color c) {
    cairo_set_line_width(g_cr, radius);
    cairo_set_source_rgba(g_cr, c.r / 255.0, c.g / 255.0, c.b / 255.0, 1.0);

          cairo_move_to(g_cr, x1, y1);
	  cairo_line_to(g_cr, x2, y2);
        cairo_stroke(g_cr);

}
void vg_draw_rect(int x, int y, int w, int h, Color c) {
  cairo_set_source_rgba(g_cr, c.r / 255., c.g / 255., c.b / 255., 1.0);
  cairo_rectangle(g_cr, x, y, w, h);
  cairo_fill(g_cr);
}
void vg_draw_circle(int x, int y, int r, Color c) {
    cairo_set_source_rgba(g_cr, c.r / 255., c.g / 255., c.b / 255., 1.0);
    cairo_arc(g_cr, x, y, r, 0, 2.0 * M_PI);
    cairo_fill(g_cr);
}
