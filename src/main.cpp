#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <csignal>
#include <iostream>

#ifdef __linux__
#include <execinfo.h>
#include <unistd.h>
#endif

#include "app/Application.h"

// Signal handler for crashes
void crash_handler(int sig) {
#ifdef __linux__
    void* array[10];
    size_t size = backtrace(array, 10);

    std::cerr << "\n[CRASH] Signal " << sig << " caught!" << std::endl;
    std::cerr << "[CRASH] Backtrace:" << std::endl;
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    std::cerr << std::endl;
#else
    std::cerr << "\n[CRASH] Signal " << sig << " caught!" << std::endl;
#endif

    _exit(1);
}

int main() {
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE, crash_handler);

    try {
        Application app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
