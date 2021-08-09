#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <SDL_events.h>
#include <SDL_surface.h>
#include <SDL_video.h>

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
#include "vector-graphics.h"

// TODO: fix zoom and scroll!
// https://gist.github.com/derofim/033cb33ed46636071d3983bb7b235981
// https://github.com/cubicool/cairo-gl-sdl2/blob/master/src/sdl-example.cpp

using namespace std;

// CONFIG
static const int PEN_RADIUS = 10;

static const int MIN_ERASER_RADIUS = 30;
static const int MAX_ERASER_RADIUS = 100;

// size of grid.
const int GRID_BASE_SIZE = 100;

// 1080p
int SCREEN_WIDTH = -1;
int SCREEN_HEIGHT = -1;

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
int PALETTE_WIDTH() { return SCREEN_WIDTH / (1 + g_palette.size()); }

int PALETTE_HEIGHT() { return SCREEN_HEIGHT / 20; }

//  https://github.com/serge-rgb/milton --- has great reference code for stylus
//  https://github.com/serge-rgb/milton/blob/5056a615e41e914bc22bcc7d2b5dc763e58c7b85/src/sdl_milton.cc#L239
// easytab: #device[13] = Wacom Bamboo One S Pen

using ll = long long;
using ll = long long;

struct Segment {
    ll guid;
    vector<V2<int>> points;
    vector<bool> visible;
    Color color;
    Segment(){};
};

struct CurveState {
    bool is_down = false;
    int seg_guid;
} g_curvestate;

V2<int> g_penstate;

struct PanState {
    bool panning = false;
    V2<int> startpan;
    V2<int> startpenpos;
} g_panstate;

struct ColorState {
    V2<int> startpick;
    int colorix = 0;
    bool is_eraser = false;
    int eraser_radius = -1;  // radius of the eraser based on pressure.
} g_colorstate;

struct OverviewState {
    V2<int> minPos;
    V2<int> maxPos;
    V2<int> saved_pan;
    bool overviewing = false;
} g_overviewstate;

struct RenderState {
    float zoom = 1;
    V2<int> pan;
} g_renderstate;

std::vector<Segment> g_segments;

// https://stackoverflow.com/a/54945214/5305365
struct hash_pair_int {
    size_t operator()(const pair<int, int> &pi) const {
        return std::hash<int>()(pi.first) * 31 + std::hash<int>()(pi.second);
    };
};

// value stored into the spatial hash
struct SegPointGuid {
    int seg_guid;    // guid of segment.
    int point_guid;  // guid of point stored in segment.

    bool operator<(const SegPointGuid &other) const {
        return make_pair(seg_guid, point_guid) <
               make_pair(other.seg_guid, other.point_guid);
    }

    bool operator==(const SegPointGuid &other) const {
        return make_pair(seg_guid, point_guid) ==
               make_pair(other.seg_guid, other.point_guid);
    }

    SegPointGuid() : seg_guid(-1), point_guid(-1){};
    SegPointGuid(int seg_guid, int point_guid)
        : seg_guid(seg_guid), point_guid(point_guid){};
};

struct hash_spatial_hash_value {
    size_t operator()(const SegPointGuid &v) const {
        return std::hash<int>()(v.point_guid) * 31 +
               std::hash<int>()(v.seg_guid);
    };
};

// a key into the spatial hash is coordinates.
using SpatialHashKey = pair<int, int>;

// TODO: order the Stroke indexes
// by insertion time, so we paint in the right
// order.
static const int HASH_CELL_SZ = 1000;
unordered_map<pair<int, int>,
              unordered_set<SegPointGuid, hash_spatial_hash_value>,
              hash_pair_int>
    g_spatial_hash;

// returns true if point was added.
void add_to_spatial_hash(SegPointGuid value) {
    const Segment &s = g_segments[value.seg_guid];
    const V2<int> point = s.points[value.point_guid];
    const int sx = point.x / HASH_CELL_SZ;
    int sy = point.y / HASH_CELL_SZ;
    unordered_set<SegPointGuid, hash_spatial_hash_value> &bucket =
        g_spatial_hash[make_pair(sx, sy)];
    auto it = bucket.find(value);
    assert(it == bucket.end());
    bucket.insert(value);
}

void remove_from_spatial_hash(SegPointGuid value) {
    const Segment &s = g_segments[value.seg_guid];
    const V2<int> point = s.points[value.point_guid];
    const int sx = point.x / HASH_CELL_SZ;
    int sy = point.y / HASH_CELL_SZ;
    unordered_set<SegPointGuid, hash_spatial_hash_value> &bucket =
        g_spatial_hash[make_pair(sx, sy)];
    auto it = bucket.find(value);
    assert(it != bucket.end());
    bucket.erase(it);
}

