// Puente entre SDL y el par setup()/loop() del firmware. Tomado del ejemplo
// oficial de LovyanGFX (examples_for_PC/PlatformIO_SDL).

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#if defined(SDL_h_)

void setup(void);
void loop(void);

static int user_func(bool* running) {
    setup();
    do {
        loop();
    } while (*running);
    return 0;
}

int main(int, char**) {
    return lgfx::Panel_sdl::main(user_func);
}

#endif
