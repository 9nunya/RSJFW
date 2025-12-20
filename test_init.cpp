#include <SDL3/SDL.h>
#include <iostream>

int main() {
    std::cout << "Init(0)..." << std::endl;
    if (SDL_Init(0) != 0) {
         std::cerr << "Init(0) failed: " << SDL_GetError() << std::endl;
         return 1;
    }
    std::cout << "Init(0) Success." << std::endl;

    std::cout << "Init(EVENTS)..." << std::endl;
    if (SDL_InitSubSystem(SDL_INIT_EVENTS) != 0) {
         std::cerr << "Init(EVENTS) failed: " << SDL_GetError() << std::endl;
    } else {
         std::cout << "Init(EVENTS) Success." << std::endl;
    }

    std::cout << "Init(VIDEO)..." << std::endl;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
         std::cerr << "Init(VIDEO) failed: " << SDL_GetError() << std::endl;
         return 1;
    }
    std::cout << "Init(VIDEO) Success." << std::endl;
    
    SDL_Quit();
    return 0;
}
