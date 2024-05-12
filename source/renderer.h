#pragma once

#include <SDL3/SDL.h>

class Renderer {
public:
  Renderer(SDL_Window* window);
  int Present();
  ~Renderer();
};
