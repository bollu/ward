#include <iostream>
#define EASYTAB_IMPLEMENTATION
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib.h>  // Every Xlib program must include this
#include <cairo/cairo.h>
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

// CONFIG
static const int MIN_PEN_RADIUS = 5;
static const int MAX_PEN_RADIUS = 10;

static const int MIN_ERASER_RADIUS = 30;
static const int MAX_ERASER_RADIUS = 100;

const int GRID_BASE_SIZE = 100;

// 1080p
int SCREEN_WIDTH = -1;
int SCREEN_HEIGHT = -1;

static const int NUM_SCREENS =
    5;	// 5x5 x screen total world size when we zoom out.

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
    Color::RGB(0, 0, 0),	// black
    Color::RGB(158, 158, 158),	// gray
    Color::RGB(233, 30, 99),	// R
    Color::RGB(76, 175, 80),	// G
    Color::RGB(33, 150, 243),	// B
    Color::RGB(255, 160, 0)	// gold
};

// leave gap in beginning for eraser.
int PALETTE_WIDTH() { return SCREEN_WIDTH / (1 + g_palette.size()); }

int PALETTE_HEIGHT() { return SCREEN_HEIGHT / 20; }

//  https://github.com/serge-rgb/milton --- has great reference code for stylus
//  + SDL
//  https://github.com/serge-rgb/milton/blob/5056a615e41e914bc22bcc7d2b5dc763e58c7b85/src/sdl_milton.cc#L239
// easytab: #device[13] = Wacom Bamboo One S Pen
// pen lower: pan
// ctrl+pen lower: zoom
// pen upper: erase
// pen lower button: middle
// pen upper button: right

// TODO: refactor with V2..
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
    return a.scale(1.0/f);
}

using ll = long long;
static ll g_max_circle_guid = 0;

using ll = long long;
struct Circle {
    ll guid;
    V2<int> posPrevEnd, posNextStart;
    V2<int> posStart, posEnd;
    int radius;
    float pressure;
    Color color;

    V2<int> pos() const { return 0.5 * (posStart + posEnd); }

    Circle(V2<int> posPrevEnd, 
            V2<int> posStart,
            V2<int> posEnd, 
            V2<int> posNextStart,
            int radius, float pressure,
	   Color color, ll guid)
	: posPrevEnd(posPrevEnd),
    posStart(posStart),
    posEnd(posEnd),
    posNextStart(posNextStart),
	  radius(radius),
	  pressure(pressure),
	  color(color),
	  guid(guid) {}
};

struct CurveState {
    V2<int> start;
    // ringbuffer
    static const int NPREV = 5;
    V2<int> prev[NPREV];
    int prevLen = 0;
    bool prevFilled = false;

    bool is_down = false;
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
    int eraser_radius = -1; // radius of the eraser based on pressure.
} g_colorstate;

struct OverviewState {
    bool overviewing = false;
    V2<int> pan;
} g_overviewstate;

struct RenderState {
    float zoom = 1;
    V2<int> pan = V2<int>(SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2);
    bool damaged = true;
} g_renderstate;

std::vector<Circle> g_circles;

