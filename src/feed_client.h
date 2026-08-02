#pragma once

#include <stdint.h>

#define FEED_MAX_ITEMS   20
#define FEED_TEXT_LEN   192
#define FEED_AUTHOR_LEN  40
#define FEED_HANDLE_LEN  28
#define FEED_IMGKEY_LEN  20

// El proxy entrega la imagen ya recortada, escalada y convertida a RGB565:
// el firmware no decodifica nada, solo pinta pixeles.
#define FEED_IMG_SIDE    64
#define FEED_IMG_PIXELS  (FEED_IMG_SIDE * FEED_IMG_SIDE)

enum FeedOrigin : uint8_t {
    FEED_FROM_X,
    FEED_FROM_RSS,
    FEED_FROM_TREND,
};

struct FeedItem {
    char       text[FEED_TEXT_LEN];
    char       author[FEED_AUTHOR_LEN];
    char       handle[FEED_HANDLE_LEN];
    char       imgKey[FEED_IMGKEY_LEN];
    uint32_t   epoch;
    FeedOrigin origin;
};

enum FeedResult : uint8_t {
    FEED_UPDATED,       // llegó contenido nuevo
    FEED_STALE_CACHE,   // falló la red, se conserva lo anterior
    FEED_FAILED,        // falló y no hay nada cacheado
};

// Baja el feed y lo deja en el caché interno. Ante fallo conserva el caché
// anterior: la pantalla nunca se queda vacía por un corte de red.
FeedResult feedFetch();

uint8_t         feedCount();
const FeedItem* feedItem(uint8_t index);

// Baja los pixeles de una imagen ya resuelta por el proxy. Devuelve nullptr si
// el item no tiene imagen o si falla: nunca es motivo para no dibujar el item.
const uint16_t* feedFetchImage(const char* key);
