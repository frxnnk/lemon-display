#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "apps.h"
#include "audio_manager.h"
#include "colors.h"
#include "display_manager.h"
#include "feed_client.h"
#include "ferced_config.h"
#include "notif_store.h"
#include "nvs_storage.h"
#include "ota_ferced.h"
#include "padel_client.h"
#include "time_manager.h"
#include "todo_store.h"
#include "touch_manager.h"
#include "ui_config.h"
#include "ui_ferced.h"
#include "ui_launcher.h"
#include "ui_notif.h"
#include "ui_padel.h"
#include "ui_todo.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "wifi_provision.h"

enum AppPhase : uint8_t {
    PHASE_PROVISION,
    PHASE_CONNECTING,
    PHASE_RUNNING,
    PHASE_CONFIG,
    PHASE_LAUNCHER,
    PHASE_AVISO,
    PHASE_TODO_REMINDER,
};

static AppPhase s_phase = PHASE_PROVISION;
static AppId    s_app = APP_NOTICIAS;
static uint32_t s_lastRotate = 0;

// -- Estado de noticias --
static uint8_t  s_index = 0;
static uint32_t s_lastFetch = 0;
static uint32_t s_retryMs = FEED_RETRY_MIN_MS;
static uint32_t s_nextRetry = 0;
static bool     s_offline = false;

// -- Estado de padel --
static uint8_t  s_padelIndex = 0;
static uint32_t s_padelLastFetch = 0;
static bool     s_padelTried = false;

// -- Estado de tareas --
// La lista cambia desde el servidor web, que corre en la tarea de AsyncTCP. En
// vez de dejar que esa tarea toque la pantalla —dos tareas dibujando sobre el
// mismo panel es una carrera— se compara la revisión y repinta el loop.
static uint32_t s_todoRev = 0;
static uint32_t s_reminderTaskId = 0;
static uint32_t s_lastReminderCheck = 0;

// -- Estado de avisos --
// Cuánto queda un aviso en pantalla antes de cerrarse solo. Un cuarto de minuto
// alcanza para levantar la vista y leerlo, y no tanto como para tapar la
// pantalla si uno no está.
// Veinte segundos. La cuenta cambio dos veces y por buenos motivos: eran 25
// cuando el aviso tapaba la pantalla entera, o sea 25 segundos sin poder ver
// nada, y ahi bajaron a 9. Pero ahora es un cartel en la esquina que deja ver
// lo que habia atras, asi que ya no cuesta nada tenerlo puesto: lo que costaba
// era la interrupcion, no el tiempo. Con 9 se iba antes de que levantaras la
// vista.
//
// Se puede cerrar antes con la cruz, asi que este numero es un techo, no una
// espera obligada.
#define AVISO_MS 20000UL
static uint32_t s_notifRev = 0;
static uint32_t s_avisoDesde = 0;

#define WIFI_RETRY_MIN_MS  5000UL
#define WIFI_RETRY_MAX_MS 60000UL
static uint32_t s_wifiRetryMs = WIFI_RETRY_MIN_MS;
static uint32_t s_nextWifiRetry = 0;

// NTP deja el reloj del sistema en epoch UTC, igual que el campo ts del feed.
// Sin sincronizar devuelve 0 y la UI cae en "recien".
static uint32_t nowEpoch() {
    if (!timeReady()) return 0;
    return (uint32_t)time(nullptr);
}

static void startProvisioning() {
    s_phase = PHASE_PROVISION;
    // El portal cautivo usa el mismo puerto 80 que el editor de tareas.
    webServerStop();
    provisionStart();
    provisionDrawQR();
}

// -- Red de rollback ---------------------------------------------------------
// Con CONFIG_APP_ROLLBACK_ENABLE el bootloader deja la particion recien escrita
// en PENDING_VERIFY: si nadie la confirma, el proximo arranque vuelve sola a la
// anterior.
//
// Arduino la confirma apenas bootea, dentro de initArduino(), lo que anularia
// la red entera. Esa llamada esta envuelta en if(!verifyRollbackLater()), y
// verifyRollbackLater() es una funcion debil que por defecto devuelve false.
// Definiendola fuerte le decimos que se abstenga.
extern "C" bool verifyRollbackLater() { return true; }

static bool s_arranqueConfirmado = false;