using Command = vector<SegPointGuid>;

void run_command(const Command &c) {
    for (const SegPointGuid &v : c) {
        assert(v.seg_guid < g_segments.size());
        Segment &s = g_segments[v.seg_guid];
        assert(v.point_guid < s.visible.size());
        s.visible[v.point_guid] = !s.visible[v.point_guid];

        if (s.visible[v.point_guid]) {
        }
    }
};

struct Commander {
    vector<Command> cmds;
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
        // TODO: can ask segments to fixup data if too damaged.
        // What is the heuristic? Need some kind of amortized
        // analysis to decide when to resize a segment
        // that is changed too much.
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

    void add_to_command(SegPointGuid v) {
        assert(this->runtill == this->cmds.size() - 1);
        assert(this->cmds.size() >= 0);
        this->cmds[this->runtill].push_back(v);
    }

    const Command &getCommand() const {
        assert(this->runtill == this->cmds.size() - 1);
        assert(this->cmds.size() > 0);
        return this->cmds[this->runtill];
    }

} g_commander;

void draw_pen_strokes_cr() {
    for (int i = 0; i < g_segments.size(); ++i) {
        const Segment &s = g_segments[i];
        if (s.points.size() < 2) {
            continue;
        }
        const int line_radius = g_renderstate.zoom * PEN_RADIUS;
        // TODO: incorporate erasing here.
        vg_draw_lines(s.points, s.visible, line_radius, s.color,
                      g_renderstate.pan, g_renderstate.zoom);
    }
}

void draw_eraser_cr() {
    if (!g_colorstate.is_eraser) {
        return;
    }
    assert(g_colorstate.eraser_radius >= 0);
    const V2<float> pos = g_renderstate.zoom * (g_penstate).cast<float>();
    assert(g_colorstate.eraser_radius >= 0);
    const float radius = g_colorstate.eraser_radius * g_renderstate.zoom;
    const Color COLOR_GRAY = Color::RGB(100, 100, 100);
    vg_draw_circle(pos.x, pos.y, radius, COLOR_GRAY);
}
void draw_grid_cr() {
    const int GRIDSIZE = g_renderstate.zoom * GRID_BASE_SIZE;
    const int STARTX = -1 * (GRIDSIZE + (g_renderstate.pan.x % GRIDSIZE));
    const int STARTY = -1 * (GRIDSIZE + (g_renderstate.pan.y % GRIDSIZE));

    // set grid color.

    static const int GRID_LINE_WIDTH = 2;
    const Color GRID_LINE_COLOR = Color::RGB(170, 170, 170);

    // cairo_set_operator(cr, cairo_operator_t::CAIRO_OPERATOR_SOURCE);
    // cairo_set_line_width(cr, GRID_LINE_WIDTH);
    // cairo_set_source_rgba(cr, 170. / 255.0, 170. / 255.0, 170. / 255.0, 1.0);

    for (int x = STARTX; x <= STARTX + SCREEN_WIDTH + GRIDSIZE; x += GRIDSIZE) {
        vg_draw_line(x, STARTY, x, STARTY + SCREEN_HEIGHT + 2 * GRIDSIZE,
                     GRID_LINE_WIDTH, GRID_LINE_COLOR);
    }

    for (int y = STARTY; y <= STARTY + SCREEN_HEIGHT + GRIDSIZE;
         y += GRIDSIZE) {
        vg_draw_line(STARTX, y, STARTX + SCREEN_WIDTH + 2 * GRIDSIZE, y,
                     GRID_LINE_WIDTH, GRID_LINE_COLOR);
    }
};

