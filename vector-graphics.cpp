#include "vector-graphics.h"
#include "assert.h"
#include <SDL_video.h>
#include <cairo/cairo.h>
#include <cairo/cairo-gl.h>




// #include <GL/gl.h>
// #include <GL/glu.h>

#define NANOVG_GL2_IMPLEMENTATION
#include "nanovg/nanovg.h"
#include "nanovg/nanovg_gl.h"

NVGcontext* g_vg = NULL;
float g_width, g_height, g_px_ratio;

void vg_init(SDL_SysWMinfo sysinfo, SDL_GLContext gl_context, int width, int height) {
  	g_vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
	g_width = width;
	g_height = height;
	g_px_ratio = g_width / g_height;
	nvgLineCap(g_vg, NVG_ROUND);
	nvgLineJoin(g_vg,  NVG_ROUND);
}

void vg_draw_line(int x1, int y1, int x2, int y2, int radius, Color c) {
	nvgBeginPath(g_vg);

	nvgMoveTo(g_vg, x1, y1);
	nvgLineTo(g_vg, x2, y2);
	// nvgFillColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
	nvgStrokeColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
	nvgStrokeWidth(g_vg, radius);
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

void vg_begin_frame(){
  nvgBeginFrame(g_vg, g_width, g_height, g_px_ratio);
};
void vg_end_frame(){
   nvgEndFrame(g_vg);

};