// Se confirma cuando el firmware DEMUESTRA que sirve, no cuando arranca. Un
// binario que compila y bootea pero se queda sin red es justo el caso que el
// rollback tiene que atrapar: sin conexion no hay forma de arreglarlo salvo por
// USB, que es de lo que este mecanismo existe para escapar.
static void confirmarArranque() {
    if (s_arranqueConfirmado) return;
    s_arranqueConfirmado = true;

    const esp_partition_t* corriendo = esp_ota_get_running_partition();
    esp_ota_img_states_t estado;
    if (esp_ota_get_state_partition(corriendo, &estado) != ESP_OK) return;
    if (estado != ESP_OTA_IMG_PENDING_VERIFY) return;

    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[OTA] arranque confirmado, la particion nueva queda fija");
}

// -- Noticias ----------------------------------------------------------------

static void showNoticias() {
    const uint8_t total = feedCount();
    if (total == 0) {
        uiFercedShowStatus("Sin contenido", "No llego nada del feed. Tocar para reintentar.");
        return;
    }
    if (s_index >= total) s_index = 0;
    uiFercedShowItem(feedItem(s_index), s_index, total, s_offline);
}

// Devuelve true si bajo contenido nuevo. `forzado` saltea el TTL del pool en el
// proxy: sin eso, apretar el boton dentro de los 10 minutos del cache devolvia
// exactamente los mismos items y en pantalla no pasaba nada.
static bool refreshFeed(bool forzado = false) {
    const UiFrameStats st = uiAnimStats();
    const FeedResult r = feedFetch(st.frames, st.avgUs100, st.worstUs100, forzado);
    s_lastFetch = millis();

    if (r == FEED_UPDATED) {
        // Primer feed exitoso desde el arranque: recien aca el firmware probo
        // que tiene red y sirve.
        confirmarArranque();
        s_offline = false;
        s_retryMs = FEED_RETRY_MIN_MS;
        s_nextRetry = 0;
        s_index = 0;
        if (s_phase == PHASE_RUNNING && s_app == APP_NOTICIAS) {
            showNoticias();
            s_lastRotate = millis();
        }
        return true;
    }

    // Falla de red: se conserva el pool y se reintenta con backoff, en vez de
    // dejar la pantalla vacia.
    s_offline = true;
    s_nextRetry = millis() + s_retryMs;
    s_retryMs = s_retryMs * 2 > FEED_RETRY_MAX_MS ? FEED_RETRY_MAX_MS : s_retryMs * 2;
    if (s_phase == PHASE_RUNNING && s_app == APP_NOTICIAS) showNoticias();
    return false;
}

// -- Padel -------------------------------------------------------------------

static void showPadel() {
    const uint8_t total = padelScreenCount();
    if (total == 0) {
        uiPadelShowStatus("Padel", s_padelTried
                                       ? "No llego nada del circuito. Tocar para reintentar."
                                       : "Buscando el circuito.");
        return;
    }
    if (s_padelIndex >= total) s_padelIndex = 0;
    uiPadelShowScreen(s_padelIndex);
}

static bool refreshPadel() {
    s_padelTried = true;
    const PadelResult r = padelFetch();
    s_padelLastFetch = millis();
    if (r != PADEL_UPDATED) return false;

    confirmarArranque();
    s_padelIndex = 0;
    if (s_phase == PHASE_RUNNING && s_app == APP_PADEL) {
        showPadel();
        s_lastRotate = millis();
    }
    return true;
}

// -- Tareas ------------------------------------------------------------------

static void showTodo() {
    s_todoRev = todoRevision();
    uiTodoDraw(wifiConnected() ? wifiIP().c_str() : "sin red");
}

static bool entrarRecordatorio(uint32_t epoch) {
    const TodoItem* due = todoFindDueReminder(epoch);
    if (!due) return false;

    // Se marca antes de mostrarlo. Si el equipo se reinicia con la tarjeta
    // abierta, el mismo aviso no vuelve a sonar al arrancar.
    s_reminderTaskId = due->id;
    if (!todoMarkReminderFired(s_reminderTaskId)) return false;
    const TodoItem* item = todoGetById(s_reminderTaskId);
    if (!item) return false;

    s_phase = PHASE_TODO_REMINDER;
    uiTodoDrawReminder(item);
    playAlertUp();
    return true;
}

// -- Avisos ------------------------------------------------------------------

static void showAvisos() {
    // Estar mirando la lista ya es haberlos leído: si no, al salir volvería a
    // interrumpir con el que se acaba de leer.
    notifMarcarTodosLeidos();
    s_notifRev = notifRevision();
    uiNotifDrawLista();
}

