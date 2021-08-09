#pragma once
#include <GL/glew.h>
#include <GL/glx.h>
#include <SDL_syswm.h>
#include <SDL_video.h>

#include <vector>

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

template <typename T>
struct V2 {
    T x = 0;
    T y = 0;
    V2() : x(0), y(0){};
    V2(T x, T y) : x(x), y(y){};

    V2<T> sub(const V2<T> &other) const { return V2(x - other.x, y - other.y); }
    V2<T> add(const V2<T> &other) const { return V2(x + other.x, y + other.y); }
    V2<T> scale(float f) const { return V2(f * x, f * y); }
    T lensq() const { return x * x + y * y; }

    template <typename O>
    V2<O> cast() const {
        return V2<O>(O(x), O(y));
    }
};

struct Rect {
    V2<int> pos;
    V2<int> dim;
};

template <typename T>
V2<T> operator+(V2<T> a, V2<T> b) {
    return a.add(b);
}
template <typename T>
V2<T> operator-(V2<T> a, V2<T> b) {
    return a.sub(b);
}
template <typename T>
V2<T> operator-(V2<T> a) {
    return V2<T>(-a.x, -a.y);
}
template <typename T>
V2<T> operator*(float f, V2<T> a) {
    return a.scale(f);
}
template <typename T>
V2<T> operator*(V2<T> a, float f) {
    return a.scale(f);
}
template <typename T>
V2<T> operator/(V2<T> a, float f) {
    return a.scale(1.0 / f);
}

void vg_init(SDL_SysWMinfo sysinfo, SDL_GLContext gl_context, int width,
             int height);
void vg_draw_line(int x1, int y1, int x2, int y2, int radius, Color c);
// draw at vs[i] - offset
void vg_draw_lines(const std::vector<V2<int>> &vs,
                   const std::vector<bool> &visible, int radius, Color c,
                   V2<int> offset, float zoom);
void vg_draw_rect(int x1, int y1, int x2, int y2, Color c);
void vg_draw_circle(int x, int y, int r, Color c);
void vg_begin_frame();
void vg_end_frame();
