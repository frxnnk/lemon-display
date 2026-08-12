#include "web_server.h"
#include "notif_store.h"
#include "todo_store.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ctime>
#include <pgmspace.h>
#include "todo_page.h"

static AsyncWebServer* server = nullptr;

// Las mutaciones exigen este encabezado. No es autenticación —cualquiera en la
// red de casa puede editar la lista, igual que el portal cautivo— sino la
// defensa contra CSRF: un navegador NO deja que una página de otro sitio mande
// un encabezado propio sin un preflight de CORS, que este servidor no contesta.
// Sin esto, cualquier web que el usuario visite podría borrarle las tareas con
// un <form> escondido.
static const char* HEADER_GUARD = "X-Ferced";

// ── La página ────────────────────────────────────────────────────────────────
// Va entera acá, sin dependencias externas: el aparato tiene que poder servirla
// con el VPS caído y sin internet. Tipografía del sistema porque embeber
// Satoshi serían ~100 KB de flash para una página que se abre de vez en cuando.
static const char PAGINA_OLD[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0e1011">
<title>Ferced · Tareas</title>
<style>
:root{--bg:#0e1011;--fg:#fff;--fg2:#bfbfbf;--fg3:#8c8c8c;--fg4:#636363;--line:#222526;--surface:#16191a;--danger:#f87171}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;padding:24px 18px 48px;max-width:620px;margin-inline:auto}
header{display:flex;align-items:baseline;gap:10px;margin-bottom:22px}
h1{font-size:15px;letter-spacing:.14em;font-weight:600;margin:0;text-transform:uppercase}
header span{font-size:12px;color:var(--fg4);letter-spacing:.1em;text-transform:uppercase}
form{display:flex;gap:10px;margin-bottom:8px}
input[type=text]{flex:1;min-width:0;background:var(--surface);border:1px solid var(--line);border-radius:12px;padding:14px 16px;color:var(--fg);font-size:16px;outline:none}
input[type=text]:focus{border-color:var(--fg3)}
input[type=text]::placeholder{color:var(--fg4)}
button{background:var(--surface);border:1px solid var(--line);border-radius:12px;color:var(--fg);font-size:16px;padding:14px 20px;cursor:pointer}
button:active{background:#1f2223}
#add{font-weight:600;min-width:56px}
ul{list-style:none;padding:0;margin:18px 0 0}
li{display:flex;align-items:center;gap:14px;padding:14px 2px;border-bottom:1px solid var(--line)}
li:last-child{border-bottom:0}
.box{flex:none;width:24px;height:24px;background:none;padding:0;border:1.5px solid var(--fg3);border-radius:7px;cursor:pointer;display:grid;place-items:center;font-size:14px;color:var(--bg)}
li.done .box{background:var(--fg);border-color:var(--fg)}
.txt{flex:1;min-width:0;cursor:pointer;overflow-wrap:anywhere}
li.done .txt{color:var(--fg4);text-decoration:line-through}
.del{flex:none;background:none;border:0;color:var(--fg4);font-size:22px;line-height:1;padding:6px 10px;cursor:pointer}
.del:active{color:var(--danger)}
footer{display:flex;justify-content:space-between;align-items:center;margin-top:26px;font-size:13px;color:var(--fg4)}
footer button{font-size:13px;padding:8px 14px;border-radius:10px;color:var(--fg3)}
.vacio{color:var(--fg4);padding:28px 2px;text-align:center}
.aviso{color:var(--danger);font-size:13px;min-height:18px;margin-top:8px}
</style></head><body>
<header><h1>Tareas</h1><span>Ferced</span></header>
<form id="f"><input type="text" id="t" placeholder="Nueva tarea" autocomplete="off" maxlength="60"><button id="add" type="submit">+</button></form>
<div class="aviso" id="aviso"></div>
<ul id="l"></ul>
<footer><span id="cnt"></span><button id="clear" type="button">Borrar hechas</button></footer>
<script>
var L=document.getElementById('l'),C=document.getElementById('cnt'),A=document.getElementById('aviso');
function api(url,metodo){
  return fetch(url,{method:metodo||'GET',headers:{'X-Ferced':'1'},cache:'no-store'})
    .then(function(r){if(!r.ok)throw new Error(r.status);return r.json()})
    .then(pintar)
    .catch(function(e){A.textContent='No pude hablar con el aparato ('+e.message+')'});
}
function pintar(d){
  A.textContent='';
  L.innerHTML='';
  if(!d.items.length){
    var v=document.createElement('li');v.className='vacio';v.textContent='Sin tareas todavia.';L.appendChild(v);
  }
  d.items.forEach(function(it,i){
    var li=document.createElement('li');if(it.d)li.className='done';
    // La casilla es un <button> y no un <div>: asi la ve un lector de pantalla
    // y se puede llegar con el teclado.
    var b=document.createElement('button');b.className='box';b.type='button';
    b.textContent=it.d?'✓':'';
    b.setAttribute('aria-pressed',it.d?'true':'false');
    b.setAttribute('aria-label',(it.d?'Desmarcar: ':'Marcar hecha: ')+it.t);
    var t=document.createElement('div');t.className='txt';t.textContent=it.t;
    var x=document.createElement('button');x.className='del';x.type='button';x.textContent='×';
    x.setAttribute('aria-label','Borrar: '+it.t);
    b.onclick=t.onclick=function(){api('/api/toggle?i='+i,'POST')};
    x.onclick=function(){api('/api/del?i='+i,'POST')};
    li.appendChild(b);li.appendChild(t);li.appendChild(x);L.appendChild(li);
  });
  var p=d.items.filter(function(x){return !x.d}).length;
  C.textContent=p+(p===1?' pendiente':' pendientes')+' · '+d.items.length+'/'+d.max;
}
document.getElementById('f').onsubmit=function(e){
  e.preventDefault();
  var i=document.getElementById('t'),v=i.value.trim();
  if(!v)return;
  i.value='';
  api('/api/add?t='+encodeURIComponent(v),'POST');
};
document.getElementById('clear').onclick=function(){api('/api/clear','POST')};
api('/api/todos');
// La lista tambien cambia desde la pantalla tactil del aparato, asi que se
// refresca sola mientras la pagina este visible.
setInterval(function(){if(!document.hidden)api('/api/todos')},5000);
</script></body></html>)HTML";

// ── JSON ─────────────────────────────────────────────────────────────────────

static void escapar(Print& out, const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        switch (*p) {
            case '"':  out.print("\\\""); break;
            case '\\': out.print("\\\\"); break;
            case '\n': out.print("\\n"); break;
            default:
                if (*p < 0x20) out.print(' ');
                else           out.write(*p);
        }
    }
}

static void responderLista(AsyncWebServerRequest* req, uint32_t createdId = 0) {
    AsyncResponseStream* out = req->beginResponseStream("application/json");
    out->print("{\"v\":2,\"rev\":");
    out->print(todoRevision());
    out->print(",\"max\":");
    out->print(TODO_MAX_ITEMS);
    if (createdId) { out->print(",\"created\":"); out->print(createdId); }
    out->print(",\"items\":[");
    for (uint8_t i = 0; i < todoCount(); ++i) {
        const TodoItem* it = todoItem(i);
        if (!it) continue;
        if (i) out->print(',');
        out->print("{\"id\":"); out->print(it->id);
        out->print(",\"title\":\""); escapar(*out, it->title);
        out->print("\",\"description\":\""); escapar(*out, it->description);
        out->print("\",\"done\":"); out->print(it->done ? "true" : "false");
        out->print(",\"collapsed\":"); out->print(it->collapsed ? "true" : "false");
        out->print(",\"due\":"); out->print(it->dueEpoch);
        out->print(",\"reminder\":"); out->print(it->reminderEpoch);
        out->print(",\"reminderFired\":"); out->print(it->reminderFired ? "true" : "false");
        out->print(",\"subtasks\":[");
        for (uint8_t s = 0; s < it->subCount; ++s) {
            if (s) out->print(',');
            const TodoSubtask& sub = it->subtasks[s];
            out->print("{\"id\":"); out->print(sub.id);
            out->print(",\"title\":\""); escapar(*out, sub.title);
            out->print("\",\"done\":"); out->print(sub.done ? "true" : "false");
            out->print('}');
        }
        out->print("]}");
    }
    out->print("]}");
    out->addHeader("Cache-Control", "no-store");
    req->send(out);
}

// Devuelve false y ya respondió si el pedido no trae el encabezado propio.
static bool autorizado(AsyncWebServerRequest* req) {
    if (req->hasHeader(HEADER_GUARD)) return true;
    Serial.printf("[Todo] %s sin %s: lo rechazo\n",
                  req->client()->remoteIP().toString().c_str(), HEADER_GUARD);
    req->send(403, "application/json", "{\"error\":\"falta X-Ferced\"}");
    return false;
}

static void errorJSON(AsyncWebServerRequest* req, int status, const char* message) {
    String body = "{\"error\":\"";
    for (const char* p = message; p && *p; ++p) {
        if (*p == '"' || *p == '\\') body += '\\';
        body += *p;
    }
    body += "\"}";
    req->send(status, "application/json", body);
}

static const AsyncWebParameter* parametro(AsyncWebServerRequest* req, const char* name) {
    const AsyncWebParameter* value = req->getParam(name);
    if (!value) value = req->getParam(name, true);
    return value;
}

static bool entero(AsyncWebServerRequest* req, const char* name, uint32_t& value,
                   bool optional = false) {
    const AsyncWebParameter* p = parametro(req, name);
    if (!p) { value = 0; return optional; }
    if (p->value().length() == 0) { value = 0; return optional; }
    char* end = nullptr;
    const unsigned long parsed = strtoul(p->value().c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

static bool booleano(AsyncWebServerRequest* req, const char* name, bool& value) {
    const AsyncWebParameter* p = parametro(req, name);
    if (!p) return false;
    if (p->value() == "1" || p->value() == "true") { value = true; return true; }
    if (p->value() == "0" || p->value() == "false") { value = false; return true; }
    return false;
}

// El índice puede venir por query o por cuerpo según el cliente; se aceptan los
// dos para no depender de cómo arme el pedido quien lo mande.
static int indiceDe(AsyncWebServerRequest* req) {
    const AsyncWebParameter* p = parametro(req, "i");
    if (!p) return -1;
    const long v = p->value().toInt();
    return (v < 0 || v > 255) ? -1 : (int)v;
}

// ── API ──────────────────────────────────────────────────────────────────────

void webServerStart() {
    if (server) return;

    server = new AsyncWebServer(80);

    server->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* r = req->beginResponse_P(200, "text/html; charset=utf-8", PAGINA);
        r->addHeader("Cache-Control", "no-cache");
        req->send(r);
    });

    server->on("/api/todos", HTTP_GET, [](AsyncWebServerRequest* req) {
        responderLista(req);
    });

    server->on("/api/tasks/create", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        const AsyncWebParameter* title = parametro(req, "title");
        const AsyncWebParameter* description = parametro(req, "description");
        uint32_t due = 0, reminder = 0;
        if (!title || !entero(req, "due", due, true) || !entero(req, "reminder", reminder, true)) {
            errorJSON(req, 400, "datos invalidos"); return;
        }
        const uint32_t createdId = todoAddFull(title->value().c_str(), description ? description->value().c_str() : "", due, reminder);
        if (!createdId) {
            errorJSON(req, 409, "lista llena o titulo vacio"); return;
        }
        responderLista(req, createdId);
    });

    server->on("/api/tasks/update", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t id = 0, due = 0, reminder = 0;
        const AsyncWebParameter* title = parametro(req, "title");
        const AsyncWebParameter* description = parametro(req, "description");
        if (!title || !entero(req, "id", id) || !entero(req, "due", due, true) ||
            !entero(req, "reminder", reminder, true) ||
            !todoUpdate(id, title->value().c_str(), description ? description->value().c_str() : "", due, reminder)) {
            errorJSON(req, 400, "tarea o datos invalidos"); return;
        }
        responderLista(req);
    });

    server->on("/api/tasks/delete", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t id = 0;
        if (!entero(req, "id", id) || !todoRemoveById(id)) { errorJSON(req, 404, "tarea inexistente"); return; }
        responderLista(req);
    });

    server->on("/api/tasks/done", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t id = 0; bool done = false;
        if (!entero(req, "id", id) || !booleano(req, "done", done) || !todoSetDone(id, done)) {
            errorJSON(req, 400, "estado invalido"); return;
        }
        responderLista(req);
    });

    server->on("/api/tasks/move", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t id = 0, position = 0;
        if (!entero(req, "id", id) || !entero(req, "position", position) ||
            position > 255 || !todoMove(id, static_cast<uint8_t>(position))) {
            errorJSON(req, 400, "posicion invalida"); return;
        }
        responderLista(req);
    });

    server->on("/api/tasks/collapse", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t id = 0; bool collapsed = false;
        if (!entero(req, "id", id) || !booleano(req, "collapsed", collapsed) ||
            !todoSetCollapsed(id, collapsed)) {
            errorJSON(req, 400, "estado invalido"); return;
        }
        responderLista(req);
    });

    server->on("/api/tasks/snooze", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t id = 0, minutes = 10;
        if (!entero(req, "id", id) || !entero(req, "minutes", minutes, true)) {
            errorJSON(req, 400, "datos invalidos"); return;
        }
        if (minutes == 0) minutes = 10;
        const time_t now = time(nullptr);
        if (now < 1600000000 || minutes > 1440 ||
            !todoSnooze(id, static_cast<uint32_t>(now) + minutes * 60UL)) {
            errorJSON(req, 409, "reloj no sincronizado o tarea invalida"); return;
        }
        responderLista(req);
    });

    server->on("/api/subtasks/create", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t taskId = 0;
        const AsyncWebParameter* title = parametro(req, "title");
        if (!title || !entero(req, "taskId", taskId) ||
            !todoAddSubtask(taskId, title->value().c_str())) {
            errorJSON(req, 409, "tarea llena, inexistente o titulo vacio"); return;
        }
        responderLista(req);
    });

    server->on("/api/subtasks/update", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t taskId = 0, id = 0;
        const AsyncWebParameter* title = parametro(req, "title");
        if (!title || !entero(req, "taskId", taskId) || !entero(req, "id", id) ||
            !todoUpdateSubtask(taskId, id, title->value().c_str())) {
            errorJSON(req, 400, "subtarea invalida"); return;
        }
        responderLista(req);
    });

    server->on("/api/subtasks/delete", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t taskId = 0, id = 0;
        if (!entero(req, "taskId", taskId) || !entero(req, "id", id) ||
            !todoRemoveSubtask(taskId, id)) {
            errorJSON(req, 404, "subtarea inexistente"); return;
        }
        responderLista(req);
    });

    server->on("/api/subtasks/done", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t taskId = 0, id = 0; bool done = false;
        if (!entero(req, "taskId", taskId) || !entero(req, "id", id) ||
            !booleano(req, "done", done) || !todoSetSubtaskDone(taskId, id, done)) {
            errorJSON(req, 400, "estado invalido"); return;
        }
        responderLista(req);
    });

    server->on("/api/subtasks/move", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        uint32_t taskId = 0, id = 0, position = 0;
        if (!entero(req, "taskId", taskId) || !entero(req, "id", id) ||
            !entero(req, "position", position) || position > 255 ||
            !todoMoveSubtask(taskId, id, static_cast<uint8_t>(position))) {
            errorJSON(req, 400, "posicion invalida"); return;
        }
        responderLista(req);
    });

    server->on("/api/add", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        const AsyncWebParameter* p = req->getParam("t");
        if (!p) p = req->getParam("t", true);
        if (!p || p->value().length() == 0) {
            req->send(400, "text/plain", "falta el texto");
            return;
        }
        if (!todoAdd(p->value().c_str())) {
            req->send(409, "text/plain", "la lista esta llena o el texto quedo vacio");
            return;
        }
        Serial.printf("[Todo] +%s\n", p->value().c_str());
        responderLista(req);
    });

    server->on("/api/toggle", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        const int i = indiceDe(req);
        if (i < 0 || !todoToggle((uint8_t)i)) {
            req->send(400, "text/plain", "indice invalido");
            return;
        }
        responderLista(req);
    });

    server->on("/api/del", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        const int i = indiceDe(req);
        if (i < 0 || !todoRemove((uint8_t)i)) {
            req->send(400, "text/plain", "indice invalido");
            return;
        }
        responderLista(req);
    });

    server->on("/api/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!autorizado(req)) return;
        todoClearDone();
        responderLista(req);
    });

    // ── Avisos ───────────────────────────────────────────────────────────────
    // La entrada que usan los agentes. A propósito NO exige el encabezado
    // X-Ferced: quien la va a llamar es un hook de una sola línea, y pedirle una
    // bandera de más a algo que se instala una vez y se olvida es fricción
    // gratis. El riesgo de que una web del navegador mande un aviso a la caja es
    // que aparezca un cartel; el de que borre la lista de tareas, no.
    //
    //   curl -X POST "http://ferced.local/api/notify?src=claude&t=Termino"
    server->on("/api/notify", HTTP_POST, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* t = req->getParam("t");
        if (!t) t = req->getParam("t", true);
        if (!t || t->value().length() == 0) {
            req->send(400, "text/plain", "falta t (el titulo del aviso)");
            return;
        }
        const AsyncWebParameter* s = req->getParam("src");
        if (!s) s = req->getParam("src", true);
        const AsyncWebParameter* b = req->getParam("b");
        if (!b) b = req->getParam("b", true);

        // El reloj puede no estar sincronizado todavía; notifPush() lo tolera.
        const uint32_t epoch = (uint32_t)time(nullptr);
        if (!notifPush(s ? s->value().c_str() : "",
                       t->value().c_str(),
                       b ? b->value().c_str() : "",
                       epoch > 1600000000UL ? epoch : 0)) {
            req->send(400, "text/plain", "el titulo quedo vacio");
            return;
        }
        req->send(200, "text/plain", "ok");
    });

    // GET además de POST: un `curl` sin banderas, o un navegador, o cualquier
    // cosa que sólo sepa pedir una URL. La barrera de entrada tiene que ser lo
    // más baja posible.
    server->on("/api/notify", HTTP_GET, [](AsyncWebServerRequest* req) {
        const AsyncWebParameter* t = req->getParam("t");
        if (!t || t->value().length() == 0) {
            req->send(400, "text/plain", "falta t (el titulo del aviso)");
            return;
        }
        const AsyncWebParameter* s = req->getParam("src");
        const AsyncWebParameter* b = req->getParam("b");
        const uint32_t epoch = (uint32_t)time(nullptr);
        notifPush(s ? s->value().c_str() : "", t->value().c_str(),
                  b ? b->value().c_str() : "",
                  epoch > 1600000000UL ? epoch : 0);
        req->send(200, "text/plain", "ok");
    });

    server->onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "no hay nada aca");
    });

    server->begin();

    // mDNS para que la dirección no dependa de la IP que reparta el router. Un
    // hook que se instala una vez y se olvida no puede romperse porque el DHCP
    // cambió de humor.
    if (MDNS.begin("ferced")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[Web] servidor en el puerto 80, http://ferced.local/");
    } else {
        Serial.println("[Web] servidor en el puerto 80 (mDNS no arrancó; usar la IP)");
    }
}

void webServerStop() {
    if (!server) return;
    server->end();
    delete server;
    server = nullptr;
    Serial.println("[Todo] editor apagado");
}

bool webServerRunning() { return server != nullptr; }
