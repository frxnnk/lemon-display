#pragma once

#include "ui_anim.h"

#include <stdint.h>

// Chasis comun a todas las pantallas del aparato.
//
// Antes cada pantalla resolvia su encabezado y su pie por su cuenta: una tenia
// pastilla, otra regla, otra nada, y el pie era una raya gris en unas y aire en
// otras. Se veian de aparatos distintos. Aca vive lo que las hace una sola:
//
//   CEJILLA en versalitas                                    [marca]
//   ──────────────────────────────────────────── regla
//   (cuerpo, cosa de cada pantalla)
//   ════════════════════════════════════════════ riel
//
// La regla de la casa —el color de acento es solo para estado— sigue en pie. El
// espectro del riel no es acento: es la marca. Es la unica cosa cromatica de
// todo el sistema y aparece siempre en el mismo lugar, asi que no compite con
// SUCCESS ni con DANGER, que siguen siendo los unicos colores que significan
// algo.

namespace FercedChrome {

// Alturas del chasis. Las pantallas se cuelgan de estas, no de numeros sueltos.
constexpr int MARGIN    = 28;
constexpr int EYEBROW_Y = 34;   // linea media de la cejilla
constexpr int RULE_Y    = 76;   // la regla que cierra el encabezado
constexpr int BODY_Y    = 104;  // primer pixel util del cuerpo
constexpr int RAIL_Y    = 452;  // riel
constexpr int RAIL_H    = 2;

}  // namespace FercedChrome

// El espectro del isotipo —verde, cyan, azul— leido de arriba hacia abajo y
// tendido de izquierda a derecha. t va de 0 a 1.
uint16_t fercedSpectrum(float t);

// Riel del pie. `filled01` es cuanto va cumplido: en noticias, lo que falta
// para el proximo titular; en tareas, lo hecho sobre el total. Con 0 queda solo
// la regla apagada, que es lo correcto en una pantalla sin progreso.
//
// `alpha` mezcla contra el canvas para que el riel pueda entrar con el resto.
void uiRail(LGFX_Sprite& g, int y, float filled01, float alpha = 1.0f);

// El mismo riel pero con la columna a medida, para la tarjeta que interrumpe:
// como es un pop-up con su propio margen, su riel no puede ir de margen a
// margen de la pantalla.
void uiRailEn(LGFX_Sprite& g, int x, int w, int y, float filled01,
              float alpha = 1.0f);

// Cejilla + regla. `izq` va en versalitas y `der` alineado a la derecha, contra
// la marca. Cualquiera de los dos puede ser nullptr.
void uiEyebrow(LGFX_Sprite& g, const char* izq, const char* der, float alpha);

// La marca chica, monocroma, arriba a la derecha. Es la que va en las pantallas
// de contenido: ahi el protagonista es el titular, no el aparato.
void uiMark(LGFX_Sprite& g, int x, int y, float alpha);

// La marca grande y a color, para las pantallas donde el aparato habla de si
// mismo —el selector y configuracion—. 26x64, generada del logo.svg del sitio.
constexpr int UI_MARK_C_W = 26;
constexpr int UI_MARK_C_H = 64;
void uiMarkColor(LGFX_Sprite& g, int x, int y);

// ── Marcas de los agentes ──
//
// Los avisos los manda quien quiera contra /api/notify, asi que `src` es texto
// libre. Los dos que existen hoy —claude y codex— tienen marca propia;
// cualquier otro cae en la inicial dentro de la misma burbuja, que es lo que
// hace que una fuente nueva no rompa nada.
//
// Se guarda la FORMA y no los colores: un byte de alfa por pixel, generado con
// tools/marca_agente.py. El color lo pone el firmware al dibujar, y por eso la
// misma marca sirve encendida cuando el aviso esta sin leer y atenuada cuando
// ya se leyo, sin guardar dos copias.
struct MarcaAgente {
    const uint8_t* alfa20;   // fila de la lista
    const uint8_t* alfa34;   // burbuja de la lista
    const uint8_t* alfa56;   // el personaje que asoma al llegar un aviso
    uint16_t       color;
};

// nullptr si la fuente no tiene marca: quien llama dibuja la inicial.
const MarcaAgente* marcaDeAgente(const char* src);

// Pinta la mascara teñida, mezclando contra `fondo` porque RGB565 no tiene alfa.
// `intensidad` la atenua entera, para los avisos ya leidos.
void uiMarcaAgente(LGFX_Sprite& g, const uint8_t* alfa, int lado, int x, int y,
                   uint16_t color, uint16_t fondo, float intensidad);

// Burbuja de chat: rectangulo redondeado con la cola abajo a la izquierda. Con
// `borde` igual a `relleno` sale maciza, y con `relleno` en el canvas sale de
// contorno; asi la misma funcion dibuja el aviso sin leer y el leido.
void uiBurbuja(LGFX_Sprite& g, int x, int y, int w, int h,
               uint16_t borde, uint16_t relleno);
constexpr int UI_BURBUJA_COLA = 9;   // cuanto baja la cola por debajo del cuerpo

// Recorta las esquinas de un rectangulo ya pintado, tapandolas con el canvas.
// Sirve para redondear una imagen empujada con pushImage, que no tiene forma de
// dibujarse redondeada.
void uiRoundCorners(LGFX_Sprite& g, int x, int y, int w, int h, int r,
                    uint16_t fondo);

// Escribe `texto` recortado a `maxW` con puntos suspensivos. Devuelve el ancho
// real dibujado, que sirve para tachar o subrayar sin adivinar.
int uiTextoRecortado(LGFX_Sprite& g, const char* texto, int x, int y, int maxW);
