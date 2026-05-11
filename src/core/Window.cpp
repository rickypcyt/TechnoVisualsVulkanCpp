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
}

bool Window::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            closeRequested = true;
            return false;
        }
        
        if (event.type == SDL_WINDOWEVENT) {
            SDL_Window* sourceWindow = SDL_GetWindowFromID(event.window.windowID);
            if (sourceWindow == window &&
                (event.window.event == SDL_WINDOWEVENT_RESIZED || 
                 event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                resizePending = true;
                resizeStableFrames = 0;
            }
        }
    }
    
    handleResizeHint();
    return true;
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
        return;
    }

    uint32_t currentWidth = static_cast<uint32_t>(drawableWidth);
    uint32_t currentHeight = static_cast<uint32_t>(drawableHeight);
    
    if (currentWidth == lastDrawableWidth && currentHeight == lastDrawableHeight) {
        if (++resizeStableFrames >= RESIZE_STABILITY_THRESHOLD) {
            framebufferResized = true;
            resizePending = false;
        }
    } else {
        resizeStableFrames = 0;
        lastDrawableWidth = currentWidth;
        lastDrawableHeight = currentHeight;
    }
}

void Window::getDrawableSize(uint32_t& width, uint32_t& height) const {
    if (window) {
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        if (drawableWidth > 0 && drawableHeight > 0) {
            width = static_cast<uint32_t>(drawableWidth);
            height = static_cast<uint32_t>(drawableHeight);
        }
    }
}
