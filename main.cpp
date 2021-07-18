#include <iostream>
#define EASYTAB_IMPLEMENTATION
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib.h>  // Every Xlib program must include this
#include <X11/extensions/XInput.h>

#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "assert.h"
#include "easytab.h"

using namespace std;
using namespace std;

// 1080p
int SCREEN_WIDTH = -1;
int SCREEN_HEIGHT = -1;
static const int  NUM_SCREENS = 5; // 5x5 x screen total world size when we zoom out.

struct Color {
    int r, g, b;
    explicit Color() { this->r = this->g = this->b = 0; };

    static Color RGB(int r, int g, int b) { return Color(r, g, b); }

   private:
    Color(int r, int g, int b) : r(r), g(g), b(b) {}
};

static const Color g_draw_background_color = Color::RGB(0xEE, 0xEE, 0xEE);
static const Color g_overview_background_color = Color::RGB(0xEE, 0xEE, 0xEE);

// color palette
static const vector<Color> g_palette = {
    Color::RGB(0, 0, 0),        // black
    Color::RGB(158, 158, 158),  // gray
    Color::RGB(233, 30, 99),    // R
    Color::RGB(76, 175, 80),    // G
    Color::RGB(33, 150, 243),   // B
    Color::RGB(255, 160, 0)     // gold
};

// leave gap in beginning for eraser.
// static const int PALETTE_WIDTH = SCREEN_WIDTH / (1 + g_palette.size());
// static const int PALETTE_HEIGHT = SCREEN_HEIGHT / 20;
int PALETTE_WIDTH = -1;
int PALETTE_HEIGHT = -1;


//  https://github.com/serge-rgb/milton --- has great reference code for stylus
//  + SDL
//  https://github.com/serge-rgb/milton/blob/5056a615e41e914bc22bcc7d2b5dc763e58c7b85/src/sdl_milton.cc#L239
// easytab: #device[13] = Wacom Bamboo One S Pen
// pen lower: pan
// ctrl+pen lower: zoom
// pen upper: erase
// pen lower button: middle
// pen upper button: right

// TODO: refactor with vec..
struct Vec {
    int x;
    int y;
};

using ll = long long;
static ll g_max_circle_guid = 0;

using ll = long long;
struct Circle {
    ll guid;
    int x, y;
    int radius;
    Color color;

    Circle(int x, int y, int radius, Color color, ll guid)
        : x(x), y(y), radius(radius), color(color), guid(guid) {}
};

struct CurveState {
    int startx;
    int starty;
    int prevx;
    int prevy;
    bool is_drawing;
    CurveState()
        : startx(0), starty(0), prevx(0), prevy(0), is_drawing(false){};
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
    bool is_eraser;
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
    RenderState() : zoom(1), panx(SCREEN_WIDTH*2), pany(SCREEN_HEIGHT*2){};
    bool damaged = true;
} g_renderstate;

std::vector<Circle> g_circles;

// dubious has concatenation from stack overflow:
// https://stackoverflow.com/a/54945214/5305365
struct hash_pair_int {
    size_t operator()(const pair<int, int> &pi) const {
        return std::hash<int>()(pi.first) * 31 + std::hash<int>()(pi.second);
    };
};

// TODO: order the circle indexes by insertion time, so we paint in the right order.
static const int SPATIAL_HASH_CELL_SIZE = 300;
unordered_map<pair<int, int>, unordered_set<int>, hash_pair_int> g_spatial_hash;

// returns if circle was really added.
bool add_circle_to_spatial_hash(const Circle &c) {
    int sx = c.x / SPATIAL_HASH_CELL_SIZE;
    int sy = c.y / SPATIAL_HASH_CELL_SIZE;
    // TODO: treat circles as rects, not points.
    unordered_set<int> &bucket = g_spatial_hash[make_pair(sx, sy)];

    // this is already covered.
    for (int ix : bucket) {
        const Circle &d = g_circles[ix];
        int dx = c.x - d.x;
        int dy = c.y - d.y;
        int dlsq = dx * dx + dy * dy;
        if (sqrt(dlsq) + c.radius < d.radius) {
            return false;
        }
    }
    bucket.insert(c.guid);
    return true; 
}