// El aviso interrumpe lo que haya en pantalla. Es el punto de un aviso: si hay
// que ir a buscarlo, no avisó nada.
static void entrarAviso() {
    const Notif* n = notifPendiente();
    if (!n) return;
    s_phase = PHASE_AVISO;
    s_avisoDesde = millis();
    uiNotifDrawCard(n, 1.0f);
}

// -- Apps --------------------------------------------------------------------

static void showApp() {
    switch (s_app) {
        case APP_PADEL:  showPadel(); break;
        case APP_TAREAS: showTodo();  break;
        case APP_AVISOS: showAvisos(); break;
        default:         showNoticias(); break;
    }
}

static void advance() {
    // Las tareas y los avisos no rotan: son listas, no carruseles.
    if (s_app == APP_TAREAS || s_app == APP_AVISOS) return;

    if (s_app == APP_PADEL) {
        const uint8_t total = padelScreenCount();
        // Sin contenido, un toque es un reintento. refreshPadel() ya repinta
        // cuando trae algo: volver a dibujar acá costaría otros 99 ms para
        // mostrar lo mismo.
        if (total == 0) { if (!refreshPadel()) showPadel(); s_lastRotate = millis(); return; }
        s_padelIndex = (s_padelIndex + 1) % total;
    } else {
        const uint8_t total = feedCount();
        if (total == 0) { refreshFeed(); return; }
        s_index = (s_index + 1) % total;
    }
    showApp();
    s_lastRotate = millis();
}

static void enterApp(AppId app) {
    // Cambiar de app cambia el dibujo entero, así que se pide la cortina: la
    // pantalla nueva baja por bandas de arriba hacia abajo en vez de aparecer de
    // golpe. Un deslizamiento pide un movimiento, y el vertical es el único que
    // entra en el presupuesto del panel.
    //
    // Las apps animadas —noticias y pádel— consumen el pedido sin usarlo en
    // uiAnimBegin(): su propia ola ya baja de arriba hacia abajo y sería el
    // mismo gesto dos veces. Las estáticas lo usan en uiAnimReveal().
    if (app != s_app) uiAnimCurtainOnce();

    s_app = app;
    s_phase = PHASE_RUNNING;
    s_lastRotate = millis();

    // La primera entrada a padel baja los datos: no tiene sentido pedirlos al
    // arrancar el aparato si el usuario nunca abre la app.
    //
    // Ojo con repintar de más: cada pantalla completa cuesta ~99 ms, así que
    // encadenar showPadel() + refreshPadel() + showApp() se ve como un
    // parpadeo triple. refreshPadel() ya dibuja cuando trae contenido.
    if (app == APP_PADEL && padelScreenCount() == 0) {
        showPadel();                    // "Buscando el circuito", mientras bloquea el GET
        if (refreshPadel()) return;
    }
    showApp();
    s_lastRotate = millis();
}

// El estado que muestra cada tarjeta del selector. Lo arma quien conoce la app:
// el launcher no consulta clientes.
static void llenarApps(AppInfo out[APP_COUNT]) {
    out[APP_NOTICIAS].nombre = "NOTICIAS";
    out[APP_NOTICIAS].inicial = 'N';
    if (feedCount() > 0) {
        snprintf(out[APP_NOTICIAS].estado, sizeof(out[APP_NOTICIAS].estado),
                 "%u titulares%s", feedCount(), s_offline ? "  ·  sin red" : "");
    } else {
        snprintf(out[APP_NOTICIAS].estado, sizeof(out[APP_NOTICIAS].estado), "sin contenido");
    }

    out[APP_PADEL].nombre = "PÁDEL";
    out[APP_PADEL].inicial = 'P';
    const PadelTour* live = padelLive();
    if (live) {
        snprintf(out[APP_PADEL].estado, sizeof(out[APP_PADEL].estado),
                 "%s  ·  día %u de %u", live->name, live->day, live->days);
    } else if (padelTourCount() > 0) {
        snprintf(out[APP_PADEL].estado, sizeof(out[APP_PADEL].estado),
                 "próximo: %s", padelTour(0)->name);
    } else {
        snprintf(out[APP_PADEL].estado, sizeof(out[APP_PADEL].estado), "%s",
                 s_padelTried ? "sin datos" : "sin abrir todavía");
    }

    out[APP_TAREAS].nombre = "TAREAS";
    out[APP_TAREAS].inicial = 'T';
    const uint8_t pend = todoPending();
    if (todoCount() == 0) {
        snprintf(out[APP_TAREAS].estado, sizeof(out[APP_TAREAS].estado), "sin tareas");
    } else if (pend == 0) {
        snprintf(out[APP_TAREAS].estado, sizeof(out[APP_TAREAS].estado), "todo hecho");
    } else {
        snprintf(out[APP_TAREAS].estado, sizeof(out[APP_TAREAS].estado),
                 "%u de %u pendiente%s", pend, todoCount(), pend == 1 ? "" : "s");
    }

    out[APP_AVISOS].nombre = "AVISOS";
    out[APP_AVISOS].inicial = 'A';
    const uint8_t sinLeer = notifSinLeer();
    if (notifCount() == 0) {
        snprintf(out[APP_AVISOS].estado, sizeof(out[APP_AVISOS].estado), "sin avisos");
    } else if (sinLeer == 0) {
        snprintf(out[APP_AVISOS].estado, sizeof(out[APP_AVISOS].estado),
                 "%u, todos leídos", notifCount());
    } else {
        snprintf(out[APP_AVISOS].estado, sizeof(out[APP_AVISOS].estado),
                 "%u sin leer", sinLeer);
    }
}

