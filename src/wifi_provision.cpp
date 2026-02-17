#include "wifi_provision.h"
#include "display_manager.h"
#include "ui_components.h"
#include "colors.h"
#include "config.h"
#include "design_system.h"
#include "data/lemon_logo.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <qrcode.h>
#include <Arduino.h>

// ── AP Configuration ──
static const char* AP_SSID = "Lemon-Setup";
static const char* AP_PASS = "lemon1234";
static const int   DNS_PORT = 53;

// ── State ──
static DNSServer*        dnsServer  = nullptr;
static AsyncWebServer*   webServer  = nullptr;
static bool              hasCredentials = false;
static char              rxSSID[33] = "";
static char              rxPass[65] = "";
static bool              running    = false;

// ── Captive portal HTML (PROGMEM) — dark theme, official logo, responsive ──
static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<meta name="theme-color" content="#000">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black">
<title>Lemon · WiFi</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent}
:root{--g:#00F068;--bg:#000;--c:#080C08;--s:#1A1A1A;--b:#2A2C2A;--t1:#fff;--t2:#868686;--t3:#5B5B5B}
html,body{height:100%}
body{background:var(--bg);color:var(--t1);font-family:'Inter',-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;display:flex;flex-direction:column;align-items:center;padding:0 16px;padding-top:max(16px,env(safe-area-inset-top));padding-bottom:max(16px,env(safe-area-inset-bottom))}
.hd{text-align:center;padding:24px 0 16px;width:100%}
.lo{display:inline-flex;align-items:center;gap:10px}
.lo canvas{border-radius:4px}
.sb{color:var(--t2);font-size:14px;margin-top:8px}
.ct{flex:1;display:flex;flex-direction:column;width:100%;max-width:400px;min-height:0;gap:12px}
.cd{background:var(--c);border:1px solid var(--b);border-radius:16px;overflow:hidden;overflow-y:auto;flex:1;min-height:0;-webkit-overflow-scrolling:touch;scrollbar-width:none}
.cd::-webkit-scrollbar{width:0}
.nt{display:flex;align-items:center;padding:14px 16px;min-height:54px;gap:12px;border-bottom:1px solid #121412;cursor:pointer;transition:background .12s}
.nt:last-child{border:none}
.nt:active{background:#0E180E}
.nm{font-size:15px;font-weight:500;flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sg{display:flex;align-items:flex-end;gap:2px;height:16px;flex-shrink:0}
.sg i{display:block;width:3px;border-radius:1px;background:#1E1E1E}
.sg i.a{background:var(--g)}
.sg i:nth-child(1){height:4px}
.sg i:nth-child(2){height:8px}
.sg i:nth-child(3){height:12px}
.sg i:nth-child(4){height:16px}
.pn{display:none;flex-direction:column;gap:14px}
.pn.on{display:flex}
.pn h3{font-size:17px;font-weight:600;color:var(--g);padding:4px 0}
.pw{position:relative}
input{width:100%;background:#0A0E0A;border:1px solid var(--b);border-radius:12px;padding:14px 48px 14px 14px;color:var(--t1);font-size:16px;outline:none;transition:border .2s}
input::placeholder{color:var(--t3)}
input:focus{border-color:var(--g)}
.ey{position:absolute;right:12px;top:50%;transform:translateY(-50%);background:none;border:none;color:var(--t2);font-size:12px;cursor:pointer;padding:8px}
.bt{display:block;width:100%;border:none;border-radius:12px;padding:14px;font-size:16px;font-weight:600;cursor:pointer;min-height:50px;text-align:center;transition:opacity .12s,transform .08s}
.bt:active{opacity:.8;transform:scale(.98)}
.bp{background:var(--g);color:#000}
.bg{background:none;color:var(--t2);border:1px solid var(--b)}
.st{text-align:center;padding:48px 16px}
.sp{display:inline-block;width:40px;height:40px;border:3px solid var(--b);border-top-color:var(--g);border-radius:50%;animation:r .7s linear infinite}
@keyframes r{to{transform:rotate(360deg)}}
.st p{margin-top:16px;color:var(--t2);font-size:14px}
.ok{color:var(--g)!important;font-weight:500}
.er{color:#FF1A3B!important}
.em{text-align:center;color:var(--t3);padding:32px 16px;font-size:14px}
.rf{display:block;margin:0 auto;background:none;border:1px solid var(--b);border-radius:10px;color:var(--t2);font-size:13px;padding:8px 24px;cursor:pointer;flex-shrink:0;transition:background .12s}
.rf:active{background:var(--s)}
.ft{text-align:center;padding:12px 0;color:var(--t3);font-size:11px;flex-shrink:0;width:100%}
@media(min-width:480px){body{padding-left:24px;padding-right:24px}.cd{max-height:50vh;flex:none}.hd{padding:32px 0 20px}}
@media(min-width:768px){body{justify-content:center}.cd{max-height:40vh}}
</style>
</head>
<body>
<div class="hd">
<div class="lo"><canvas id="lg" width="244" height="56" style="height:32px;width:auto"></canvas></div>
<p class="sb">Configurar WiFi</p>
</div>
<div class="ct">
<div id="ls" class="cd"><div class="st"><div class="sp"></div><p>Buscando redes...</p></div></div>
<div id="fm" class="pn">
<h3 id="sn"></h3>
<div class="pw"><input type="password" id="pw" placeholder="Contrase&#241;a" autocomplete="off"><button type="button" class="ey" onclick="tp()">mostrar</button></div>
<button class="bt bp" onclick="go()">Conectar</button>
<button class="bt bg" onclick="bk()">Volver</button>
</div>
<div id="rs" class="pn"><div class="st"><div class="sp"></div><p id="rm">Conectando...</p></div></div>
<button class="rf" onclick="sc()" id="rb">Buscar redes</button>
</div>
<div class="ft">v4.0.0 &middot; lemon.me</div>
<script>
let sel='',rc=0;
function lg(){fetch('/logo').then(r=>r.arrayBuffer()).then(b=>{let d=new Uint16Array(b),c=document.getElementById('lg').getContext('2d'),m=c.createImageData(244,56);for(let i=0;i<d.length;i++){let p=d[i];m.data[i*4]=((p>>11)&31)*255/31|0;m.data[i*4+1]=((p>>5)&63)*255/63|0;m.data[i*4+2]=(p&31)*255/31|0;m.data[i*4+3]=p?255:0}c.putImageData(m,0,0)}).catch(()=>{})}
function sb(r){let s=r>-50?4:r>-65?3:r>-75?2:1,h='';for(let i=1;i<=4;i++)h+='<i class="'+(i<=s?'a':'')+'"></i>';return h}
function sc(){document.getElementById('rb').textContent='Buscando...';fetch('/scan').then(r=>r.json()).then(d=>{document.getElementById('rb').textContent='Buscar redes';if(d.length===0&&rc<3){rc++;document.getElementById('ls').innerHTML='<div class="st"><div class="sp"></div><p>Buscando redes...</p></div>';setTimeout(sc,2000);return}rc=0;let h='';d.forEach(n=>{h+='<div class="nt" onclick="pk(\''+n.s.replace(/\\/g,'\\\\').replace(/'/g,"\\'")+'\')">';h+='<div class="nm">'+n.s+'</div><div class="sg">'+sb(n.r)+'</div></div>'});document.getElementById('ls').innerHTML=h||'<div class="em">No se encontraron redes</div>'}).catch(()=>{document.getElementById('rb').textContent='Buscar redes'})}
function pk(s){sel=s;document.getElementById('sn').textContent=s;document.getElementById('ls').style.display='none';document.getElementById('rb').style.display='none';document.getElementById('fm').classList.add('on');setTimeout(()=>document.getElementById('pw').focus(),120)}
function bk(){document.getElementById('fm').classList.remove('on');document.getElementById('ls').style.display='';document.getElementById('rb').style.display='';document.getElementById('pw').value=''}
function tp(){let i=document.getElementById('pw'),b=document.querySelector('.ey');if(i.type==='password'){i.type='text';b.textContent='ocultar'}else{i.type='password';b.textContent='mostrar'}}
function go(){let p=document.getElementById('pw').value;document.getElementById('fm').classList.remove('on');document.getElementById('rs').classList.add('on');document.getElementById('rb').style.display='none';fetch('/connect?ssid='+encodeURIComponent(sel)+'&pass='+encodeURIComponent(p)).then(()=>{document.querySelector('#rs .sp').style.display='none';let m=document.getElementById('rm');m.className='ok';m.textContent='\u00a1Credenciales guardadas!'}).catch(()=>{let m=document.getElementById('rm');m.className='er';m.textContent='Error. Intenta de nuevo.'})}
lg();sc();
</script>
</body>
</html>
)rawliteral";

// ── WiFi scan results for the portal ──
static String scanResultsJson() {
    int n = WiFi.scanComplete();
    if (n < 0) {
        WiFi.scanNetworks(true);
        return "[]";
    }
    String json = "[";
    for (int i = 0; i < n && i < 20; i++) {
        if (i > 0) json += ",";
        json += "{\"s\":\"";
        // Escape quotes in SSID
        String ssid = WiFi.SSID(i);
        ssid.replace("\"", "\\\"");
        json += ssid;
        json += "\",\"r\":";
        json += String(WiFi.RSSI(i));
        json += "}";
    }
    json += "]";
    return json;
}

void provisionStart() {
    if (running) return;

    hasCredentials = false;
    rxSSID[0] = '\0';
    rxPass[0] = '\0';

    // Start AP+STA mode so we can scan for networks
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[Provision] AP started: %s / %s\n", AP_SSID, AP_PASS);
    Serial.printf("[Provision] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    // Start scanning for networks
    WiFi.scanNetworks(true);

    // DNS server — redirect all domains to our IP (captive portal)
    dnsServer = new DNSServer();
    dnsServer->start(DNS_PORT, "*", WiFi.softAPIP());

    // Web server
    webServer = new AsyncWebServer(80);

    webServer->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", PORTAL_HTML);
    });

    webServer->on("/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
        String json = scanResultsJson();
        req->send(200, "application/json", json);
    });

    // Serve official 244x56 imagotipo as raw RGB565 binary (decoded by Canvas in portal)
    webServer->on("/logo", HTTP_GET, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* resp = req->beginResponse_P(
            200, "application/octet-stream",
            (const uint8_t*)lemon_imagotipo_244, 244 * 56 * 2);
        resp->addHeader("Cache-Control", "max-age=3600");
        req->send(resp);
    });

    webServer->on("/connect", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (req->hasParam("ssid") && req->hasParam("pass")) {
            String s = req->getParam("ssid")->value();
            String p = req->getParam("pass")->value();
            strncpy(rxSSID, s.c_str(), sizeof(rxSSID) - 1);
            strncpy(rxPass, p.c_str(), sizeof(rxPass) - 1);
            hasCredentials = true;
            Serial.printf("[Provision] Got credentials: SSID=%s\n", rxSSID);
            req->send(200, "text/plain", "OK");
        } else {
            req->send(400, "text/plain", "Missing params");
        }
    });

    // Captive portal detection endpoints
    webServer->on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("/");
    });
    webServer->on("/fwlink", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("/");
    });
    webServer->on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("/");
    });
    webServer->on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("/");
    });

    // Catch-all for captive portal
    webServer->onNotFound([](AsyncWebServerRequest* req) {
        req->redirect("/");
    });

    webServer->begin();
    running = true;
    Serial.println("[Provision] Web server started");
}

void provisionStop() {
    if (!running) return;

    if (webServer) {
        webServer->end();
        delete webServer;
        webServer = nullptr;
    }
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    running = false;
    Serial.println("[Provision] Stopped");
}

bool provisionTick() {
    if (!running) return false;
    if (dnsServer) dnsServer->processNextRequest();
    return hasCredentials;
}

void provisionDrawQR() {
    // QR content: WiFi config string
    const char* qrData = "WIFI:S:Lemon-Setup;T:WPA;P:lemon1234;;";

    // Create QR code (version 6 = 41x41 modules)
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(6)];
    qrcode_initText(&qrcode, qrcodeData, 6, ECC_LOW, qrData);

    int modules = qrcode.size;  // 41
    int pixPerModule = 5;  // More compact (was 6)
    int qrSize = modules * pixPerModule;  // 205
    int padding = 12;
    int totalSize = qrSize + 2 * padding;  // 229

    int qrX = (SCREEN_W - totalSize) / 2;
    int qrY = 90;

    // Clear screen (VSync to avoid bounce on RGB panel)
    displayWaitVSync();
    tft.fillScreen(Colors::BG_BASE);

    // ── Full imagotipo 244x56 centered at top ──
    int logoX = (SCREEN_W - 244) / 2;
    drawLemonImagotipo244(tft, logoX, 16);

    // ── Glass card containing QR code ──
    int cardW = totalSize + 24;
    int cardH = totalSize + 24;
    int cardX = (SCREEN_W - cardW) / 2;
    int cardY = qrY - 12;

    // Card border + fill
    tft.fillSmoothRoundRect(cardX, cardY, cardW, cardH, 16, Colors::CARD_BORDER);
    tft.fillSmoothRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 15, Colors::BG_CARD);

    // White QR background (rounded)
    tft.fillSmoothRoundRect(qrX, qrY, totalSize, totalSize, 8, 0xFFFF);

    // Draw QR modules
    for (int y = 0; y < modules; y++) {
        for (int x = 0; x < modules; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                tft.fillRect(qrX + padding + x * pixPerModule,
                             qrY + padding + y * pixPerModule,
                             pixPerModule, pixPerModule, 0x0000);
            }
        }
    }

    // ── Instructions with typographic hierarchy ──
    int textY = cardY + cardH + 16;

    // Title
    tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::top_center);
    tft.drawString("Configurar WiFi", SCREEN_W / 2, textY, &SatoshiMedium18);

    // Subtitle
    textY += 26;
    tft.setTextColor(Colors::TEXT_SECONDARY, Colors::BG_BASE);
    tft.drawString("Escanea el QR o conectate a:", SCREEN_W / 2, textY, &Satoshi12);

    // URL in accent color
    textY += 24;
    tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_BASE);
    tft.drawString("http://192.168.4.1", SCREEN_W / 2, textY, &SatoshiMedium18);

    // Credentials in caption style
    textY += 32;
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.drawString("Red: Lemon-Setup", SCREEN_W / 2, textY, &Satoshi9);
    textY += 14;
    tft.drawString("Clave: lemon1234", SCREEN_W / 2, textY, &Satoshi9);
}

bool provisionHasCredentials() {
    return hasCredentials;
}

void provisionGetCredentials(char* ssid, int ssidLen, char* pass, int passLen) {
    strncpy(ssid, rxSSID, ssidLen - 1);
    ssid[ssidLen - 1] = '\0';
    strncpy(pass, rxPass, passLen - 1);
    pass[passLen - 1] = '\0';
}
