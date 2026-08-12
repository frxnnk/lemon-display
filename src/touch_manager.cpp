#include "touch_manager.h"
#include "display_manager.h"
#include "config.h"
#include <Arduino.h>
#include <cmath>

// ── Estado del toque simple ──
static bool     wasTouching    = false;
static int16_t  touchStartX    = 0;
static int16_t  touchStartY    = 0;
static int16_t  lastValidX     = 0;
static int16_t  lastValidY     = 0;
static uint32_t touchStartTime = 0;
static uint32_t lastTapTime    = 0;
static int16_t  lastTapX       = 0;
static int16_t  lastTapY       = 0;

// Cuánto se alejó el dedo del punto de partida en toda la pasada. Sirve para no
// confundir con un toque a un dedo que se fue lejos y volvió: eso no es un
// toque, aunque termine donde empezó.
static int16_t  maxTravel      = 0;

// El GT911 saltea reportes: cada tanto devuelve cero contactos en medio de un
// gesto aunque el dedo siga apoyado. Si se toma ese hueco como que se soltó, un
// solo deslizamiento se parte en dos mitades y ninguna llega al umbral, así que
// el gesto se pierde. Se exigen lecturas vacías consecutivas antes de darlo por
// terminado.
#define LIBERACIONES_PARA_SOLTAR 2
static uint8_t  vacios         = 0;

// ── Velocidad (anillo de las últimas posiciones) ──
#define VEL_SAMPLES 5
static int16_t  velY[VEL_SAMPLES];
static uint32_t velTime[VEL_SAMPLES];
static uint8_t  velIdx   = 0;
static uint8_t  velCount = 0;

// ── Pellizco ──
static bool     pinchActive    = false;
static float    pinchStartDist = 0.0f;
static float    pinchLastScale = 1.0f;
static int16_t  pinchCX        = 0;
static int16_t  pinchCY        = 0;

// ── Umbrales ──
//
// El deslizamiento se reconoce por DISTANCIA, no por duración. Antes había un
// tope de 300 ms y todo lo que tardaba más caía fuera de las cuatro ramas y
// terminaba en TOUCH_NONE: el gesto se perdía en silencio. Un deslizamiento
// hecho con calma sobre un aparato que uno sostiene con la otra mano tarda
// tranquilamente 400 ms, y por eso "a veces anda y a veces no".
static const uint32_t DEBOUNCE_TAP_MS   = 120;   // sólo entre toques
static const uint32_t LONG_PRESS_MS     = 500;
static const int16_t  SWIPE_MIN_PX      = 45;    // 9% del panel
static const int16_t  TAP_SLOP_PX       = 24;    // rodada del dedo en un toque
static const uint32_t GESTO_MAX_MS      = 2500;  // más que esto es apoyar, no gesticular
static const float    FLING_MIN_VEL     = 500.0f;
static const uint32_t FLING_MAX_MS      = 320;
static const uint32_t DOUBLE_TAP_MAX_MS = 300;
static const int16_t  DOUBLE_TAP_MAX_PX = 30;
// Un diagonal tiene que decidirse por un eje y no titubear: el dominante gana
// sólo si le saca esta proporción al otro. Si no, manda el que llegó al umbral.
static const float    EJE_DOMINANTE     = 1.2f;

static float dist2d(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    return sqrtf(dx * dx + dy * dy);
}

void touchSetup() {
    Serial.println("[Touch] GT911 listo via LovyanGFX (multitouch activo)");
}

// Clasifica una pasada terminada. Devuelve SIEMPRE un gesto: la regla es que
// ningún toque se pierda. Antes, una pasada larga y lenta no encajaba en
// ninguna rama y el aparato se hacía el desentendido, que es lo peor que puede
// hacer una pantalla táctil.
static TouchGesture clasificar(int16_t dx, int16_t dy, uint32_t duration, float velocityY) {
    const int16_t absDx = abs(dx);
    const int16_t absDy = abs(dy);

    // Apoyar el aparato o dejar el dedo puesto no es gesticular.
    if (duration > GESTO_MAX_MS) return TOUCH_NONE;

    // Un envión vertical rápido es su propio gesto: lo usa el dashboard heredado
    // y acá abre el selector igual que el deslizamiento hacia arriba.
    if (duration <= FLING_MAX_MS && fabsf(velocityY) >= FLING_MIN_VEL &&
        absDy > SWIPE_MIN_PX && absDy > absDx) {
        return velocityY < 0 ? TOUCH_FLING_UP : TOUCH_FLING_DOWN;
    }

    // Deslizamiento: alcanza con haber recorrido lo suficiente, tarde lo que
    // tarde.
    if (absDx >= SWIPE_MIN_PX || absDy >= SWIPE_MIN_PX) {
        const bool horizontalGana = absDx >= SWIPE_MIN_PX &&
                                    (absDy < SWIPE_MIN_PX || (float)absDx >= absDy * EJE_DOMINANTE);
        const bool verticalGana   = absDy >= SWIPE_MIN_PX &&
                                    (absDx < SWIPE_MIN_PX || (float)absDy >= absDx * EJE_DOMINANTE);
        if (horizontalGana) return dx > 0 ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT;
        if (verticalGana)   return dy < 0 ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
        // Diagonal parejo: gana el que más recorrió, pero se resuelve igual. No
        // hacer nada sería volver al problema original.
        if (absDx >= absDy) return dx > 0 ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT;
        return dy < 0 ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
    }

    // Quieto y sostenido.
    if (duration >= LONG_PRESS_MS) return TOUCH_LONG_PRESS;

    // El dedo se fue lejos y volvió: no llegó a ser deslizamiento, pero tampoco
    // es un toque. Tragarlo es mejor que marcar una tarea que nadie quiso tocar.
    if (maxTravel > TAP_SLOP_PX * 2) return TOUCH_NONE;

    return TOUCH_TAP;
}