static void enterLauncher() {
    s_phase = PHASE_LAUNCHER;
    AppInfo apps[APP_COUNT];
    llenarApps(apps);
    // El selector también entra con cortina: se abre con un deslizamiento hacia
    // arriba, así que aparecer de golpe rompía la relación entre el gesto y lo
    // que pasa en pantalla.
    uiAnimCurtainOnce();
    uiLauncherDraw(apps, APP_COUNT, s_app);
}

static void enterRunning() {
    s_phase = PHASE_RUNNING;
    s_app = APP_NOTICIAS;
    timeSetup();
    // El editor de tareas queda levantado siempre, no sólo con la app abierta:
    // la gracia es poder anotar algo desde el teléfono mientras la pantalla
    // muestra otra cosa.
    webServerStart();
    uiFercedShowStatus("Conectado", "Buscando contenido.");
    // refreshFeed() repinta solo, con contenido o con el aviso de que no llegó.
    refreshFeed();
    s_lastRotate = millis();
}

// El host del endpoint, sin esquema ni ruta: alcanza para distinguir el build
// de LAN del de produccion y no expone la ruta ni el token.
static const char* endpointHost() {
    static char host[48];
    const char* p = strstr(FEED_ENDPOINT, "://");
    p = p ? p + 3 : FEED_ENDPOINT;
    size_t n = 0;
    while (p[n] && p[n] != '/' && n < sizeof(host) - 1) { host[n] = p[n]; n++; }
    host[n] = '\0';
    return host;
}

// La pantalla de diagnostico no anima nada: se dibuja una sola vez al entrar y
// el loop de la fase se limita a esperar un toque.
static void enterConfig() {
    s_phase = PHASE_CONFIG;

    // wifiSSID() devuelve el SSID con el que se conecto, sin tener que leer la
    // clave de NVS para nada.
    const bool online = wifiConnected();
    const String ip = wifiIP();

    // El fps sale del PERIODO, no del costo. avgUs100 mide solo lo que tarda
    // el tick y deja afuera el delay() del loop, el tactil y wifiLoop(): usarlo
    // daria cerca del doble del framerate real.
    const UiFrameStats st = uiAnimStats();
    const float fps = st.periodUs100 > 0 ? 10000.0f / (float)st.periodUs100 : 0.0f;

    char nombre[NVS_NOMBRE_LEN];
    nvsGetNombre(nombre, sizeof(nombre));
    const ConfigInfo info = {
        nombre,
        FERCED_VERSION, FERCED_COMMIT, FERCED_BUILD_DATE,
        wifiSSID(), ip.c_str(), endpointHost(),
        online ? wifiRSSI() : 0,
        millis() / 1000,
        fps,
        feedCount(),
        online,
    };
    uiConfigDraw(info);
}

// -- Actualizacion por WiFi --------------------------------------------------
// Corre entera dentro del toque: otaAplicar() bloquea hasta terminar y avisa el
// avance por este callback. El callback escribe SOLO la franja de estado, que
// es para lo que existe: la descarga informa hasta 101 veces y repintar la
// pantalla completa cuesta 99 ms cada una, o sea diez segundos de dibujo.
static char s_otaVersion[16] = {};

