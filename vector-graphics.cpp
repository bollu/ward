#include "vector-graphics.h"
#include "assert.h"
#include <SDL_video.h>
#include <cairo/cairo.h>
#include <cairo/cairo-gl.h>
#include <iostream>



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
	// nvgLineCap(g_vg, NVG_BUTT);
	// nvgLineJoin(g_vg, NVG_BEVEL);
	// nvgLineJoin(g_vg, NVG_MITER);
	// nvgMiterLimit(g_vg, 5.0);
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

void vg_draw_lines(V2<int> *vs, int len, int radius, Color c) {
	nvgStrokeColor(g_vg, nvgRGBA(c.r, c.g, c.b, 255));
	nvgStrokeWidth(g_vg, radius);
	nvgBeginPath(g_vg);
	nvgMoveTo(g_vg, vs[0].x, vs[0].y);
	// std::cerr << "[[[starting line\n";
	for(int i = 0; i + 1 < len; ++i) {
	  nvgQuadTo(g_vg, vs[i].x, vs[i].y, vs[i+1].x, vs[i+1].y);
	}
	// std::cerr << "ending line]]]\n";
	nvgStroke(g_vg);

	// for(int i = 1; i < len; ++i) {
	  // vg_draw_circle(vs[i].x, vs[i].y, radius, c);
	  // std::cerr << "\t-drawing line to [" << vs[i].x << " " << vs[i].y << "] \n";
	// }

}

void vg_begin_frame(){
  nvgBeginFrame(g_vg, g_width, g_height, g_px_ratio);
};
void vg_end_frame(){
   nvgEndFrame(g_vg);

};
