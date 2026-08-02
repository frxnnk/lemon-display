#pragma once

// ── Configuración del firmware Ferced ──
// El aparato no sabe de dónde salen los datos: sólo pide esta URL.
// Cambiar la fuente es cambiar el proxy, no reflashear.

#ifndef FEED_ENDPOINT
#define FEED_ENDPOINT "http://192.168.1.34:9110/v1/feed?n=20"
#endif

#define FEED_REFRESH_MS   600000UL   // 10 min: refresco del pool
#define FEED_ROTATE_MS     17000UL   // 17 s ≈ 3,5 ítems por minuto
#define FEED_RETRY_MIN_MS    5000UL
#define FEED_RETRY_MAX_MS   60000UL

#define FERCED_AP_SSID "Ferced-Setup"
#define FERCED_AP_PASS "ferced1234"