static void otaProgreso(int pct) {
    char linea[40];
    snprintf(linea, sizeof(linea), "Descargando %s", s_otaVersion);
    uiConfigEstado(linea, pct);
}

static void buscarActualizacion() {
    uiConfigEstado("Buscando actualización...");

    const OtaCheck c = otaBuscar();
    if (c.error) {
        uiConfigEstado(c.detalle, -1, true);
        return;
    }
    if (!c.hayNueva) {
        char linea[48];
        snprintf(linea, sizeof(linea), "Ya está al día: %s", FERCED_VERSION);
        uiConfigEstado(linea);
        return;
    }

    snprintf(s_otaVersion, sizeof(s_otaVersion), "%s", c.version);

    char detalle[64] = {};
    if (otaAplicar(otaProgreso, detalle, sizeof(detalle))) return;  // reinicia

    // Nada de ESP.restart() aca: si fallo, la particion quedo abortada y el
    // firmware que corre es el mismo de siempre. Lo unico que falta es contar
    // por que, y para eso la pantalla tiene que quedarse donde esta.
    uiConfigEstado(detalle[0] ? detalle : "no se pudo actualizar", -1, true);
}

// El boton "Actualizar feed" se quedaba mudo: disparaba el pedido, pero el
// proxy servia el pool cacheado y en pantalla no cambiaba nada, asi que parecia
// roto. Ahora fuerza el refresco de verdad y cuenta como le fue, sin salir de
// esta pantalla: ver el resultado es justamente lo que faltaba.
static void actualizarFeed() {
    uiConfigEstado("Actualizando feed...");
    const bool ok = refreshFeed(true);

    char linea[52];
    if (ok) snprintf(linea, sizeof(linea), "Listo: %u titulares", feedCount());
    else    snprintf(linea, sizeof(linea), "No se pudo actualizar");
    uiConfigEstado(linea, -1, !ok);
}

// Sin esto un brownout (6), un panic (4) y un reinicio por software (3) son
// indistinguibles, y ya hubo un reinicio espontaneo sin motivo capturado. El
// numero es el de esp_reset_reason_t; el nombre es para leer el log de un
// vistazo.
static const char* motivoReset(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "encendido";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "wdt-interrupcion";
        case ESP_RST_TASK_WDT:  return "wdt-tarea";
        case ESP_RST_WDT:       return "wdt-otro";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        default:                return "desconocido";
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ferced-display ===");
    const esp_reset_reason_t rr = esp_reset_reason();
    Serial.printf("[Boot] motivo=%d (%s)\n", (int)rr, motivoReset(rr));

    nvsInit();

#ifdef FERCED_REGALO
    // Firmware de un solo uso para dejar el aparato listo para regalar: le pone
    // nombre, le borra las tareas y los avisos del dueño anterior y le olvida el
    // WiFi, para que arranque en la pantalla del QR como recién sacado de la
    // caja.
    //
    // Va como entorno de compilación aparte y NO se deja puesto: se flashea
    // este, se espera a que haga lo suyo, y se vuelve a flashear el normal. Sin
    // guarda ni bandera en NVS, porque no hace falta ninguna si el firmware no
    // se queda: una guarda mal puesta borraría el WiFi de quien lo reciba.
    Serial.println("[REGALO] preparando el aparato...");
    nvsSetNombre(FERCED_REGALO);
    while (todoCount() > 0) todoRemove(0);
    notifBorrarTodos();
    nvsForgetWifi();
    Serial.printf("[REGALO] listo: se llama \"%s\", sin tareas, sin avisos y sin WiFi.\n",
                  FERCED_REGALO);
    Serial.println("[REGALO] AHORA flashear el firmware normal (ferced_display_vps).");
#endif

    Colors::setTheme(Colors::THEME_DARK);
    displaySetup();
    displaySetupVSync();
    displaySetBrightness(nvsGetBrightness());
    touchSetup();
    audioSetup();
    audioSetEnabled(nvsGetSoundEnabled());
    uiFercedSetup();
    uiPadelSetup();
    uiLauncherSetup();
    uiTodoSetup();
    uiNotifSetup();
    todoLoad();

    if (!nvsHasWifi()) {
        uiFercedShowStatus("Configurar", "Escanea el codigo para conectar el equipo a tu red.");
        startProvisioning();
        return;
    }

    char ssid[33] = {};
    char pass[65] = {};
    nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));

    uiFercedShowStatus("Conectando", ssid);
    wifiSetup(ssid, pass);

    if (!wifiConnected()) {
        uiFercedShowStatus("Sin red", "Reintentando sola.");
        s_phase = PHASE_CONNECTING;
        return;
    }
    enterRunning();
}

