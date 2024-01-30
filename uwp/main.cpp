/*
 * This file is supposed to be used to build tests on platforms that require
 * the main function to be implemented in C++, which means that SDL_main's
 * implementation needs C++ and thus can't be included in test*.c
 *
 * Placed in the public domain by Daniel Gibson, 2022-12-12
 */

#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>

#include <absim.hpp>

#include <strstream>

extern "C" uint8_t game_arduboy[];
extern "C" uint32_t game_arduboy_size;

static std::unique_ptr<absim::arduboy_t> arduboy;

int main(int argc, char* argv[])
{
    SDL_DisplayMode const* mode = nullptr;
    SDL_DisplayID* displays = nullptr;
    int display_count = 0;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Event evt;
    SDL_bool keep_going = SDL_TRUE;

    arduboy = std::make_unique<absim::arduboy_t>();
    if(!arduboy)
        return 1;
    {
        std::istrstream ss((char const*)game_arduboy, (int)game_arduboy_size);
        auto error = arduboy->load_file("game.arduboy", ss);
        if(!error.empty())
            return 1;
    }

    if(SDL_Init(SDL_INIT_VIDEO) != 0) {
        return 1;
    }
    else if(nullptr == (displays = SDL_GetDisplays(&display_count)) || display_count < 1) {
        return 1;
    }
    else if(nullptr == (mode = SDL_GetCurrentDisplayMode(displays[0])))
    {
        return 1;
    }
    else if(SDL_CreateWindowAndRenderer(mode->w, mode->h, SDL_WINDOW_FULLSCREEN, &window, &renderer) != 0) {
        return 1;
    }

    SDL_free(displays);
    displays = nullptr;

    uint64_t pt = SDL_GetTicks();

    while(keep_going) {
        while(SDL_PollEvent(&evt)) {
            if((evt.type == SDL_EVENT_KEY_DOWN) && (evt.key.keysym.sym == SDLK_ESCAPE)) {
                keep_going = SDL_FALSE;
            }
        }

        uint64_t t = SDL_GetTicks();
        uint64_t dt = t - pt;
        pt = t;

        SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);
        SDL_RenderClear(renderer);



        SDL_RenderPresent(renderer);
    }

    SDL_Quit(); 
    return 0;
}