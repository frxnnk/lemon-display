#pragma once

#include <stdint.h>

#define FEED_MAX_ITEMS   20
#define FEED_TEXT_LEN   192
#define FEED_AUTHOR_LEN  40
#define FEED_HANDLE_LEN  28

enum FeedOrigin : uint8_t {
    FEED_FROM_X,
    FEED_FROM_RSS,
    FEED_FROM_TREND,
};

struct FeedItem {
    char       text[FEED_TEXT_LEN];
    char       author[FEED_AUTHOR_LEN];
    char       handle[FEED_HANDLE_LEN];
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
