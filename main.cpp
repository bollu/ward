#include <iostream>
#define EASYTAB_IMPLEMENTATION
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib.h> // Every Xlib program must include this
#include <X11/extensions/XInput.h>

#include "assert.h"
#include "easytab.h"

using namespace std;
using namespace std;

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

// easytab: #device[13] = Wacom Bamboo One S Pen
int main() {

    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        cout << "Failed to initialise SDL\n";
        return -1;
    }

    // Create a window
    SDL_Window *window =
        SDL_CreateWindow("Demo Game", SDL_WINDOWPOS_UNDEFINED,
                         SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_OPENGL);
    if (window == nullptr) {
        SDL_Log("Could not create a window: %s", SDL_GetError());
        return -1;
    }

    // https://github.com/serge-rgb/milton/blob/5056a615e41e914bc22bcc7d2b5dc763e58c7b85/src/sdl_milton.cc#L239
    // https://github.com/serge-rgb/milton/search?q=SDL_SysWMEvent
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);


    SDL_SysWMinfo sysinfo; 
    SDL_VERSION(&sysinfo.version);
    int ok = SDL_GetWindowWMInfo(window, &sysinfo) ;
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
                EasyTabResult res = EasyTab_HandleEvent(&event.syswm.msg->msg.x11.event);
                std::cout << "posX: " << EasyTab->PosX << " | posY: " << EasyTab->PosY << " | pressure: " << EasyTab->Pressure << "\n";
            }
        }


        // Randomly change the colour
        Uint8 red = 0xEE;
        Uint8 green = 0xEE;
        Uint8 blue = 0xEE;

        // Fill the screen with the colour
        SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
        SDL_RenderClear(renderer);

        SDL_RenderPresent(renderer);
    }

    // Tidy up
    EasyTab_Unload(sysinfo.info.x11.display);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

