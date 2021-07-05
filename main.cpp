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

struct Circle {
    int x, y;
    int radius;
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

struct RenderState {
    float zoom;
    int panx, pany;
    RenderState() : zoom(1), panx(0), pany(0){};
} g_renderstate;
std::vector<Circle> cs;

void draw(SDL_Renderer *renderer, int const WIDTH = SCREEN_WIDTH,
          const int HEIGHT = SCREEN_HEIGHT) {
    for (Circle c : cs) {
        SDL_Rect rect;
        rect.x = c.x - c.radius;
        rect.y = c.y - c.radius;
        rect.w = rect.h = c.radius;
        rect.x -= g_renderstate.panx;
        rect.y -= g_renderstate.pany;
        // SDL_ALPHA_OPAQUE = 255;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &rect);
    }
}

double lerp(double t, int x0, int x1) { 
    t = std::min<double>(1, std::max<double>(0, t));
    assert(t >= 0);
    assert(t <= 1);
    return x1 * t + x0 * (1 - t);
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
    SDL_Window *window = SDL_CreateWindow("Demo Game", SDL_WINDOWPOS_UNDEFINED,
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

                    if (g_panstate.panning) {
                        g_renderstate.panx = g_panstate.startpanx +
                                             (g_panstate.startx - g_penstate.x);
                        g_renderstate.pany = g_panstate.startpany +
                                             (g_panstate.starty - g_penstate.y);
                    } else if (EasyTab->Buttons & EasyTab_Buttons_Pen_Touch) {
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


                        for (int i = 0; i <= 10; i++) {
                        cout << "i: " << i << " | dlsq: " << dlsq << "###\n";
                            Circle c;
                            c.x = lerp(float(i) / 10.0, g_curvestate.prevx,
                                       g_renderstate.panx + g_penstate.x);
                            c.y = lerp(float(i) / 10.0, g_curvestate.prevy,
                                       g_renderstate.pany + g_penstate.y);
                            c.radius = EasyTab->Pressure[0] *
                                       EasyTab->Pressure[0] * 30;
                            cs.push_back(c);
                        }

                        g_curvestate.prevx = g_renderstate.panx + g_penstate.x;
                        g_curvestate.prevy = g_renderstate.pany + g_penstate.y;
                    } else if (!(EasyTab->Buttons &
                                 EasyTab_Buttons_Pen_Touch)) {
                        g_curvestate.initialized = false;
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
                        break;
                    case SDL_BUTTON_MIDDLE:
                        button_name = "middle";
                        g_panstate.panning = true;
                        g_panstate.startpanx = g_renderstate.panx;
                        g_panstate.startpany = g_renderstate.pany;
                        g_panstate.startx = g_penstate.x;
                        g_panstate.starty = g_penstate.y;
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
                        button_name = "right";
                        break;
                    case SDL_BUTTON_MIDDLE:
                        button_name = "middle";
                        g_panstate.panning = false;
                        break;
                    default:
                        button_name = "unk";
                        break;
                }
                cout << "mousedown: |" << button_name << "\n";
            }
        }

        // Randomly change the colour
        Uint8 red = 0xEE;
        Uint8 green = 0xEE;
        Uint8 blue = 0xEE;

        // Fill the screen with the colour
        SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
        SDL_RenderClear(renderer);
        draw(renderer);
        SDL_RenderPresent(renderer);
    }

    // Tidy up
    EasyTab_Unload(sysinfo.info.x11.display);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

