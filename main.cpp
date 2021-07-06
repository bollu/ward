#include <iostream>
#define EASYTAB_IMPLEMENTATION
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib.h>  // Every Xlib program must include this
#include <X11/extensions/XInput.h>

#include <vector>

#include "assert.h"
#include "easytab.h"

using namespace std;
using namespace std;

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

struct Color {
    int r, g, b;
    explicit Color() { this->r = this->g = this->b = 0; };

    static Color RGB(int r, int g, int b) { return Color(r, g, b); }

   private:
    Color(int r, int g, int b) : r(r), g(g), b(b) {}
};

static const Color g_draw_background_color = Color::RGB(0xEE, 0xEE, 0xEE);
static const Color g_overview_background_color = Color::RGB(207, 216, 220);

// color palette
static const vector<Color> g_palette = {
    Color::RGB(0, 0, 0), // black
    Color::RGB(158, 158, 158), // gray
    Color::RGB(233, 30, 99), // R
    Color::RGB(76, 175, 80), // G
    Color::RGB(33, 150, 243), // B
    Color::RGB(255, 160, 0) // gold
};

// leave gap in beginning for eraser.
static const int PALETTE_WIDTN = SCREEN_WIDTH / (1 + g_palette.size());
static const int PALETTE_HEIGHT = 40.0;

//  https://github.com/serge-rgb/milton --- has great reference code for stylus
//  + SDL
//  https://github.com/serge-rgb/milton/blob/5056a615e41e914bc22bcc7d2b5dc763e58c7b85/src/sdl_milton.cc#L239
// easytab: #device[13] = Wacom Bamboo One S Pen
// pen lower: pan
// ctrl+pen lower: zoom
// pen upper: erase
// pen lower button: middle
// pen upper button: right

void set_pixel(SDL_Surface *surface, int x, int y, Uint32 pixel) {
    Uint32 *const target_pixel =
        (Uint32 *)((Uint8 *)surface->pixels + y * surface->pitch +
                   x * surface->format->BytesPerPixel);
    *target_pixel = pixel;
}

// TODO: refactor with vec..
struct Vec {
    int x;
    int y;
};

struct Circle {
    int x, y;
    int radius;
    bool erased;
    Color color;

    Circle(int x, int y, int radius, Color color) : x(x), y(y), radius(radius), color(color), erased(false) {}
};

struct CurveState {
    int startx;
    int starty;
    int prevx;
    int prevy;
    bool initialized;
    CurveState()
        : startx(0), starty(0), prevx(0), prevy(0), initialized(false){};
} g_curvestate;

struct PenState {
    int x, y;
    PenState() : x(0), y(0){};
} g_penstate;

struct PanState {
    bool panning;
    int startpanx;
    int startpany;
    int startx;
    int starty;

    PanState()
        : panning(false), startpanx(0), startpany(0), startx(0), starty(0){};
} g_panstate;

struct ColorState {
    int startpickx;
    int startpicky;
    int colorix = 0;  
    bool erasing;
} g_colorstate;


struct OverviewState {
    bool overviewing;
    int panx;
    int pany;

    OverviewState() : overviewing(false), panx(0), pany(0){};
} g_overviewstate;

struct RenderState {
    float zoom;
    int panx, pany;
    RenderState() : zoom(1), panx(0), pany(0){};
} g_renderstate;

std::vector<Circle> g_circles;

void draw_pen_strokes(SDL_Renderer *renderer, int const WIDTH = SCREEN_WIDTH,
                      const int HEIGHT = SCREEN_HEIGHT) {
    for (const Circle &c : g_circles) {
        if (c.erased) { continue; }
        SDL_Rect rect;
        rect.x = c.x - c.radius;
        rect.y = c.y - c.radius;
        rect.w = rect.h = c.radius;
        rect.x -= g_renderstate.panx;
        rect.y -= g_renderstate.pany;
        rect.x *= g_renderstate.zoom;
        rect.y *= g_renderstate.zoom;
        rect.w *= g_renderstate.zoom;
        rect.h *= g_renderstate.zoom;
        SDL_SetRenderDrawColor(renderer, c.color.r, c.color.g, c.color.b,
                               SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &rect);
    }
}

void draw_palette(SDL_Renderer *renderer) {
    // for each color in the color wheel, assign location.
    for (int i = 0; i < g_palette.size(); ++i) {
        SDL_Rect rect;
        // leave gap at beginnning for eraser.
        rect.x = (1 + i) * PALETTE_WIDTN;
        rect.y = SCREEN_HEIGHT - PALETTE_HEIGHT;
        rect.w = PALETTE_WIDTN;
        rect.h = PALETTE_HEIGHT;
        Color color = g_palette[i];
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b,
                               SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &rect);
    }
}

double lerp(double t, int x0, int x1) {
    t = std::min<double>(1, std::max<double>(0, t));
    assert(t >= 0);
    assert(t <= 1);
    return x1 * t + x0 * (1 - t);
}

