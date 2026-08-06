#pragma once

#include "display_manager.h"

#include <stdint.h>

// Motor de composicion y animacion, compartido por todas las pantallas del
// aparato.
//
// Existe porque hay UN solo sprite de 480x480 en PSRAM (460 KB: no entran dos)
// y UNA sola medicion de framerate que vale la pena creer. Cada app arma su
// propio dibujo, pero el borrado, el escalonado de entrada, la espera de VSync,
// el empuje por bandas y la telemetria son los mismos para todas.
//
// La tecnica que hay detras esta medida, no elegida a ojo: el panel refresca a
// 42,3 Hz y un frame a pantalla completa cuesta ~99 ms, asi que animar exige
// repintar solo la banda viva. Ver docs/HANDOFF.md antes de tocar los tiempos.

// Paleta Ferced en RGB565. Los grises no son grises inventados: son blanco
// con alpha sobre el canvas, como manda la identidad.
namespace FercedColors {
constexpr uint16_t CANVAS  = 0x0842;  // #0e1011
constexpr uint16_t CARD    = 0x0841;  // #0a0a0a
constexpr uint16_t SURFACE = 0x18E3;  // blanco 4% sobre canvas
constexpr uint16_t FG      = 0xFFFF;  // #ffffff
constexpr uint16_t FG_2    = 0xBDF7;  // blanco 75%
constexpr uint16_t FG_3    = 0x8C71;  // blanco 55%
constexpr uint16_t FG_4    = 0x632C;  // blanco 40%
constexpr uint16_t LINE    = 0x18E3;  // blanco 10%
constexpr uint16_t SUCCESS = 0x36F3;  // #34d399 — sólo estado
constexpr uint16_t DANGER  = 0xFB8E;  // #f87171 — sólo estado
}  // namespace FercedColors

// Duracion corta y escalonado chico: la sensacion de "caro" viene de que los
// elementos no entren todos juntos, no de que tarde.
//
// STAGGER > ENTER a proposito: estrictamente en secuencia. Con solapamiento la
// banda sucia es la union de dos elementos (~180 px) y el frame cae en 2 VSync;
// sin solapamiento es un elemento (~70 px) y entra en uno solo.
constexpr uint16_t UI_ENTER_MS   = 130;
constexpr uint16_t UI_STAGGER_MS = 140;
constexpr int      UI_RISE_PX    = 22;

// Medicion real de la ultima transicion, para no discutir el framerate a ojo.
struct UiFrameStats {
    uint16_t frames;     // frames dibujados
    uint16_t avgUs100;   // costo medio DENTRO del tick, en centenas de us
    uint16_t worstUs100; // peor frame
    // Periodo real de reloj de pared entre frames. De aca sale el FPS de
    // verdad: avgUs100 deja afuera el delay() del loop, el tactil y wifiLoop(),
    // asi que derivar el framerate de ese campo lo sobreestima como al doble.
    uint16_t periodUs100;
};
UiFrameStats uiAnimStats();

// El sprite de composicion. Publico porque cada pantalla dibuja sobre el.
extern LGFX_Sprite uiSprite;

void uiAnimSetup();
bool uiAnimReady();

// Banda vertical que toca redibujar. Fuera de ella el sprite ya tiene el
// contenido bueno del frame anterior: ni limpiar ni empujar.
struct UiBand {
    int y, h;
};

// easeOutQuint aproxima cubic-bezier(.16, 1, .3, 1), el easing de la marca:
// arranca rapido y frena largo. Barato de calcular en un MCU.
float uiAnimEase(float t);

// RGB565 no tiene alfa: los fundidos se hacen mezclando contra el fondo.
uint16_t uiAnimLerp(uint16_t fg, uint16_t bg, float t);

// Progreso (0..1) de un elemento segun su turno en el escalonado.
float uiAnimSlotT(uint32_t elapsed, uint8_t slot);