void run_command(vector<int> &cmd) {
    for (const int cix : cmd) {
        const Circle &c = g_circles[cix];
        const int sx = c.x / SPATIAL_HASH_CELL_SIZE;
        const int sy = c.y / SPATIAL_HASH_CELL_SIZE;
        std::unordered_set<int> &bucket = g_spatial_hash[make_pair(sx, sy)];
        if (bucket.count(cix)) {
            bucket.erase(cix);
        } else {
            bucket.insert(cix);
        }
    }
};

struct Commander {
    vector<vector<int>> cmds;
    // have run commands till index
    int runtill = -1;

    void undo() {
        std::cerr << "trying to undo...\n";
        if (runtill < 0) {
            return;
        }
        std::cerr << "\tundoing\n";
        assert(runtill < cmds.size());
        run_command(cmds[runtill]);
        runtill--;
        assert(runtill >= -1);
    }

    void redo() {
        // nothing to redo.
        if (runtill == cmds.size() - 1) {
            return;
        }
        std::cerr << "redoing command!\n";
        run_command(cmds[runtill + 1]);
        runtill++;
    }

    void start_new_command() {
        // if we have more commands, drop extra commands.
        if (this->runtill != cmds.size() - 1) {
            // keep [0, ..., undoix]
            // eg. undoix=0 => resize[0..0]
            cmds.resize(this->runtill + 1);
        }

        assert(this->runtill == this->cmds.size() - 1);
        this->cmds.push_back({});
        this->runtill = this->cmds.size() - 1;
    }

    void add_to_command(const Circle &c) {
        assert(this->runtill == this->cmds.size() - 1);
        assert(this->cmds.size() >= 0);
        this->cmds[this->runtill].push_back(c.guid);
    }

} g_commander;



