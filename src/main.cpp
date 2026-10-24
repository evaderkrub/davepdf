#ifndef SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#endif
#include "platform/desktop.h"

int main(int argc, char** argv) {
    return davepdf::platform::run(argc, argv);
}