void draw_palette() {
    // selected palette is drawn slightly higher.
    int SELECTED_PALETTE_HEIGHT = PALETTE_HEIGHT() * 1.3;
    // draw eraser.
    {
        SDL_Rect rect;
        // leave gap at beginnning for eraser.
        rect.x = 0 * PALETTE_WIDTH();
        rect.w = PALETTE_WIDTH();

        rect.h = (g_colorstate.is_eraser ? SELECTED_PALETTE_HEIGHT
                                         : PALETTE_HEIGHT());
        rect.y = SCREEN_HEIGHT - rect.h;
        static const Color COLOR_WHITE = Color::RGB(255, 255, 255);
        vg_draw_rect(rect.x, rect.y, rect.w, rect.h, COLOR_WHITE);
    }
    // for each color in the color wheel, assign location.
    for (int i = 0; i < g_palette.size(); ++i) {
        SDL_Rect rect;
        // leave gap at beginnning for eraser.
        rect.x = (1 + i) * PALETTE_WIDTH();
        rect.w = PALETTE_WIDTH();
        bool selected = !g_colorstate.is_eraser && (g_colorstate.colorix == i);
        rect.h = selected ? SELECTED_PALETTE_HEIGHT : PALETTE_HEIGHT();
        rect.y = SCREEN_HEIGHT - rect.h;

        Color color = g_palette[i];

        vg_draw_rect(rect.x, rect.y, rect.w, rect.h, color);
    }
}

double lerp(double t, int x0, int x1) {
    t = std::min<double>(1, std::max<double>(0, t));
    assert(t >= 0);
    assert(t <= 1);
    return x1 * t + x0 * (1 - t);
}

