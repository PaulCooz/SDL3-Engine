#define SDL_MAIN_USE_CALLBACKS

#include "renderer.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

SDL_Window* window;
Renderer* renderer;

int SDL_AppInit(void** appstate, int argc, char** argv) {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  SDL_WindowFlags WindowFlags = Renderer::GetRequiredWindowFlags() | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
  window = SDL_CreateWindow("SDL+DX window", 800, 600, WindowFlags);
  renderer = new Renderer(window);
  SDL_ShowWindow(window);
  return 0;
}

int SDL_AppIterate(void* appstate) {
  renderer->Present();
  return 0;
}

int SDL_AppEvent(void* appstate, const SDL_Event* event) {
  if (event->type == SDL_EVENT_QUIT) {
    return 1;
  } else {
    return 0;
  }
}

void SDL_AppQuit(void* appstate) {
  delete renderer;
  SDL_DestroyWindow(window);
  SDL_Quit();
}