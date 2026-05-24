#include "Window.h"
#include <SDL2/SDL_vulkan.h>
#include <stdexcept>
#include <iostream>

Window::Window() = default;

Window::~Window() {
    destroy();
}

void Window::initSDL() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("failed to initialize SDL: ") + SDL_GetError());
    }
    sdlInitialized = true;
}

void Window::createMainWindow(const std::string& title, int width, int height) {
    window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        throw std::runtime_error(std::string("failed to create SDL window: ") + SDL_GetError());
    }
}

void Window::createUiWindow(const std::string& title, int width, int height) {
    uiWindow = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_RESIZABLE
    );

    if (!uiWindow) {
        throw std::runtime_error(std::string("failed to create ImGui SDL window: ") + SDL_GetError());
    }

    uiRenderer = SDL_CreateRenderer(uiWindow, -1, SDL_RENDERER_ACCELERATED);
    if (!uiRenderer) {
        std::cerr << "[Window] failed to create accelerated renderer: " << SDL_GetError() << ", falling back to software renderer" << std::endl;
        uiRenderer = SDL_CreateRenderer(uiWindow, -1, 0);
    }

    if (!uiRenderer) {
        SDL_DestroyWindow(uiWindow);
        uiWindow = nullptr;
        throw std::runtime_error(std::string("failed to create SDL renderer: ") + SDL_GetError());
    }

    SDL_SetRenderDrawBlendMode(uiRenderer, SDL_BLENDMODE_BLEND);
    if (SDL_RenderSetVSync(uiRenderer, 0) != 0) {
        std::cerr << "[Window] warning: failed to disable renderer vsync: " << SDL_GetError() << std::endl;
    }
}

void Window::destroy() {
    if (uiRenderer != nullptr) {
        SDL_DestroyRenderer(uiRenderer);
        uiRenderer = nullptr;
    }
    if (uiWindow != nullptr) {
        SDL_DestroyWindow(uiWindow);
        uiWindow = nullptr;
    }
    if (window != nullptr) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    if (sdlInitialized) {
        SDL_Quit();
        sdlInitialized = false;
    }
}

bool Window::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            closeRequested = true;
        }
        
        if (event.type == SDL_WINDOWEVENT) {
            SDL_Window* sourceWindow = SDL_GetWindowFromID(event.window.windowID);
            if (sourceWindow == window &&
                (event.window.event == SDL_WINDOWEVENT_RESIZED || 
                 event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                resizePending = true;
            }
        }
    }
    
    handleResizeHint();
    return !closeRequested;
}

void Window::handleResizeHint() {
    if (!resizePending) {
        return;
    }

    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);

    if (drawableWidth == 0 || drawableHeight == 0) {
        framebufferResized = false;
        resizePending = false;
        lastDrawableWidth = 0;
        lastDrawableHeight = 0;
        return;
    }

    uint32_t currentWidth = static_cast<uint32_t>(drawableWidth);
    uint32_t currentHeight = static_cast<uint32_t>(drawableHeight);
    
    if (currentWidth != lastDrawableWidth || currentHeight != lastDrawableHeight) {
        lastDrawableWidth = currentWidth;
        lastDrawableHeight = currentHeight;
        framebufferResized = true;
    }
    resizePending = false;
}

void Window::getDrawableSize(uint32_t& width, uint32_t& height) const {
    int drawableWidth = 0;
    int drawableHeight = 0;
    if (window) {
        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    }
    width = drawableWidth > 0 ? static_cast<uint32_t>(drawableWidth) : 0;
    height = drawableHeight > 0 ? static_cast<uint32_t>(drawableHeight) : 0;
}