// Cuanto dura la entrada completa de `slots` elementos.
uint32_t uiAnimTotalMs(uint8_t slots);

// true si la banda toca el rango [y0, y1]. Dibujar afuera es trabajo tirado, y
// en este panel el trabajo tirado se paga en frames perdidos.
bool uiAnimTouches(const UiBand& b, int y0, int y1);

// Declara que lo que hay en el panel no lo dibujo el sprite, asi que la proxima
// transicion tiene que limpiar todo antes de empujar bandas.
//
// Queda un solo llamador: la pantalla de aprovisionamiento, que dibuja directo
// sobre tft porque el simulador no la puede correr. Todas las demas componen
// sobre el sprite, asi que lo que hay en el sprite es lo que hay en el panel y
// no hay nada que declarar. Que lo declare quien ensucia, y no quien viene
// despues, es lo que hace imposible olvidarselo.
void uiAnimInvalidate();

// Arranca el reloj de la transicion. Devuelve true si limpio la pantalla
// entera.
//
// Antes limpiaba SIEMPRE: 99 ms con el panel en negro y recien despues los
// elementos entrando de a uno. Ese parpadeo era la parte fea de la animacion, y
// ademas un tiron de 99 ms en el que el aparato no atiende el tactil.
//
// Ahora, si la pantalla anterior era de la misma app, no limpia nada: cada
// banda se limpia sola en el frame en que su elemento empieza a entrar, y el
// contenido viejo se reemplaza en ola de arriba hacia abajo. La pantalla nunca
// queda vacia. Quien llama se encarga de limpiar lo que su dibujo nuevo no vaya
// a tapar, con uiAnimClearBand().
bool uiAnimBegin();

// Limpia una franja y la empuja. Para lo que quedo huerfano de la pantalla
// anterior: renglones que sobran cuando el texto nuevo es mas corto, un pie que
// ya no va.
void uiAnimClearBand(int y, int h);

// Milisegundos desde uiAnimBegin().
uint32_t uiAnimElapsed();

// Empuja SOLO la banda indicada, sincronizando con el panel.
void uiAnimPresent(int y, int h);

// Contabiliza un frame que efectivamente pinto algo. t0 es el micros() de su
// arranque. Los frames sin banda no se cuentan: falsearian la media hacia abajo
// y taparian el costo real de los que si pintan.
void uiAnimCountFrame(uint32_t t0);

// Muestra el sprite entero, que ya tiene la pantalla nueva compuesta.
//
// Las pantallas estaticas —el selector, tareas, avisos, configuracion— dibujan
// sobre el sprite igual que las animadas y terminan aca. Antes dibujaban
// directo sobre tft, lo que dejaba el sprite con contenido que ya no estaba en
// el panel y obligaba a uiAnimInvalidate() para que la siguiente animacion no
// empujara fantasmas. Componiendo siempre sobre el sprite ese problema no
// existe: lo que hay en el sprite es lo que hay en el panel, siempre.
//
// Con la cortina pedida el revelado baja por bandas en vez de aparecer de
// golpe. Cuesta lo mismo en total —los mismos 460 KB— pero repartido en frames
// que entran en el presupuesto, asi que el aparato sigue atendiendo el tactil
// mientras pasa y el cambio de app se lee como un movimiento y no como un corte.
void uiAnimReveal();

// Pide que el proximo revelado sea con cortina. Lo llama el cambio de app, que
// es quien sabe que la pantalla entera cambia de tema; un repintado dentro de
// la misma pantalla —marcar una tarea, mover la barra de un aviso— no lo pide y
// sale instantaneo, que es lo correcto para una respuesta a un toque.
//
// La bandera vive aca y no en cada pantalla para que el que decide sea el que
// sabe: las pantallas no tienen por que enterarse de por que las estan
// dibujando.
void uiAnimCurtainOnce();

// Cierra la transicion: publica las estadisticas y las imprime por serie.
void uiAnimPublishStats();
