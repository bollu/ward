#include <iostream>
#include "easytab.h"
#include <SDL2/SDL.h>

//  https://gist.githubusercontent.com/adrian-gierakowski/52a243291130a2e7eb50/raw/077ed0dc8d80a2297bd2f2115aa6a923b081a56a/sfml_skia.cpp
using namespace std;
using namespace std;

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

int main() {

    if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
    {
        cout << "Failed to initialise SDL\n";
        return -1;
    }

    // Create a window
    SDL_Window *window = SDL_CreateWindow("Demo Game",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          640,
                                          480,
                                          SDL_WINDOW_OPENGL);
    if (window == nullptr)
    {
        SDL_Log("Could not create a window: %s", SDL_GetError());
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr)
    {
        SDL_Log("Could not create a renderer: %s", SDL_GetError());
        return -1;
    }

    while (true)
    {
        // Get the next event
        SDL_Event event;
        if (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                break;
            }
        }

        // Randomly change the colour
        Uint8 red = 0xEE; rand() % 255;
        Uint8 green = 0xEE;; rand() % 255;
        Uint8 blue = 0xEE; rand() % 255;

        // Fill the screen with the colour
        SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
        SDL_RenderClear(renderer);

        SDL_RenderPresent(renderer);
    }

    // Tidy up
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}


