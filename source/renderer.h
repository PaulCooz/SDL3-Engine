#pragma once

#include <SDL3/SDL.h>

class Renderer {
public:
  static SDL_WindowFlags GetRequiredWindowFlags();

  Renderer(SDL_Window* window);
  int Present();
  ~Renderer();
};