// TODO: overview mode is very sluggish :( 
void draw_pen_strokes(SDL_Renderer *renderer, int const WIDTH = SCREEN_WIDTH,
                      const int HEIGHT = SCREEN_HEIGHT) {
    const float zoominv = (1.0 / g_renderstate.zoom);
    const int startx = zoominv * g_renderstate.panx;
    const int starty = zoominv * g_renderstate.pany;
    const int endx = zoominv * (startx + SCREEN_WIDTH);
    const int endy = zoominv * (starty + SCREEN_HEIGHT);

    for (int xix = startx / SPATIAL_HASH_CELL_SIZE - 1;
         xix <= endx / SPATIAL_HASH_CELL_SIZE; ++xix) {
        for (int yix = starty / SPATIAL_HASH_CELL_SIZE - 1; yix < endy; ++yix) {

            if (!g_spatial_hash.count(make_pair(xix, yix))) {
                continue;
            }

            unordered_set<int> &bucket = g_spatial_hash[make_pair(xix, yix)];
            SDL_Rect rect;
            for (int cix : bucket) {
                const Circle &c = g_circles[cix];
                // cout << "\t- circle(x=" << c.x << " y=" << c.y << " rad=" <<
                // c.radius << ")\n";
                rect.x = c.x - c.radius;
                rect.y = c.y - c.radius;
                rect.w = rect.h = c.radius;
                rect.x -= g_renderstate.panx;
                rect.y -= g_renderstate.pany;
                rect.x *= g_renderstate.zoom;
                rect.y *= g_renderstate.zoom;
                rect.w *= g_renderstate.zoom;
                rect.h *= g_renderstate.zoom;
                SDL_SetRenderDrawColor(renderer, c.color.r, c.color.g,
                                       c.color.b, SDL_ALPHA_OPAQUE);
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }
}

void draw_palette(SDL_Renderer *renderer) {
    // selected palette is drawn slightly higher.
    int SELECTED_PALETTE_HEIGHT = PALETTE_HEIGHT * 1.3;
    // draw eraser.
    {
        SDL_Rect rect;
        // leave gap at beginnning for eraser.
        rect.x = 0 * PALETTE_WIDTH;
        rect.w = PALETTE_WIDTH;

        rect.h = (g_colorstate.is_eraser ? SELECTED_PALETTE_HEIGHT
                                          : PALETTE_HEIGHT);
        rect.y = SCREEN_HEIGHT - rect.h;
        Color color = Color::RGB(255, 255, 255);  // eraser indicated by white.
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b,
                               SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &rect);
    }
    // for each color in the color wheel, assign location.
    for (int i = 0; i < g_palette.size(); ++i) {
        SDL_Rect rect;
        // leave gap at beginnning for eraser.
        rect.x = (1 + i) * PALETTE_WIDTH;
        rect.w = PALETTE_WIDTH;
        bool selected = !g_colorstate.is_eraser && (g_colorstate.colorix == i);
        rect.h = selected ? SELECTED_PALETTE_HEIGHT : PALETTE_HEIGHT;
        rect.y = SCREEN_HEIGHT - rect.h;

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


// https://stackoverflow.com/a/3069122/5305365
// Approximate General Sweep Boundary of a 2D Curved Object,
// dynadraw: http://www.graficaobscura.com/dyna/dynadraw.c
// SDL2-cairo: https://github.com/tsuu32/sdl2-cairo-example
// meditate on a stylus only UI for undo/redo.
// add eraser radius.
// add keyboard shortcut for eraser/palette toggle?
int main() {
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        cerr << "Failed to initialise SDL\n";
        return -1;
    }

    SDL_DisplayMode DM;
    SDL_GetCurrentDisplayMode(0, &DM);
    SCREEN_WIDTH = DM.w;
    SCREEN_HEIGHT = DM.h;

    assert(SCREEN_WIDTH >= 0 && "unable to detect screen width");
    assert(SCREEN_HEIGHT >= 0 && "unable to detect screen height");
    
    PALETTE_WIDTH = SCREEN_WIDTH / (1 + g_palette.size());
    PALETTE_HEIGHT = SCREEN_HEIGHT / 20;

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
    assert(ok == EASYTAB_OK && "PLEASE plug in your drawing tablet! [unable to load easytab]");

    bool g_quit = false;
    while (!g_quit) {
        const Uint64 start_count = SDL_GetPerformanceCounter();
        // Get the next event
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                g_quit = true;
                break;
            }

            else if (event.type == SDL_SYSWMEVENT) {
                EasyTabResult res =
                    EasyTab_HandleEvent(&event.syswm.msg->msg.x11.event);
                if (res != EASYTAB_OK) {
                    continue;
                }
                assert(res == EASYTAB_OK);

                // std::cerr
                //     << "npackets: " << EasyTab->NumPackets
                //     << " | posX: " << EasyTab->PosX[0]
                //     << " | posY: " << EasyTab->PosY[0]
                //     << " | pressure: " << EasyTab->Pressure[0]
                //     << " | touch "
                //     << (EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)
                //     << " | presslo "
                //     << (EasyTab->Buttons & EasyTab_Buttons_Pen_Lower)
                //     << " | presshi "
                //     << (EasyTab->Buttons & EasyTab_Buttons_Pen_Upper)
                //     << " | altitide: " << EasyTab->Orientation.Altitude
                //     << " | azimuth: " << EasyTab->Orientation.Azimuth
                //     << " | twist: " << EasyTab->Orientation.Twist
                //     << "\n";

                // TODO: draw a line from old position to current position
                // and fill with circles.

                if (EasyTab->NumPackets > 1) {
                    std::cerr << "- NumPackets: " << EasyTab->NumPackets
                              << "\n";
                }

                for (int p = 0; p < EasyTab->NumPackets; ++p) {
                    g_penstate.x = EasyTab->PosX[p];
                    g_penstate.y = EasyTab->PosY[p];
                    const int pressure = EasyTab->Pressure[p];

                    static const float PAN_FACTOR = 3;

                    // overview
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
                        g_renderstate.damaged = true;
                        continue;
                    }

                    // not is_drawing / hovering
                    if (g_curvestate.is_drawing &&
                        !(EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
                        g_curvestate.is_drawing = false;
                        continue;
                    }


                    // pressing down, not with eraser.
                    if ((EasyTab->Buttons & EasyTab_Buttons_Pen_Touch) &&
                        !g_colorstate.is_eraser) {
                        if (!g_curvestate.is_drawing) {
                            g_curvestate.startx = g_curvestate.prevx =
                                g_penstate.x + g_renderstate.panx;
                            g_curvestate.starty = g_curvestate.prevy =
                                g_penstate.y + g_renderstate.pany;
                            g_curvestate.is_drawing = true;
                            g_commander.start_new_command();
                        }

                        const int dx = abs(g_penstate.x - g_curvestate.prevx);
                        const int dy = abs(g_penstate.y - g_curvestate.prevy);

                        const int radius = EasyTab->Pressure[p] * 10;
                        int dlsq = dx * dx + dy * dy;

                        // too close to the previous position, don't create an interpolant.
                        if (dlsq < radius * radius) { continue; }

                        const int NUM_INTERPOLANTS = min<int>(max<int>(1, sqrt(dlsq)), 10);
                        for (int k = 0; k <= NUM_INTERPOLANTS; k++) {
                            int x = lerp(double(k) / NUM_INTERPOLANTS,
                                         g_curvestate.prevx,
                                         g_renderstate.panx + g_penstate.x);
                            int y = lerp(double(k) / NUM_INTERPOLANTS,
                                         g_curvestate.prevy,
                                         g_renderstate.pany + g_penstate.y);
                            const Color color = g_palette[g_colorstate.colorix];
                            assert(g_max_circle_guid < (1ll << 62) &&
                                   "too many circles!");

                            // need post ++ for guid, as we need firt guid to be zero
                            // since we push it  back into a vector.
                            const Circle circle =
                                Circle(x, y, radius, color, g_max_circle_guid);
                            // add circle to the vector to keeps the GUIDs correct.

                            // if we don't need this circle, continue, don't add
                            // it to the command. Such a circle is the price we pay...
                            // TODO: create code that cleans up such unreferenced circles!
                            if(add_circle_to_spatial_hash(circle)) {
                                g_circles.push_back(circle);
                                g_commander.add_to_command(circle);
                                g_max_circle_guid++;
                                g_renderstate.damaged = true;
                            }                         }
                        g_curvestate.prevx = g_renderstate.panx + g_penstate.x;
                        g_curvestate.prevy = g_renderstate.pany + g_penstate.y;
                        continue;
                    }

                    // choosing a color.
                    if (g_penstate.y >= SCREEN_HEIGHT - PALETTE_HEIGHT) {
                        // std::cerr << "COLOR PICKING\n";
                        int ix = g_penstate.x / PALETTE_WIDTH;
                        if (ix == 0 && !g_colorstate.is_eraser) {
                            g_colorstate.is_eraser = true;
                            g_renderstate.damaged = true;
                        } 

                        if (ix != 0 && (ix-1 != g_colorstate.colorix || g_colorstate.is_eraser)) {
                            g_colorstate.is_eraser = false;
                            g_colorstate.colorix = ix - 1;
                            g_renderstate.damaged = true;
                        }
                        assert(g_colorstate.colorix >= 0);
                        assert(g_colorstate.colorix < g_palette.size());
                        continue;
                    }

                    // hovering, since we got an event or pressing down, while
                    // is_eraser.
                    // pen down with eraser.
                    static bool is_erasing = false;

                    // not erasing / hovering
                    if (is_erasing && !(EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
                        is_erasing = false;
                        continue;
                    }

                    if (g_colorstate.is_eraser && (EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
                        if (!is_erasing) {
                            is_erasing = true;
                            g_commander.start_new_command();
                        }
                        const int ERASER_RADIUS =
                            30 + (EasyTab->Pressure[p] * 100);
                        // std::cerr << "is_eraser (radius=" << ERASER_RADIUS
                        // <<
                        // ")\n";

                        const int startx =
                            g_renderstate.panx + g_penstate.x - ERASER_RADIUS;
                        const int starty =
                            g_renderstate.pany + g_penstate.y - ERASER_RADIUS;
                        const int endx = startx + 2 * ERASER_RADIUS;
                        const int endy = starty + 2 * ERASER_RADIUS;

                        for (int xix = startx / SPATIAL_HASH_CELL_SIZE - 1;
                             xix <= endx / SPATIAL_HASH_CELL_SIZE + 1; ++xix) {
                            for (int yix = starty / SPATIAL_HASH_CELL_SIZE - 1;
                                 yix <= endy / SPATIAL_HASH_CELL_SIZE + 1;
                                 ++yix) {
                                if (!g_spatial_hash.count(
                                        make_pair(xix, yix))) {
                                    continue;
                                }
                                unordered_set<int> &bucket =
                                    g_spatial_hash[make_pair(xix, yix)];
                                vector<int> to_erase;
                                for (int cix : bucket) {
                                    Circle &c = g_circles[cix];
                                    const int dx =
                                        g_renderstate.panx + g_penstate.x - c.x;
                                    const int dy =
                                        g_renderstate.pany + g_penstate.y - c.y;
                                    // eraser has some radius without pressing.
                                    // With pressing, becomes bigger.
                                    if (dx * dx + dy * dy <=
                                        ERASER_RADIUS * ERASER_RADIUS) {
                                        to_erase.push_back(cix);
                                        g_commander.add_to_command(c);
                                        g_renderstate.damaged = true;
                                    }
                                }

                                for (int cix : to_erase) {
                                    bucket.erase(cix);
                                }
                            }
                        }
                    }  // end if(is_eraser )
                }      // end loop over packets
            } else if (event.type == SDL_KEYDOWN) {
                cerr << "keydown: " << SDL_GetKeyName(event.key.keysym.sym)
                     << "\n";
                if (event.key.keysym.sym == SDLK_q) {
                    // undo 
                    g_renderstate.damaged = true;
                    g_commander.undo();
                } else if (event.key.keysym.sym == SDLK_w) {
                    // redo
                    g_renderstate.damaged = true;
                    g_commander.redo();
                } else if (event.key.keysym.sym == SDLK_e) { 
                    // toggle eraser
                    g_renderstate.damaged = true;
                    g_colorstate.is_eraser = !g_colorstate.is_eraser;
                } else if (event.key.keysym.sym == SDLK_r) {
                    g_colorstate.colorix = (g_colorstate.colorix + 1) % g_palette.size();
                    g_renderstate.damaged = true;
                }
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
                            g_renderstate.damaged = true;
                            g_renderstate.zoom = 1.0 / 5;
                            g_overviewstate.panx = g_renderstate.panx;
                            g_overviewstate.pany = g_renderstate.pany;
                            g_renderstate.panx = g_renderstate.pany = 0;
                            break;
                        }
                        if (g_overviewstate.overviewing) {
                            g_overviewstate.overviewing = false;
                            g_renderstate.damaged = true;
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
                            g_renderstate.damaged = true;
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
                cerr << "mousedown: |" << button_name << "\n";
            }
    
            else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_ENTER) {
                g_renderstate.damaged = true; // need to repaint when window gains focus
                continue;
            }


            else if (event.type == SDL_MOUSEBUTTONUP) {
                string button_name = "unk";
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        button_name = "left";
                        break;
                    case SDL_BUTTON_RIGHT:
                        // if (g_overviewstate.overviewing) {
                        //     g_panstate.panning = false;
                        //     g_renderstate.zoom = 1.0;
                        //     g_renderstate.panx = g_overviewstate.panx;
                        //     g_renderstate.pany = g_overviewstate.pany;
                        //     break;
                        // }
                        break;
                    case SDL_BUTTON_MIDDLE:
                        button_name = "middle";
                        if (!g_overviewstate.overviewing) {
                            g_renderstate.damaged = true;
                            g_panstate.panning = false;
                            break;
                        }
                        break;
                    default:
                        button_name = "unk";
                        break;
                }
                cerr << "mousedown: |" << button_name << "\n";
            }
        }

        // for logging into the console.
        const bool logging_was_damaged = g_renderstate.damaged;
        if (g_renderstate.damaged) {
            g_renderstate.damaged = false;
            Uint8 opaque = 255;
            SDL_SetRenderDrawColor(renderer, g_draw_background_color.r,
                    g_draw_background_color.g,
                    g_draw_background_color.b, opaque);
            SDL_RenderClear(renderer);
            draw_pen_strokes(renderer);
            if (!g_panstate.panning && !g_overviewstate.overviewing) {
                draw_palette(renderer);
            }
            SDL_RenderPresent(renderer);
        }

        const Uint64 end_count = SDL_GetPerformanceCounter();
        const int counts_per_second = SDL_GetPerformanceFrequency();
        const double elapsedSec = (end_count - start_count) / (double) counts_per_second;
        const double elapsedMS = elapsedSec * 1000.0;
        int FPS = 1.0 / elapsedSec;
        const int TARGET_FPS = 60;
        const double timeToNextFrameMs = 1000.0 / TARGET_FPS;
        if (timeToNextFrameMs > elapsedMS) {
            // SDL_Delay(floor(1000.0/TARGET_FPS - elapsedMS));
            printf("%20s | elapsed time: %4.2f | sleeping for: %4.2f | time to next frame: %4.2f\n", 
                    (logging_was_damaged ? "DAMAGED" : ""),
                    elapsedMS, timeToNextFrameMs - elapsedMS, timeToNextFrameMs);
            // std::cout << "fps: " << FPS << " | elapsed msec: " << elapsedMS << " | time to next frame ms: " << timeToNextFrameMs << "\n";
            SDL_Delay((timeToNextFrameMs - elapsedMS)); 
        }
    }

    // Tidy up
    EasyTab_Unload(sysinfo.info.x11.display);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

