// Puente entre SDL y el par setup()/loop() del firmware. Tomado del ejemplo
// oficial de LovyanGFX (examples_for_PC/PlatformIO_SDL).

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#if defined(SDL_h_)

#include <cstdlib>
#include <cstring>

void setup(void);
void loop(void);

// Definidas en sim_main.cpp
extern int  g_startIndex;
extern bool g_still;

static int user_func(bool* running) {
    setup();
    do {
        loop();
    } while (*running);
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--still") == 0) {
            g_still = true;
        } else if (std::strncmp(argv[i], "--item=", 7) == 0) {
            g_startIndex = std::atoi(argv[i] + 7);
        }
    }
    return lgfx::Panel_sdl::main(user_func);
}

#endif