// catmull rom spline
template <typename T>
T catmullrom(T p0, T p1, T p2, T p3, float t) {
    return 0.5 * ((2 * p1) + (-p0 + p2) * t +
		  (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
		  (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
};

// dubious has concatenation from stack overflow:
// https://stackoverflow.com/a/54945214/5305365
struct hash_pair_int {
    size_t operator()(const pair<int, int> &pi) const {
	return std::hash<int>()(pi.first) * 31 + std::hash<int>()(pi.second);
    };
};

// TODO: order the circle indexes by insertion time, so we paint in the right
// order.
static const int SPATIAL_HASH_CELL_SIZE = 300;
unordered_map<pair<int, int>, unordered_set<int>, hash_pair_int> g_spatial_hash;

// returns if circle was really added.
bool add_circle_to_spatial_hash(const Circle &c) {
    int sx = c.pos().x / SPATIAL_HASH_CELL_SIZE;
    int sy = c.pos().y / SPATIAL_HASH_CELL_SIZE;
    // TODO: treat circles as rects, not points.
    unordered_set<int> &bucket = g_spatial_hash[make_pair(sx, sy)];

    // this is already covered.
    for (int ix : bucket) {
	const Circle &d = g_circles[ix];
	V2<int> delta = c.pos() - d.pos();
	// int dx = c.pos.x - d.x;
	// int dy = c.pos.y - d.y;
	// int dlsq = dx * dx + dy * dy;
	if (sqrt(delta.lensq()) + c.radius < d.radius) {
	    return false;
	}
    }
    bucket.insert(c.guid);
    return true;
}

void run_command(vector<int> &cmd) {
    for (const int cix : cmd) {
	const Circle &c = g_circles[cix];
	const int sx = c.pos().x / SPATIAL_HASH_CELL_SIZE;
	const int sy = c.pos().y / SPATIAL_HASH_CELL_SIZE;
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

    const vector<int> &getCommand() const {
	assert(this->runtill == this->cmds.size() - 1);
	assert(this->cmds.size() > 0);
	return this->cmds[this->runtill];
    }

} g_commander;


// https://math.stackexchange.com/a/3770801/261373
// https://arxiv.org/pdf/2011.08232.pdf -- eqn  (15)
std::tuple<V2<float>, V2<float>, V2<float>, V2<float>>
  catmullromToBezier(V2<float> cr0,V2<float> cr1, V2<float> cr2, V2<float> cr3) {
    V2<float> b0 = cr1; // endpoints
    V2<float> b3 = cr2;  // endpoints
    const float tau = 0.9; // tension
    V2<float> b1 = cr1 + (cr2 - cr0) / (6 * tau);
    V2<float> b2 = cr2  - (cr3 - cr1) / (6 * tau);
    return {b0, b1, b2, b3};
}

void draw_pen_strokes_cr(cairo_t *cr) {
    const float zoominv = (1.0 / g_renderstate.zoom);
    const int startx = zoominv * g_renderstate.pan.x;
    const int starty = zoominv * g_renderstate.pan.y;
    const int endx = zoominv * (startx + SCREEN_WIDTH);
    const int endy = zoominv * (starty + SCREEN_HEIGHT);

    cairo_set_operator(cr, cairo_operator_t::CAIRO_OPERATOR_SOURCE);
    cairo_set_line_cap (cr, cairo_line_cap_t::CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join (cr, cairo_line_join_t::CAIRO_LINE_JOIN_ROUND);
    // https://www.cairographics.org/operators/
    //cairo_set_antialias (cr, cairo_antialias_t::CAIRO_ANTIALIAS_BEST);
    cairo_set_line_width(cr, g_renderstate.zoom * MAX_PEN_RADIUS);

    for (int xix = startx / SPATIAL_HASH_CELL_SIZE - 1;
	 xix <= endx / SPATIAL_HASH_CELL_SIZE; ++xix) {
	for (int yix = starty / SPATIAL_HASH_CELL_SIZE - 1; yix < endy; ++yix) {
	    if (!g_spatial_hash.count(make_pair(xix, yix))) {
		continue;
	    }

	    unordered_set<int> &bucket = g_spatial_hash[make_pair(xix, yix)];
	    // SDL_Rect rect;
	    for (int cix : bucket) {
		const Circle &c = g_circles[cix];
		// V2<float> cr0 = g_renderstate.zoom * (c.posPrevEnd - g_renderstate.pan).cast<float>();
		V2<float> cr1 = g_renderstate.zoom * (c.posStart - g_renderstate.pan).cast<float>();
		V2<float> cr2 = g_renderstate.zoom * (c.posEnd - g_renderstate.pan).cast<float>();
		// V2<float> cr3 = g_renderstate.zoom * (c.posNextStart - g_renderstate.pan).cast<float>();

        // V2<float> b0, b1, b2, b3;
        // std::tie(b0, b1, b2, b3) = catmullromToBezier(cr0, cr1, cr2, cr3);
        cairo_set_source_rgba(cr, c.color.r / 255.0, c.color.g / 255.0, c.color.b / 255.0, 1.0);
        // cairo_move_to(cr, b0.x, b0.y);
        // cairo_curve_to(cr, b1.x, b1.y, b2.x, b2.y, b3.x, b3.y);
        // cairo_close_path(cr);
        
        cairo_move_to(cr, cr1.x, cr1.y);
        cairo_line_to(cr, cr2.x, cr2.y);
        cairo_stroke(cr);
	    }
	}
    }
}

void draw_eraser_cr(cairo_t *cr) {
    if (!g_colorstate.is_eraser) { return; }
    const V2<float> pos = g_renderstate.zoom * (g_penstate - g_renderstate.pan).cast<float>();
    assert(g_colorstate.eraser_radius >= 0);
    const float radius = g_colorstate.eraser_radius * g_renderstate.zoom;
    
    cairo_set_operator(cr, cairo_operator_t::CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8); // eraser is white.
    cairo_arc(cr, pos.x, pos.y, radius, 0, 2.0 * M_PI);
    cairo_fill (cr);

}


// TODO: overview mode is very sluggish :(
void draw_pen_strokes(SDL_Renderer *renderer) {
    const float zoominv = (1.0 / g_renderstate.zoom);
    const int startx = zoominv * g_renderstate.pan.x;
    const int starty = zoominv * g_renderstate.pan.y;
    const int endx = zoominv * (startx + SCREEN_WIDTH);
    const int endy = zoominv * (starty + SCREEN_HEIGHT);

    for (int xix = startx / SPATIAL_HASH_CELL_SIZE - 1;
	 xix <= endx / SPATIAL_HASH_CELL_SIZE; ++xix) {
	for (int yix = starty / SPATIAL_HASH_CELL_SIZE - 1; yix < endy; ++yix) {
	    if (!g_spatial_hash.count(make_pair(xix, yix))) {
		continue;
	    }

	    unordered_set<int> &bucket = g_spatial_hash[make_pair(xix, yix)];
	    // SDL_Rect rect;
	    for (int cix : bucket) {
		const Circle &c = g_circles[cix];
		// cout << "\t- circle(x=" << c.x << " y=" << c.y << " rad=" <<
		// c.radius << ")\n";
		// rect.x = c.pos.x - c.radius;
		// rect.y = c.pos.y - c.radius;
		// rect.w = rect.h = c.radius;
		// rect.x -= g_renderstate.pan.x;
		// rect.y -= g_renderstate.pan.y;
		// rect.x *= g_renderstate.zoom;
		// rect.y *= g_renderstate.zoom;
		// rect.w *= g_renderstate.zoom;
		// rect.h *= g_renderstate.zoom;
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, c.color.r, c.color.g,
				       c.color.b, 255);
		V2<int> s =
		    g_renderstate.zoom * (c.posStart - g_renderstate.pan);
		V2<int> t = g_renderstate.zoom * (c.posEnd - g_renderstate.pan);
		SDL_RenderDrawLine(renderer, s.x, s.y, t.x, t.y);
		// SDL_RenderFillRect(renderer, &rect);
	    }
	}
    }
}

void draw_grid_cr(cairo_t *cr) {
    const int GRIDSIZE = g_renderstate.zoom * GRID_BASE_SIZE;
    const int STARTX = -1 * (GRIDSIZE + (g_renderstate.pan.x % GRIDSIZE));
    const int STARTY = -1 * (GRIDSIZE + (g_renderstate.pan.y % GRIDSIZE));

    // set grid color.

    static const int GRID_LINE_WIDTH = 2;
    cairo_set_operator(cr, cairo_operator_t::CAIRO_OPERATOR_SOURCE);
    cairo_set_line_width(cr, GRID_LINE_WIDTH);
    cairo_set_source_rgba(cr, 170. / 255.0, 170. / 255.0, 170. / 255.0, 1.0);

    for (int x = STARTX; x <= STARTX + SCREEN_WIDTH + GRIDSIZE; x += GRIDSIZE) {
        cairo_move_to(cr, x, STARTY);
        cairo_line_to(cr, x, STARTY + SCREEN_HEIGHT + 2*GRIDSIZE);
        cairo_stroke(cr);
    }

    for (int y = STARTY; y <= STARTY + SCREEN_HEIGHT + GRIDSIZE;
	 y += GRIDSIZE) {
        cairo_move_to(cr, STARTX, y);
        cairo_line_to(cr, STARTX + SCREEN_WIDTH + 2 *GRIDSIZE, y);
        cairo_stroke(cr);
    }
};


void draw_grid(SDL_Renderer *renderer) {
    const int GRIDSIZE = g_renderstate.zoom * 100;
    const int STARTX = -1 * (GRIDSIZE + (g_renderstate.pan.x % GRIDSIZE));
    const int STARTY = -1 * (GRIDSIZE + (g_renderstate.pan.y % GRIDSIZE));

    // set grid color.
    SDL_SetRenderDrawColor(renderer, 170, 170, 170, SDL_ALPHA_OPAQUE);

    for (int x = STARTX; x <= STARTX + SCREEN_WIDTH + GRIDSIZE; x += GRIDSIZE) {
	SDL_RenderDrawLine(renderer, x, STARTY, x,
			   STARTY + SCREEN_HEIGHT + 2 * GRIDSIZE);
    }

    for (int y = STARTY; y <= STARTY + SCREEN_HEIGHT + GRIDSIZE;
	 y += GRIDSIZE) {
	SDL_RenderDrawLine(renderer, STARTX, y,
			   STARTX + SCREEN_WIDTH + 2 * GRIDSIZE, y);
    }
};

void draw_palette(SDL_Renderer *renderer) {
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
	Color color = Color::RGB(255, 255, 255);  // eraser indicated by white.
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b,
			       SDL_ALPHA_OPAQUE);
	SDL_RenderFillRect(renderer, &rect);
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


SDL_Surface *g_sdl_surface = nullptr;
cairo_surface_t *g_cr_surface = nullptr;
cairo_t *g_cr = nullptr; 


void cairo_setup_surface() {
    g_sdl_surface = SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, 0x00ff0000,
            0x0000ff00, 0x000000ff, 0);
    g_cr_surface = cairo_image_surface_create_for_data( (unsigned char
                *)g_sdl_surface->pixels, CAIRO_FORMAT_RGB24, SCREEN_WIDTH,
            SCREEN_HEIGHT, g_sdl_surface->pitch);
    cairo_surface_set_device_scale(g_cr_surface, 1, 1);
    g_cr = cairo_create(g_cr_surface);


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

    // https://github.com/tsuu32/sdl2-cairo-example/blob/16a1eb41d649e1be72e9cb51860017d01b38af5e/sdl2-cairo.c
    cairo_setup_surface();



    ok = EasyTab_Load(sysinfo.info.x11.display, sysinfo.info.x11.window);
    if (ok != EASYTAB_OK) {
	cerr << "easytab error code: |" << ok << "|\n";
    }
    assert(ok == EASYTAB_OK &&
	   "PLEASE plug in your drawing tablet! [unable to load easytab]");

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

	    if (event.type == SDL_WINDOWEVENT &&
		event.window.event == SDL_WINDOWEVENT_RESIZED) {
		SCREEN_WIDTH = event.window.data1;
		SCREEN_HEIGHT = event.window.data2;
        cairo_setup_surface();
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
		    const float pressure = EasyTab->Pressure[p];
		    static const float PAN_FACTOR = 3;

		    // overview
		    if (g_overviewstate.overviewing) {
			// if tapped, move to tap location
			if (EasyTab->Buttons & EasyTab_Buttons_Pen_Touch) {
			    g_renderstate.pan =
				(1.0 / g_renderstate.zoom) * g_penstate;
			    g_renderstate.zoom = 1.0;
			    g_overviewstate.overviewing = false;
			}

			// this is in charge of all events.
			// skip events.
			continue;
		    }

		    if (g_panstate.panning) {
			g_renderstate.pan =
			    g_panstate.startpan +
			    PAN_FACTOR * (g_panstate.startpenpos - g_penstate);
			// g_renderstate.pan.x =
			//     g_panstate.startpan.x +
			//     PAN_FACTOR * (g_panstate.startpenpos.x -
			//     g_penstate.x);
			// g_renderstate.pany =
			//     g_panstate.startpany +
			//     PAN_FACTOR * (g_panstate.startpenpos.y -
			//     g_penstate.y);
			g_renderstate.damaged = true;
			continue;
		    }

		    // went from down to hovering of pen.
		    if (g_curvestate.is_down && !g_colorstate.is_eraser &&
			!(EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
			g_curvestate.is_down = false;
			continue;
		    }

		    // pressing down, not with eraser.
		    if ((EasyTab->Buttons & EasyTab_Buttons_Pen_Touch) &&
			!g_colorstate.is_eraser) {
			if (!g_curvestate.is_down) {
			    g_curvestate.start = g_penstate + g_renderstate.pan;
			    g_curvestate.is_down = true;
			    g_commander.start_new_command();
			    g_curvestate.prevFilled = false;
			    g_curvestate.prevLen = 0;
			}

			// const int dx = abs(g_penstate.x -
			// g_curvestate.prev.x); const int dy = abs(g_penstate.y
			// - g_curvestate.prev.y);

			const float t = pressure * pressure;
			const int radius =
			    int((1. - t) * (float)MIN_PEN_RADIUS +
				t * (float)MAX_PEN_RADIUS);

			const V2<int> pos = g_renderstate.pan + g_penstate;

            if (g_curvestate.prevFilled || (!g_curvestate.prevFilled && g_curvestate.prevLen >= 1)) {
			    int tip = CurveState::NPREV + g_curvestate.prevLen;
			    const V2<int> prev = g_curvestate.prev[(tip - 1) % CurveState::NPREV];
                // const int MIN_SEGMENT_LEN = 1;
                // // reject point.
                // if ((pos - prev).lensq() <=  MIN_SEGMENT_LEN * MIN_SEGMENT_LEN) { continue; }
            }

			g_curvestate.prev[g_curvestate.prevLen] =
			    g_renderstate.pan + g_penstate;
			g_curvestate.prevLen++;

			if (g_curvestate.prevLen >= CurveState::NPREV) {
			    g_curvestate.prevFilled = true;
			    g_curvestate.prevLen = 0;
			}

			if (g_curvestate.prevFilled) {
			    const int tip = CurveState::NPREV + g_curvestate.prevLen;
			    const V2<int> p0 =
				g_curvestate.prev[(tip - 1) % CurveState::NPREV];
			    const V2<int> p1 =
				g_curvestate.prev[(tip - 2) % CurveState::NPREV];
			    const V2<int> p2 =
				g_curvestate.prev[(tip - 3) % CurveState::NPREV];
			    const V2<int> p3 =
				g_curvestate.prev[(tip - 4) % CurveState::NPREV];

                const Color color =
                    g_palette[g_colorstate.colorix];
				const Circle circle = Circle(
                    p0, p1, p2, p3,
				    radius, pressure, color, g_max_circle_guid);
                cout << "Added circle!\n";

				if (add_circle_to_spatial_hash(circle)) {
				    g_circles.push_back(circle);
				    g_commander.add_to_command(circle);
				    g_max_circle_guid++;
				    g_renderstate.damaged = true;
				}

			    // const int NUM_INTERPOLANTS = 1;
			    // for (int k = 0; k < NUM_INTERPOLANTS; ++k) {
				// V2<float> posStart = catmullrom<V2<float>>(
				//     p0, p1, p2, p3,
				//     (float)k / (float)NUM_INTERPOLANTS);
				// V2<float> posEnd = catmullrom<V2<float>>(
				//     p0, p1, p2, p3,
				//     (float)(k + 1) / (float)NUM_INTERPOLANTS);
				// const Color color =
				//     g_palette[g_colorstate.colorix];
				// assert(g_max_circle_guid < (1ll << 62) &&
				//        "too many circles!");
				// const Circle circle = Circle(
                //     p0, p1, p2, p3,
				//     radius, pressure, color, g_max_circle_guid);

				// if (add_circle_to_spatial_hash(circle)) {
				//     g_circles.push_back(circle);
				//     g_commander.add_to_command(circle);
				//     g_max_circle_guid++;
				//     g_renderstate.damaged = true;
				// }
			    // }
			}

			// const int NUM_INTERPOLANTS =
			//     min<int>(max<int>(1, sqrt(dlsq)), 10);
			// for (int k = 0; k <= NUM_INTERPOLANTS; k++) {
			//     int x = lerp(double(k) / NUM_INTERPOLANTS,
			//                  g_curvestate.prevx,
			//                  g_renderstate.panx + g_penstate.x);
			//     int y = lerp(double(k) / NUM_INTERPOLANTS,
			//                  g_curvestate.prevy,
			//                  g_renderstate.pany + g_penstate.y);
			//     const Color color =
			//     g_palette[g_colorstate.colorix];
			//     assert(g_max_circle_guid < (1ll << 62) &&
			//            "too many circles!");

			//     // need post ++ for guid, as we need firt guid to
			//     be
			//     // zero since we push it  back into a vector.
			//     const Circle circle =
			//         Circle(x, y, radius, color,
			//         g_max_circle_guid);
			//     // add circle to the vector to keeps the GUIDs
			//     // correct.

			//     // if we don't need this circle, continue, don't
			//     add
			//     // it to the command. Such a circle is the price
			//     we
			//     // pay...
			//     // TODO: create code that cleans up such
			//     // unreferenced circles!
			//     if (add_circle_to_spatial_hash(circle)) {
			//         g_circles.push_back(circle);
			//         g_commander.add_to_command(circle);
			//         g_max_circle_guid++;
			//         g_renderstate.damaged = true;
			//     }
			// }
			continue;
		    }

		    // choosing a color.
		    if (g_penstate.y >= SCREEN_HEIGHT - PALETTE_HEIGHT()) {
			// std::cerr << "COLOR PICKING\n";
			int ix = g_penstate.x / PALETTE_WIDTH();
			if (ix == 0 && !g_colorstate.is_eraser) {
			    g_colorstate.is_eraser = true;
                g_colorstate.eraser_radius = MIN_ERASER_RADIUS;
			    g_renderstate.damaged = true;
			}

			if (ix != 0 && (ix - 1 != g_colorstate.colorix ||
					g_colorstate.is_eraser)) {
			    g_colorstate.is_eraser = false;
			    g_colorstate.colorix = ix - 1;
                g_colorstate.eraser_radius = -1;
			    g_renderstate.damaged = true;
			}
			assert(g_colorstate.colorix >= 0);
			assert(g_colorstate.colorix < g_palette.size());
			continue;
		    }

		    // is drawing eraser.
		    if (g_colorstate.is_eraser &&
			(EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
			if (!g_curvestate.is_down) {
			    g_curvestate.is_down = true;
			    g_commander.start_new_command();
			}
            
			g_colorstate.eraser_radius  = MIN_ERASER_RADIUS + (EasyTab->Pressure[p] * (MAX_ERASER_RADIUS - MIN_ERASER_RADIUS));

			const int startx =
			    g_renderstate.pan.x + g_penstate.x - g_colorstate.eraser_radius;
			const int starty =
			    g_renderstate.pan.y + g_penstate.y - g_colorstate.eraser_radius;
			const int endx = startx + 2 * g_colorstate.eraser_radius;
			const int endy = starty + 2 * g_colorstate.eraser_radius;

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
				    const V2<int> delta = g_renderstate.pan +
							  g_penstate - c.pos();
				    // eraser has some radius without pressing.
				    // With pressing, becomes bigger.
				    if (delta.lensq() <= g_colorstate.eraser_radius * g_colorstate.eraser_radius) {
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

		    // not erasing / hovering eraser
		    if (g_curvestate.is_down && g_colorstate.is_eraser &&
			!(EasyTab->Buttons & EasyTab_Buttons_Pen_Touch)) {
			g_curvestate.is_down = false;
			continue;
		    }
		}  // end loop over packets
	    } else if (event.type == SDL_KEYDOWN) {
		cerr << "keydown: " << SDL_GetKeyName(event.key.keysym.sym)
		     << "\n";
		if (event.key.keysym.sym == SDLK_q) {
		    if (g_curvestate.is_down) {
			continue;
		    }
		    // undo
		    g_renderstate.damaged = true;
		    g_commander.undo();
		} else if (event.key.keysym.sym == SDLK_w) {
		    // redo
		    if (g_curvestate.is_down) {
			continue;
		    }
		    g_renderstate.damaged = true;
		    g_commander.redo();
		} else if (event.key.keysym.sym == SDLK_e) {
		    // toggle eraser
		    g_renderstate.damaged = true;
		    g_colorstate.is_eraser = !g_colorstate.is_eraser;
            g_colorstate.eraser_radius = g_colorstate.is_eraser ? MIN_ERASER_RADIUS : -1;
		} else if (event.key.keysym.sym == SDLK_r) {
		    g_colorstate.colorix =
			(g_colorstate.colorix + 1) % g_palette.size();
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
			    g_overviewstate.pan = g_renderstate.pan;
			    g_renderstate.pan.x = g_renderstate.pan.y = 0;
			    break;
			}
			if (g_overviewstate.overviewing) {
			    g_overviewstate.overviewing = false;
			    g_renderstate.damaged = true;
			    g_renderstate.zoom = 1.0;
			    g_renderstate.pan = g_overviewstate.pan;
			    break;
			}
			break;

		    case SDL_BUTTON_MIDDLE:
			button_name = "middle";
			if (!g_overviewstate.overviewing) {
			    g_panstate.panning = true;
			    g_renderstate.damaged = true;
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
		g_renderstate.damaged =
		    true;  // need to repaint when window gains focus
		continue;
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

    // eraser always damages because we need to redraw eraser circle.
    g_renderstate.damaged |= g_colorstate.is_eraser;
	// for logging into the console.
	const bool logging_was_damaged = g_renderstate.damaged;
	if (g_renderstate.damaged) {
	    g_renderstate.damaged = false;
	    Uint8 opaque = 255;
	    // SDL_SetRenderDrawColor(renderer, g_draw_background_color.r,
		// 		   g_draw_background_color.g,
		// 		   g_draw_background_color.b, opaque);
	    // SDL_RenderClear(renderer);

        cairo_set_operator(g_cr, cairo_operator_t::CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(g_cr, 0.9, 0.9, 0.9, 1.0);
        cairo_rectangle(g_cr, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        cairo_fill(g_cr);

        draw_grid_cr(g_cr);
	    draw_pen_strokes_cr(g_cr);
        draw_eraser_cr(g_cr);
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, g_sdl_surface);
        SDL_RenderCopy(renderer, texture, NULL, NULL);



	    // draw_grid(renderer);
	    // draw_pen_strokes(renderer);
	    if (!g_panstate.panning && !g_overviewstate.overviewing) {
            draw_palette(renderer);
	    }
	    SDL_RenderPresent(renderer);
        SDL_DestroyTexture(texture);

	}

	const Uint64 end_count = SDL_GetPerformanceCounter();
	const int counts_per_second = SDL_GetPerformanceFrequency();
	const double elapsedSec =
	    (end_count - start_count) / (double)counts_per_second;
	const double elapsedMS = elapsedSec * 1000.0;
	int FPS = 1.0 / elapsedSec;
	const int TARGET_FPS = 60;
	const double timeToNextFrameMs = 1000.0 / TARGET_FPS;
	if (timeToNextFrameMs > elapsedMS) {
	    // SDL_Delay(floor(1000.0/TARGET_FPS - elapsedMS));
	    printf(
		"%20s | elapsed time: %4.2f | sleeping for: %4.2f | time to "
		"next frame: %4.2f\n",
		(logging_was_damaged ? "DAMAGED" : ""), elapsedMS,
		timeToNextFrameMs - elapsedMS, timeToNextFrameMs);
	    // std::cout << "fps: " << FPS << " | elapsed msec: " << elapsedMS
	    // << " | time to next frame ms: " << timeToNextFrameMs << "\n";
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

