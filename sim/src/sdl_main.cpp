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
extern bool g_config;
extern int  g_progreso;
extern bool g_padel;
extern bool g_launcher;
extern bool g_desdeLauncher;
extern bool g_setup;
extern bool g_tareas;
extern bool g_avisos;
extern bool g_aviso;
extern int  g_hacia;
extern int  g_congelar;

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
        } else if (std::strcmp(argv[i], "--config") == 0) {
            // Por linea de comandos y no con la tecla 'c': Panel_sdl corre su
            // propio bucle de eventos y se come las teclas antes que loop(),
            // asi que el teclado no sirve para capturar sin manos.
            g_config = true;
        } else if (std::strcmp(argv[i], "--padel") == 0) {
            g_padel = true;
        } else if (std::strcmp(argv[i], "--launcher") == 0) {
            g_launcher = true;
        } else if (std::strcmp(argv[i], "--desde-launcher") == 0) {
            g_desdeLauncher = true;
        } else if (std::strcmp(argv[i], "--setup") == 0) {
            g_setup = true;
        } else if (std::strcmp(argv[i], "--tareas") == 0) {
            g_tareas = true;
        } else if (std::strcmp(argv[i], "--avisos") == 0) {
            g_avisos = true;
        } else if (std::strcmp(argv[i], "--aviso") == 0) {
            g_aviso = true;
        } else if (std::strncmp(argv[i], "--hacia=", 8) == 0) {
            g_hacia = std::atoi(argv[i] + 8);
        } else if (std::strncmp(argv[i], "--congelar=", 11) == 0) {
            g_congelar = std::atoi(argv[i] + 11);
        } else if (std::strncmp(argv[i], "--progreso=", 11) == 0) {
            // Congela la franja de estado del OTA en un porcentaje. Sin esto no
            // hay forma de fotografiar una descarga: dura lo que dura la red.
            g_progreso = std::atoi(argv[i] + 11);
            g_config = true;
        }
    }
    return lgfx::Panel_sdl::main(user_func);
}

#endif