// handle easytab packet with index p
void handle_packet(int p) {
    assert(p >= 0);
    assert(p < EasyTab->NumPackets);

    g_penstate.x = EasyTab->PosX[p];
    g_penstate.y = EasyTab->PosY[p];
    const float pressure = EasyTab->Pressure[p];
    static const float PAN_FACTOR = 8;

    // overview
    if (g_overviewstate.overviewing) {
        // if tapped, move to tap location
        if (EasyTab->Buttons & EasyTab_Buttons_Pen_Touch) {
            g_renderstate.pan = g_renderstate.pan +  1.0/g_renderstate.zoom * g_penstate;
            g_renderstate.pan = g_renderstate.pan -
                                V2<int>(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
            g_renderstate.zoom = 1.0;
            g_overviewstate.overviewing = false;
        }
        return;
    }

    if (g_panstate.panning) {
        g_renderstate.pan = g_panstate.startpan +
                            PAN_FACTOR * (g_panstate.startpenpos - g_penstate);
        return;
    }

    // went from down to hovering of pen.
    if (g_curvestate.is_down && !g_colorstate.is_eraser &&
        !(EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
        g_curvestate.is_down = false;
        return;
    }

    // pressing down, not with eraser.
    if ((EasyTab->Buttons & EasyTab_Buttons_Pen_Touch) &&
        !g_colorstate.is_eraser) {
        if (!g_curvestate.is_down) {
            g_curvestate.is_down = true;
            g_segments.push_back(Segment());
            g_curvestate.seg_guid = g_segments.size() - 1;
            g_segments[g_curvestate.seg_guid].color =
                g_palette[g_colorstate.colorix];
            g_commander.start_new_command();
            return;
        }

        const V2<int> cur = g_renderstate.pan + g_penstate;
        const Color color = g_palette[g_colorstate.colorix];
        Segment &s = g_segments[g_curvestate.seg_guid];
        g_overviewstate.minPos.x = min<int>(g_overviewstate.minPos.x, cur.x);
        g_overviewstate.minPos.y = min<int>(g_overviewstate.minPos.y, cur.y);

        g_overviewstate.maxPos.x = max<int>(g_overviewstate.maxPos.x, cur.x);
        g_overviewstate.maxPos.y = max<int>(g_overviewstate.maxPos.y, cur.y);

        s.points.push_back(cur);
        s.visible.push_back(true);
        const int point_guid = s.points.size() - 1;
        SegPointGuid v(g_curvestate.seg_guid, point_guid);
        add_to_spatial_hash(v);
        g_commander.add_to_command(v);
        return;
    }

    // choosing a color.
    if (g_penstate.y >= SCREEN_HEIGHT - PALETTE_HEIGHT()) {
        int ix = g_penstate.x / PALETTE_WIDTH();
        if (ix == 0 && !g_colorstate.is_eraser) {
            g_colorstate.is_eraser = true;
            g_colorstate.eraser_radius = MIN_ERASER_RADIUS;
        }

        if (ix != 0 &&
            (ix - 1 != g_colorstate.colorix || g_colorstate.is_eraser)) {
            g_colorstate.is_eraser = false;
            g_colorstate.colorix = ix - 1;
            g_colorstate.eraser_radius = -1;
        }
        assert(g_colorstate.colorix >= 0);
        assert(g_colorstate.colorix < g_palette.size());
        return;
    }

    // is drawing eraser.
    if (g_colorstate.is_eraser &&
        (EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
        if (!g_curvestate.is_down) {
            g_curvestate.is_down = true;
            g_commander.start_new_command();
        }

        g_colorstate.eraser_radius =
            MIN_ERASER_RADIUS +
            (EasyTab->Pressure[p] * (MAX_ERASER_RADIUS - MIN_ERASER_RADIUS));

        const int startx =
            g_renderstate.pan.x + g_penstate.x - g_colorstate.eraser_radius;
        const int starty =
            g_renderstate.pan.y + g_penstate.y - g_colorstate.eraser_radius;
        const int endx = startx + 2 * g_colorstate.eraser_radius;
        const int endy = starty + 2 * g_colorstate.eraser_radius;

        for (int xix = startx / HASH_CELL_SZ - 1;
             xix <= endx / HASH_CELL_SZ + 1; ++xix) {
            for (int yix = starty / HASH_CELL_SZ - 1;
                 yix <= endy / HASH_CELL_SZ + 1; ++yix) {
                auto it = g_spatial_hash.find(make_pair(xix, yix));
                if (it == g_spatial_hash.end()) {
                    continue;
                }
                unordered_set<SegPointGuid, hash_spatial_hash_value> &bucket =
                    it->second;
                vector<SegPointGuid> to_erase;
                for (SegPointGuid v : bucket) {
                    Segment &s = g_segments[v.seg_guid];
                    assert(v.point_guid < s.points.size());
                    if (s.visible[v.point_guid] == false) {
                        continue;
                    }
                    const V2<int> delta =
                        g_renderstate.pan + g_penstate - s.points[v.point_guid];
                    // eraser has some radius without pressing.
                    // With pressing, becomes bigger.
                    if (delta.lensq() <= g_colorstate.eraser_radius *
                                             g_colorstate.eraser_radius) {
                        to_erase.push_back(v);
                        g_commander.add_to_command(v);
                        s.visible[v.point_guid] = false;
                    }
                }
                for (auto e : to_erase) {
                    remove_from_spatial_hash(e);
                }
            }
        }
        return;
    }  // end if(is_eraser )

    // not erasing / hovering eraser
    if (g_colorstate.is_eraser &&
        !(EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
        g_curvestate.is_down = false;
        return;
    }
}

// return true if we should quit.
bool handle_event(SDL_SysWMinfo sysinfo, SDL_GLContext gl_context,
                  SDL_Event &event) {
    if (event.type == SDL_QUIT) {
        return true;
    }

    if (event.type == SDL_WINDOWEVENT &&
        event.window.event == SDL_WINDOWEVENT_RESIZED) {
        SCREEN_WIDTH = event.window.data1;
        SCREEN_HEIGHT = event.window.data2;
        vg_init(sysinfo, gl_context, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else if (event.type == SDL_SYSWMEVENT) {
        EasyTabResult res =
            EasyTab_HandleEvent(&event.syswm.msg->msg.x11.event);
        if (res != EASYTAB_OK) {
            return false;
        }
        assert(res == EASYTAB_OK);
        if (EasyTab->NumPackets > 1) {
            std::cerr << "- NumPackets: " << EasyTab->NumPackets << "\n";
        }

        for (int p = 0; p < EasyTab->NumPackets; ++p) {
            handle_packet(p);
        }  // end loop over packets
    } else if (event.type == SDL_KEYDOWN) {
        cerr << "keydown: " << SDL_GetKeyName(event.key.keysym.sym) << "\n";
        if (event.key.keysym.sym == SDLK_q) {
            if (g_curvestate.is_down) {
                return false;
            }
            // undo
            g_commander.undo();
        } else if (event.key.keysym.sym == SDLK_w) {
            // redo
            if (g_curvestate.is_down) {
                return false;
            }
            g_commander.redo();
        } else if (event.key.keysym.sym == SDLK_e) {
            // toggle eraser
            g_colorstate.is_eraser = !g_colorstate.is_eraser;
            g_colorstate.eraser_radius =
                g_colorstate.is_eraser ? MIN_ERASER_RADIUS : -1;
        } else if (event.key.keysym.sym == SDLK_r) {
            g_colorstate.colorix =
                (g_colorstate.colorix + 1) % g_palette.size();
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
                    float zoomout = 2.0;
                    zoomout = std::max<float>(2.0 + 
                        (g_overviewstate.maxPos.x - g_overviewstate.minPos.x) /
                            SCREEN_WIDTH,
                        zoomout);
                    zoomout = std::max<float>(
                        2.0 + (g_overviewstate.maxPos.y - g_overviewstate.minPos.y) /
                            SCREEN_HEIGHT,
                        zoomout);
                    g_renderstate.zoom = 1.0 / zoomout;
                    g_overviewstate.saved_pan = g_renderstate.pan;
                    g_renderstate.pan = g_overviewstate.minPos - V2<int>(SCREEN_WIDTH /2.0, SCREEN_HEIGHT/2.0);
                    break;
                }
                if (g_overviewstate.overviewing) {
                    g_overviewstate.overviewing = false;
                    g_renderstate.pan = g_overviewstate.saved_pan;
                    g_renderstate.zoom = 1.0;
                    break;
                }
                break;

            case SDL_BUTTON_MIDDLE:
                button_name = "middle";
                if (!g_overviewstate.overviewing) {
                    g_panstate.panning = true;
                    g_panstate.startpan = g_renderstate.pan;
                    g_panstate.startpenpos = g_penstate;
                    break;
                }
                break;
            default:
                button_name = "unk";
                break;
        }
        cerr << "mousedown: |" << button_name << "\n";
    }

    else if (event.type == SDL_WINDOWEVENT &&
             event.window.event == SDL_WINDOWEVENT_ENTER) {
        // need to repaint when window gains focus
        return false;
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
        cerr << "mousedown: |" << button_name << "\n";
    }
    return false;
}

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

    // Create a window
    SDL_Window *window = SDL_CreateWindow(
        "WARD", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH,
        SCREEN_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Log("Could not create a window: %s", SDL_GetError());
        return -1;
    }

    // https://github.com/serge-rgb/milton/blob/5056a615e41e914bc22bcc7d2b5dc763e58c7b85/src/sdl_milton.cc#L239
    // https://github.com/serge-rgb/milton/search?q=SDL_SysWMEvent
    // need to capture pen events.
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

    SDL_SysWMinfo sysinfo;
    SDL_VERSION(&sysinfo.version);
    int ok = SDL_GetWindowWMInfo(window, &sysinfo);
    assert(ok == SDL_TRUE && "unable to get SDL X11 information");
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (glewInit() != GLEW_OK) {
        printf("Could not init glew.\n");
        return -1;
    }

    vg_init(sysinfo, gl_context, SCREEN_WIDTH, SCREEN_HEIGHT);

    ok = EasyTab_Load(sysinfo.info.x11.display, sysinfo.info.x11.window);
    if (ok != EASYTAB_OK) {
        cerr << "easytab error code: |" << ok << "|\n";
    }
    assert(ok == EASYTAB_OK &&
           "PLEASE plug in your drawing tablet! [unable to load easytab]");

    std::cerr << "\t-checkpoint: " << __LINE__ << "\n";
    bool g_quit = false;
    while (!g_quit) {
        const Uint64 start_count = SDL_GetPerformanceCounter();
        // Get the next event
        SDL_Event event;
        while (!g_quit && SDL_PollEvent(&event)) {
            g_quit = handle_event(sysinfo, gl_context, event);
        }

        vg_begin_frame();
        vg_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
                     Color::RGB(240, 240, 240));
        if (!g_overviewstate.overviewing) {
            draw_grid_cr();
        }
        draw_pen_strokes_cr();
        draw_eraser_cr();
        if (!g_panstate.panning && !g_overviewstate.overviewing) {
            draw_palette();
        }
        vg_end_frame();
        SDL_GL_SwapWindow(window);

        const Uint64 end_count = SDL_GetPerformanceCounter();
        const int counts_per_second = SDL_GetPerformanceFrequency();
        const double elapsedSec =
            (end_count - start_count) / (double)counts_per_second;
        const double elapsedMS = elapsedSec * 1000.0;
        int FPS = 1.0 / elapsedSec;
        const int TARGET_FPS = 30;
        const double timeToNextFrameMs = 1000.0 / TARGET_FPS;
        printf(
            "elapsed time: %4.2f | sleeping for: %4.2f | time to "
            "next frame: %4.2f\n",
             elapsedMS,
            timeToNextFrameMs - elapsedMS, timeToNextFrameMs);

        if (timeToNextFrameMs > elapsedMS) {
            // SDL_Delay(floor(1000.0/TARGET_FPS - elapsedMS));
            // std::cout << "fps: " << FPS << " | elapsed msec: " << elapsedMS
            // << " | time to next frame ms: " << timeToNextFrameMs << "\n";
            SDL_Delay((timeToNextFrameMs - elapsedMS));
        }
    }

    // Tidy up
    EasyTab_Unload(sysinfo.info.x11.display);
    SDL_GL_DeleteContext(gl_context);

    // SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