TouchEvent touchLoop() {
    TouchEvent evt = { 0, 0, TOUCH_NONE, 0.0f, 1.0f, 0, 0 };

    lgfx::touch_point_t tp[5];
    int touchCount = tft.getTouch(tp, 5);
    bool isTouching = (touchCount > 0);
    uint32_t now = millis();

    // ── Pellizco ──
    if (touchCount >= 2) {
        float curDist = dist2d(tp[0].x, tp[0].y, tp[1].x, tp[1].y);
        int16_t cx = (tp[0].x + tp[1].x) / 2;
        int16_t cy = (tp[0].y + tp[1].y) / 2;

        if (!pinchActive) {
            pinchActive = true;
            pinchStartDist = curDist;
            if (pinchStartDist < 10.0f) pinchStartDist = 10.0f;
            pinchLastScale = 1.0f;
            pinchCX = cx;
            pinchCY = cy;
        } else {
            float scale = curDist / pinchStartDist;
            if (fabsf(scale - pinchLastScale) > 0.02f) {
                pinchLastScale = scale;
                pinchCX = cx;
                pinchCY = cy;
                evt.gesture = TOUCH_PINCH;
                evt.pinchScale = scale;
                evt.pinchCenterX = cx;
                evt.pinchCenterY = cy;
                evt.x = cx;
                evt.y = cy;
                return evt;
            }
        }

        lastValidX = tp[0].x;
        lastValidY = tp[0].y;
        wasTouching = true;
        vacios = 0;
        touchStartTime = now;   // que la pasada no herede la duración del pellizco
        return evt;
    }

    if (pinchActive && touchCount < 2) {
        pinchActive = false;
        if (!isTouching) wasTouching = false;
        return evt;
    }

    // ── Toque simple ──
    if (isTouching) {
        vacios = 0;

        lastValidX = tp[0].x;
        lastValidY = tp[0].y;
        // El GT911 informa coordenadas fuera de rango cada tanto.
        if (lastValidX < 0) lastValidX = 0;
        if (lastValidX >= SCREEN_W) lastValidX = SCREEN_W - 1;
        if (lastValidY < 0) lastValidY = 0;
        if (lastValidY >= SCREEN_H) lastValidY = SCREEN_H - 1;

        velY[velIdx]    = lastValidY;
        velTime[velIdx] = now;
        velIdx = (velIdx + 1) % VEL_SAMPLES;
        if (velCount < VEL_SAMPLES) velCount++;

        if (wasTouching) {
            const int16_t d = (int16_t)dist2d(touchStartX, touchStartY, lastValidX, lastValidY);
            if (d > maxTravel) maxTravel = d;
        }
    } else if (wasTouching) {
        // Puede ser un hueco del sensor y no que se haya soltado.
        if (++vacios < LIBERACIONES_PARA_SOLTAR) return evt;
    }

    if (isTouching && !wasTouching) {
        touchStartX = lastValidX;
        touchStartY = lastValidY;
        touchStartTime = now;
        wasTouching = true;
        maxTravel = 0;
        velCount = 0;
        velIdx = 0;
    } else if (!isTouching && wasTouching) {
        wasTouching = false;
        vacios = 0;

        const uint32_t duration = now - touchStartTime;
        const int16_t dx = lastValidX - touchStartX;
        const int16_t dy = lastValidY - touchStartY;

        float velocityY = 0.0f;
        if (velCount >= 2) {
            uint8_t newest = (velIdx + VEL_SAMPLES - 1) % VEL_SAMPLES;
            uint8_t oldest = (velIdx + VEL_SAMPLES - velCount) % VEL_SAMPLES;
            int32_t dtMs = (int32_t)(velTime[newest] - velTime[oldest]);
            if (dtMs > 0) {
                velocityY = (float)(velY[newest] - velY[oldest]) / ((float)dtMs / 1000.0f);
            }
        }

        evt.x = touchStartX;
        evt.y = touchStartY;
        evt.velocityY = velocityY;
        evt.gesture = clasificar(dx, dy, duration, velocityY);

        // El rebote sólo vale entre toques. Antes se aplicaba a todo, así que
        // dos deslizamientos seguidos —cambiar de app dos veces rápido— perdían
        // el segundo.
        if (evt.gesture == TOUCH_TAP) {
            if (now - lastTapTime < DEBOUNCE_TAP_MS) {
                evt.gesture = TOUCH_NONE;
            } else if ((now - lastTapTime) < DOUBLE_TAP_MAX_MS &&
                       abs(touchStartX - lastTapX) < DOUBLE_TAP_MAX_PX &&
                       abs(touchStartY - lastTapY) < DOUBLE_TAP_MAX_PX) {
                evt.gesture = TOUCH_DOUBLE_TAP;
            }
        }

        if (evt.gesture != TOUCH_NONE) {
            lastTapTime = now;
            lastTapX = touchStartX;
            lastTapY = touchStartY;
        }

        // Se registran también los descartes: cuando alguien dice "el gesto no
        // hizo nada", esta línea dice si el aparato lo vio y qué midió.
        static const char* nombres[] = {
            "NADA", "TOQUE", "PULSACION", "IZQ", "DER",
            "ARRIBA", "ABAJO", "ENVION_ARR", "ENVION_ABA",
            "PELLIZCO", "DOBLE"
        };
        Serial.printf("[Touch] %s dx=%d dy=%d dur=%lums max=%d vel=%.0f\n",
                      nombres[evt.gesture], dx, dy, (unsigned long)duration,
                      maxTravel, velocityY);
    }

    return evt;
}
