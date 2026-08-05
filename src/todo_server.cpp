#include "todo_server.h"
#include "todo_store.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <pgmspace.h>

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
static const char PAGINA[] PROGMEM = R"HTML(<!doctype html>
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

static void escapar(String& out, const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        switch (*p) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (*p < 0x20) out += ' ';
                else           out += (char)*p;
        }
    }
}

static String listaJSON() {
    String out;
    out.reserve(64 + todoCount() * (TODO_TEXT_LEN + 16));
    out += "{\"items\":[";
    for (uint8_t i = 0; i < todoCount(); i++) {
        const TodoItem* it = todoItem(i);
        if (i) out += ',';
        out += "{\"t\":\"";
        escapar(out, it->text);
        out += "\",\"d\":";
        out += it->done ? "true" : "false";
        out += '}';
    }
    out += "],\"max\":";
    out += TODO_MAX_ITEMS;
    out += '}';
    return out;
}

static void responderLista(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* r = req->beginResponse(200, "application/json", listaJSON());
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
}

// Devuelve false y ya respondió si el pedido no trae el encabezado propio.
static bool autorizado(AsyncWebServerRequest* req) {
    if (req->hasHeader(HEADER_GUARD)) return true;
    Serial.printf("[Todo] %s sin %s: lo rechazo\n",
                  req->client()->remoteIP().toString().c_str(), HEADER_GUARD);
    req->send(403, "text/plain", "falta el encabezado " + String(HEADER_GUARD));
    return false;
}

// El índice puede venir por query o por cuerpo según el cliente; se aceptan los
// dos para no depender de cómo arme el pedido quien lo mande.
static int indiceDe(AsyncWebServerRequest* req) {
    const AsyncWebParameter* p = req->getParam("i");
    if (!p) p = req->getParam("i", true);
    if (!p) return -1;
    const long v = p->value().toInt();
    return (v < 0 || v > 255) ? -1 : (int)v;
}

// ── API ──────────────────────────────────────────────────────────────────────

void todoServerStart() {
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

    server->onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "no hay nada aca");
    });

    server->begin();
    Serial.println("[Todo] editor en el puerto 80");
}

void todoServerStop() {
    if (!server) return;
    server->end();
    delete server;
    server = nullptr;
    Serial.println("[Todo] editor apagado");
}

bool todoServerRunning() { return server != nullptr; }