static const int GC_NUM_ERASED_THRESHOLD = 1000;
int g_num_erased = 0;
void garbage_collect() {
    std::cerr << "GCING";
    vector<Circle> newcs;
    newcs.reserve(g_circles.size());

    for(const Circle &c : g_circles) {
        if (!c.erased) { newcs.push_back(c); }
        else { g_num_erased--; }
    }
    assert(g_num_erased == 0);
    g_circles.swap(newcs);

}

// https://stackoverflow.com/a/3069122/5305365
// Approximate General Sweep Boundary of a 2D Curved Object,
// dynadraw: http://www.graficaobscura.com/dyna/dynadraw.c
// SDL2-cairo: https://github.com/tsuu32/sdl2-cairo-example
int main() {
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        cout << "Failed to initialise SDL\n";
        return -1;
    }

    // Create a window
    SDL_Window *window = SDL_CreateWindow("WARD", SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH,
                                          SCREEN_HEIGHT, SDL_WINDOW_OPENGL);
    if (window == nullptr) {
        SDL_Log("Could not create a window: %s", SDL_GetError());
        return -1;
    }

    // https://github.com/serge-rgb/milton/blob/5056a615e41e914bc22bcc7d2b5dc763e58c7b85/src/sdl_milton.cc#L239
    // https://github.com/serge-rgb/milton/search?q=SDL_SysWMEvent
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

    SDL_SysWMinfo sysinfo;
    SDL_VERSION(&sysinfo.version);
    int ok = SDL_GetWindowWMInfo(window, &sysinfo);
    assert(ok == SDL_TRUE && "unable to get SDL X11 information");

    SDL_Renderer *renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (renderer == nullptr) {
        SDL_Log("Could not create a renderer: %s", SDL_GetError());
        return -1;
    }

    ok = EasyTab_Load(sysinfo.info.x11.display, sysinfo.info.x11.window);
    if (ok != EASYTAB_OK) {
        cerr << "easytab error code: |" << ok << "|\n";
    }
    assert(ok == EASYTAB_OK && "unable to load easytab");

    while (true) {
        // Get the next event
        SDL_Event event;
        if (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                break;
            }

            else if (event.type == SDL_SYSWMEVENT) {
                EasyTabResult res =
                    EasyTab_HandleEvent(&event.syswm.msg->msg.x11.event);

                if (res == EASYTAB_OK) {
                    std::cout
                        << "npackets: " << EasyTab->NumPackets
                        << " | posX: " << EasyTab->PosX[0]
                        << " | posY: " << EasyTab->PosY[0]
                        << " | pressure: " << EasyTab->Pressure[0]
                        << " | touch "
                        << (EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)
                        << " | presslo "
                        << (EasyTab->Buttons & EasyTab_Buttons_Pen_Lower)
                        << " | presshi "
                        << (EasyTab->Buttons & EasyTab_Buttons_Pen_Upper)
                        << " | altitide: " << EasyTab->Orientation.Altitude
                        << "\n";

                    // TODO: draw a line from old position to current position
                    // and fill with circles.

                    g_penstate.x = EasyTab->PosX[0];
                    g_penstate.y = EasyTab->PosY[0];

                    static const float PAN_FACTOR = 5;
                    if (g_overviewstate.overviewing) {
                        // if tapped, move to tap location
                        if (EasyTab->Buttons & EasyTab_Buttons_Pen_Touch) {
                            g_renderstate.panx =
                                g_penstate.x * (1.0 / g_renderstate.zoom);
                            g_renderstate.pany =
                                g_penstate.y * (1.0 / g_renderstate.zoom);
                            g_renderstate.zoom = 1.0;
                            g_overviewstate.overviewing = false;
                        }

                        // this is in charge of all events.
                        // skip events.
                        continue;
                    }

                    if (g_panstate.panning) {
                        g_renderstate.panx =
                            g_panstate.startpanx +
                            PAN_FACTOR * (g_panstate.startx - g_penstate.x);
                        g_renderstate.pany =
                            g_panstate.startpany +
                            PAN_FACTOR * (g_panstate.starty - g_penstate.y);
                    }

                    // pressing down, not with eraser.
                    if ((EasyTab->Buttons & EasyTab_Buttons_Pen_Touch) && !g_colorstate.erasing) {
                        if (!g_curvestate.initialized) {
                            g_curvestate.startx = g_curvestate.prevx =
                                g_penstate.x + g_renderstate.panx;
                            g_curvestate.starty = g_curvestate.prevy =
                                g_penstate.y + g_renderstate.pany;
                            g_curvestate.initialized = true;
                        }
                        int dx = abs(g_penstate.x - g_curvestate.prevx);
                        int dy = abs(g_penstate.y - g_curvestate.prevy);
                        int dlsq = dx * dx + dy * dy;

                        const int NUM_INTERPOLANTS = 5;
                        for (int i = 0; i <= NUM_INTERPOLANTS; i++) {
                            cout << "i: " << i << " | dlsq: " << dlsq
                                 << "###\n";
                            int x = lerp(float(i) / NUM_INTERPOLANTS,
                                       g_curvestate.prevx,
                                       g_renderstate.panx + g_penstate.x);
                            int y = lerp(float(i) / NUM_INTERPOLANTS,
                                       g_curvestate.prevy,
                                       g_renderstate.pany + g_penstate.y);
                            int radius = EasyTab->Pressure[0] *
                                       EasyTab->Pressure[0] * 30;
                            Color color = g_palette[g_colorstate.colorix];
                            g_circles.push_back(Circle(x, y, radius, color));
                        }

                        g_curvestate.prevx = g_renderstate.panx + g_penstate.x;
                        g_curvestate.prevy = g_renderstate.pany + g_penstate.y;
                    }

                    // not drawing / hovering
                    if (!(EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
                        g_curvestate.initialized = false;
                    }

                    // choosing a color.
                    if (g_penstate.y >= SCREEN_HEIGHT - PALETTE_HEIGHT) {
                        std::cerr << "COLOR PICKING\n";
                        // cur - start = delta
                        // => start + delta = cur
                        int ix = g_penstate.x / PALETTE_WIDTN;
                        if (ix == 0) {
                            g_colorstate.erasing = true; 
                        } else {
                            g_colorstate.erasing = false;
                            g_colorstate.colorix = ix-1;
                        }
                        assert(g_colorstate.colorix >= 0);
                        assert(g_colorstate.colorix < g_palette.size());
                        continue;
                    }

                    // hovering, since we got an event or pressing down, while erasing.
                    if (g_colorstate.erasing) {
                        vector<int> tokill;
                        const int ERASER_RADIUS = 40 + (EasyTab->Pressure[0] * 50);
                        std::cerr << "ERASING (radius=" << ERASER_RADIUS << ")\n";
                        for (auto &c : g_circles) {
                            if (c.erased) { continue; }
                            const int dx = g_renderstate.panx + g_penstate.x - c.x;
                            const int dy = g_renderstate.pany + g_penstate.y - c.y;
                            // eraser has some radius without pressing. With pressing, becomes bigger.
                            if (dx*dx + dy*dy < ERASER_RADIUS*ERASER_RADIUS) {
                                cout << "\terased: " << c.x << " " << c.y << "\n";
                                c.erased = true;
                                g_num_erased++;
                            }
                        }
                    }
                }
            } else if (event.type == SDL_KEYDOWN) {
                cout << "keydown: " << SDL_GetKeyName(event.key.keysym.sym)
                     << "\n";
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                string button_name = "unk";
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        button_name = "left";
                        break;
                    case SDL_BUTTON_RIGHT:
                        button_name = "right";
                        if (!g_overviewstate.overviewing) {
                            g_overviewstate.overviewing = true;
                            g_renderstate.zoom = 1.0 / 5;
                            g_overviewstate.panx = g_renderstate.panx;
                            g_overviewstate.pany = g_renderstate.pany;
                            g_renderstate.panx = g_renderstate.pany = 0;
                            break;
                        }
                        if (g_overviewstate.overviewing) {
                            g_overviewstate.overviewing = false;
                            g_renderstate.zoom = 1.0;
                            g_renderstate.panx = g_overviewstate.panx;
                            g_renderstate.pany = g_overviewstate.pany;
                            break;
                        }
                        break;

                    case SDL_BUTTON_MIDDLE:
                        button_name = "middle";
                        if (!g_overviewstate.overviewing) {
                            g_panstate.panning = true;
                            g_panstate.startpanx = g_renderstate.panx;
                            g_panstate.startpany = g_renderstate.pany;
                            g_panstate.startx = g_penstate.x;
                            g_panstate.starty = g_penstate.y;
                            break;
                        }
                        break;
                    default:
                        button_name = "unk";
                        break;
                }
                cout << "mousedown: |" << button_name << "\n";
            }

            else if (event.type == SDL_MOUSEBUTTONUP) {
                string button_name = "unk";
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        button_name = "left";
                        break;
                    case SDL_BUTTON_RIGHT:
                        break;
                    case SDL_BUTTON_MIDDLE:
                        button_name = "middle";
                        if (!g_overviewstate.overviewing) {
                            g_panstate.panning = false;
                            break;
                        }
                        break;
                    default:
                        button_name = "unk";
                        break;
                }
                cout << "mousedown: |" << button_name << "\n";
            }
        }

        // Randomly change the colour

        // Fill the screen with the colour
        if (g_overviewstate.overviewing) {
            Uint8 opaque = 255;
            SDL_SetRenderDrawColor(renderer, g_overview_background_color.r,
                                   g_overview_background_color.g,
                                   g_overview_background_color.b, opaque);

        } else {
            Uint8 opaque = 255;
            SDL_SetRenderDrawColor(renderer, g_draw_background_color.r,
                                   g_draw_background_color.g,
                                   g_draw_background_color.b, opaque);
        }
        SDL_RenderClear(renderer);
        draw_pen_strokes(renderer);
        if (!g_panstate.panning && !g_overviewstate.overviewing) {
            draw_palette(renderer);
        }
        SDL_RenderPresent(renderer);

        if (g_num_erased >= GC_NUM_ERASED_THRESHOLD) {
            garbage_collect();
        } else {
            cout << "#circles: " << g_circles.size() << "\n";
        }
    }


    // Tidy up
    EasyTab_Unload(sysinfo.info.x11.display);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