void loop() {
    esp_task_wdt_reset();
    const uint32_t now = millis();

    if (s_phase == PHASE_PROVISION) {
        if (provisionTick()) {
            char ssid[33] = {};
            char pass[65] = {};
            provisionGetCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
            provisionStop();

            uiFercedShowStatus("Probando", ssid);
            wifiSetup(ssid, pass);
            if (wifiConnected()) {
                nvsSaveWifi(ssid, pass);
                enterRunning();
            } else {
                uiFercedShowStatus("No anduvo", "Esa red no conecto. Probemos de nuevo.");
                startProvisioning();
            }
        }
        delay(4);
        return;
    }

    if (s_phase == PHASE_CONNECTING) {
        // Reintenta sola. Un aparato de escritorio no puede quedarse esperando
        // que alguien lo toque porque el router tardo en levantar.
        if (touchLoop().gesture == TOUCH_TAP) {
            startProvisioning();
            return;
        }
        if (now >= s_nextWifiRetry) {
            char ssid[33] = {};
            char pass[65] = {};
            nvsLoadWifi(ssid, sizeof(ssid), pass, sizeof(pass));
            wifiSetup(ssid, pass);

            if (wifiConnected()) {
                s_wifiRetryMs = WIFI_RETRY_MIN_MS;
                enterRunning();
                return;
            }
            uiFercedShowStatus("Sin red", "No conecta. Tocar para reconfigurar.");
            s_nextWifiRetry = millis() + s_wifiRetryMs;
            s_wifiRetryMs = s_wifiRetryMs * 2 > WIFI_RETRY_MAX_MS
                                ? WIFI_RETRY_MAX_MS
                                : s_wifiRetryMs * 2;
        }
        uiFercedTick(nowEpoch(), 0.0f);
        delay(8);
        return;
    }

    if (s_phase == PHASE_CONFIG) {
        // La pantalla esta quieta: no hay nada que repintar, solo resolver el
        // toque contra la geometria de los botones, que vive en ui_config.cpp.
        const TouchEvent ev = touchLoop();
        if (ev.gesture == TOUCH_TAP) {
            switch (uiConfigHit(ev.x, ev.y)) {
                case CFG_CLOSE:
                    s_phase = PHASE_RUNNING;
                    showApp();
                    s_lastRotate = millis();
                    break;
                case CFG_REFRESH:
                    // Se queda en configuracion a proposito: el resultado se
                    // escribe en la franja de estado y hay que poder leerlo.
                    actualizarFeed();
                    break;
                case CFG_UPDATE:
                    buscarActualizacion();
                    break;
                case CFG_FORGET:
                    // Sin confirmacion, por decision explicita. El boton va
                    // aparte, en rojo y en la pastilla mas chica de la grilla
                    // para bajar la chance de un roce.
                    nvsForgetWifi();
                    ESP.restart();
                    break;
                default:
                    break;
            }
        }
        delay(8);
        return;
    }

    if (s_phase == PHASE_TODO_REMINDER) {
        const TouchEvent ev = touchLoop();
        if (ev.gesture == TOUCH_TAP) {
            const TodoReminderAction action = uiTodoReminderTap(ev.x, ev.y);
            if (action == TODO_REMINDER_NONE) { delay(8); return; }

            if (action == TODO_REMINDER_COMPLETE) {
                todoSetDone(s_reminderTaskId, true);
            } else if (action == TODO_REMINDER_SNOOZE) {
                todoSnooze(s_reminderTaskId, nowEpoch() + 10 * 60);
            } else if (action == TODO_REMINDER_OPEN) {
                const uint32_t id = s_reminderTaskId;
                s_reminderTaskId = 0;
                s_phase = PHASE_RUNNING;
                s_app = APP_TAREAS;
                s_todoRev = todoRevision();
                uiTodoOpen(id);
                s_lastRotate = millis();
                return;
            }

            s_reminderTaskId = 0;
            s_phase = PHASE_RUNNING;
            showApp();
            s_lastRotate = millis();
            return;
        }
        delay(8);
        return;
    }

    if (s_phase == PHASE_AVISO) {
        // Lo cierra el botón, un deslizamiento —que es deliberado— o el tiempo.
        // Antes lo cerraba CUALQUIER toque, así que un roce se llevaba el aviso
        // puesto antes de que llegaras a leerlo. Un toque fuera del botón ahora
        // no hace nada, que es lo que corresponde en algo que se abrió encima.
        const TouchEvent ev = touchLoop();
        const uint32_t pasado = now - s_avisoDesde;
        const bool cerrar = pasado >= AVISO_MS ||
                            (ev.gesture == TOUCH_TAP && uiNotifHitCerrar(ev.x, ev.y)) ||
                            (ev.gesture != TOUCH_NONE && ev.gesture != TOUCH_TAP);

        if (cerrar) {
            notifMarcarTodosLeidos();
            s_notifRev = notifRevision();
            s_phase = PHASE_RUNNING;
            showApp();
            s_lastRotate = millis();
            return;
        }

        // Llegó otro mientras este estaba arriba: se muestra el nuevo y el reloj
        // vuelve a empezar.
        if (notifRevision() != s_notifRev) {
            s_notifRev = notifRevision();
            entrarAviso();
            return;
        }

        // La barra que se agota, cuatro veces por segundo. Es una franja de 4 px:
        // repintar la pantalla entera para esto costaría 99 ms cada vez.
        static uint32_t ultimaBarra = 0;
        if (now - ultimaBarra >= 250) {
            ultimaBarra = now;
            uiNotifDrawBarra(1.0f - (float)pasado / (float)AVISO_MS);
        }
        delay(8);
        return;
    }

    if (s_phase == PHASE_LAUNCHER) {
        // Igual que configuracion: pantalla quieta, solo resolver el toque.
        const TouchEvent ev = touchLoop();
        if (ev.gesture == TOUCH_TAP) {
            const int8_t hit = uiLauncherHit(ev.x, ev.y);
            if (hit >= 0) { enterApp((AppId)hit); return; }
        } else if (ev.gesture == TOUCH_SWIPE_DOWN || ev.gesture == TOUCH_FLING_DOWN) {
            enterApp(s_app);   // cerrar sin cambiar
            return;
        } else if (ev.gesture == TOUCH_LONG_PRESS) {
            enterConfig();
            return;
        }
        delay(8);
        return;
    }

    wifiLoop();

    // Se evaluan una vez por segundo y solo en uso normal. Pueden interrumpir
    // cualquier app, pero nunca pisan configuracion, provision u otro aviso.
    if (now - s_lastReminderCheck >= 1000) {
        s_lastReminderCheck = now;
        if (entrarRecordatorio(nowEpoch())) return;
    }

    // Un aviso interrumpe, pero SÓLO desde acá: si esto viviera antes del
    // switch de fases, un aviso podría aparecer en medio de una descarga de
    // firmware o del aprovisionamiento.
    if (notifRevision() != s_notifRev) {
        s_notifRev = notifRevision();
        if (s_app != APP_AVISOS && notifPendiente()) {
            entrarAviso();
            return;
        }
    }

    // touchLoop() consume el evento: una sola llamada por vuelta y se reparte
    // el resultado, porque la segunda ya devolveria TOUCH_NONE.
    const TouchEvent ev = touchLoop();
    switch (ev.gesture) {
        case TOUCH_LONG_PRESS:
            enterConfig();
            return;
        case TOUCH_SWIPE_UP:
        case TOUCH_FLING_UP:
            if (s_app == APP_TAREAS && ev.y < 420) {
                uiTodoScroll(ev.gesture == TOUCH_FLING_UP ? 240 : 150);
                return;
            }
            enterLauncher();
            return;
        case TOUCH_SWIPE_DOWN:
        case TOUCH_FLING_DOWN:
            if (s_app == APP_TAREAS) {
                uiTodoScroll(ev.gesture == TOUCH_FLING_DOWN ? -240 : -150);
                return;
            }
            break;
        case TOUCH_SWIPE_LEFT:
            enterApp((AppId)((s_app + 1) % APP_COUNT));
            return;
        case TOUCH_SWIPE_RIGHT:
            enterApp((AppId)((s_app + APP_COUNT - 1) % APP_COUNT));
            return;
        case TOUCH_TAP:
            if (s_app == APP_TAREAS) {
                // Tocar una tarea la marca hecha. La pantalla se repinta sola
                // en cuanto cambia la revisión, más abajo.
                const TodoUiAction action = uiTodoTap(ev.x, ev.y);
                const TodoItem* item = todoGetById(action.taskId);
                switch (action.type) {
                    case TODO_UI_TOGGLE_TASK:
                        if (item) todoSetDone(item->id, !item->done);
                        break;
                    case TODO_UI_TOGGLE_SUBTASK:
                        if (item) for (uint8_t i = 0; i < item->subCount; ++i) {
                            if (item->subtasks[i].id == action.subtaskId) {
                                todoSetSubtaskDone(item->id, action.subtaskId, !item->subtasks[i].done);
                                break;
                            }
                        }
                        break;
                    case TODO_UI_TOGGLE_COLLAPSE:
                        if (item) todoSetCollapsed(item->id, !item->collapsed);
                        break;
                    case TODO_UI_OPEN_DETAIL:
                        uiTodoOpen(action.taskId);
                        break;
                    case TODO_UI_BACK:
                        uiTodoBack();
                        break;
                    default:
                        break;
                }
            } else if (s_app != APP_AVISOS) {
                advance();
            }
            break;
        default:
            break;
    }

    uint32_t sinceRotate = now - s_lastRotate;
    if (s_app != APP_TAREAS && s_app != APP_AVISOS && sinceRotate >= FEED_ROTATE_MS) {
        advance();
        // Hay que recalcularlo: advance() acaba de mover s_lastRotate y todo lo
        // que sigue mira este valor. Con el viejo —que por definicion vale
        // 17.000 y pico— el prefetch de mas abajo daba por cumplida su espera de
        // dos segundos y bajaba la imagen JUSTO despues de arrancar la
        // animacion, que es exactamente lo que esos dos segundos existen para
        // evitar. Se veia como que uno de cada dos titulares aparecia de golpe:
        // la telemetria lo delataba con transiciones de frames=1 alternadas con
        // las de 40, porque el GET bloqueante se comia la ventana entera de la
        // ola. Y de paso el riel arrancaba lleno en vez de vacio.
        //
        // millis() y no `now`: `now` es de antes del advance(), asi que la resta
        // daria negativa y, en unsigned, gigante.
        sinceRotate = millis() - s_lastRotate;
    }

    // La lista también cambia desde el teléfono, en otra tarea. Repintar acá y
    // no allá evita que dos tareas dibujen sobre el mismo panel a la vez.
    if (s_app == APP_TAREAS && todoRevision() != s_todoRev) showTodo();

    // El feed se refresca siempre, corra la app que corra: al volver a noticias
    // tiene que haber contenido fresco, no el de hace media hora.
    const bool dueRefresh = now - s_lastFetch >= FEED_REFRESH_MS;
    const bool dueRetry = s_offline && s_nextRetry != 0 && now >= s_nextRetry;
    if (dueRefresh || dueRetry) refreshFeed();

    // La imagen del próximo titular se adelanta cuando ya no pasa nada. Bajarla
    // dentro de la transición metía ~200 ms de red entre el gesto y el primer
    // píxel, que es justo donde se siente que el aparato "no responde".
    //
    // Los dos segundos de espera no son de más: si se adelantara apenas termina
    // la animación, caería encima del segundo toque de quien está pasando
    // titulares rápido, y ahí sí se comería un gesto.
    if (s_app == APP_NOTICIAS && sinceRotate > 2000 && feedCount() > 1) {
        const FeedItem* prox = feedItem((uint8_t)((s_index + 1) % feedCount()));
        if (prox) feedPrefetchImage(prox->imgKey);
    }

    // Padel solo se refresca con la app abierta: fuera de ella nadie mira el
    // orden de juego, y son dos sitios ajenos los que pagan el pedido.
    if (s_app == APP_PADEL && s_padelTried &&
        now - s_padelLastFetch >= PADEL_REFRESH_MS) {
        refreshPadel();
    }

    // La animacion la marca el VSync dentro del tick; el delay solo evita que
    // el loop queme CPU cuando no hay nada que repintar.
    //
    // El tick TIENE que ser el de la app que está corriendo: el de noticias
    // repinta la línea de progreso cuatro veces por segundo, y llamarlo con la
    // lista de tareas en pantalla la iría pisando.
    const float progreso = (float)sinceRotate / (float)FEED_ROTATE_MS;
    bool busy = false;
    switch (s_app) {
        case APP_PADEL:  busy = uiPadelTick(progreso); break;
        case APP_TAREAS:
        case APP_AVISOS: break;                        // pantallas quietas
        default:         busy = uiFercedTick(nowEpoch(), progreso); break;
    }
    delay(busy ? 1 : 8);
}
