#pragma once

#include <SDL2/SDL.h>
#include <cstdint>
#include <string>

class Window {
public:
    Window();
    ~Window();

    // Initialization
    void initSDL();
    void createMainWindow(const std::string& title, int width, int height);
    void createUiWindow(const std::string& title, int width, int height);
    void destroy();

    // Event handling
    bool pollEvents();
    bool isCloseRequested() const { return closeRequested; }

    // Main window access
    SDL_Window* getMainWindow() const { return window; }
    
    // UI window access
    SDL_Window* getUiWindow() const { return uiWindow; }
    SDL_Renderer* getUiRenderer() const { return uiRenderer; }

    // Resize handling
    bool shouldResize() const { return framebufferResized; }
    void resetResizeFlag() { framebufferResized = false; }
    void handleResizeHint();
    
    // Drawable size queries
    void getDrawableSize(uint32_t& width, uint32_t& height) const;

private:
    SDL_Window* window = nullptr;
    SDL_Window* uiWindow = nullptr;
    SDL_Renderer* uiRenderer = nullptr;
    bool sdlInitialized = false;
    bool closeRequested = false;
    bool resizePending = false;
    bool framebufferResized = false;
    mutable uint32_t lastDrawableWidth = 0;
    mutable uint32_t lastDrawableHeight = 0;
};
