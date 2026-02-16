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

// ── Captive portal HTML (PROGMEM) ──
static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lemon Setup</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#000;color:#fff;font-family:-apple-system,system-ui,sans-serif;padding:16px;min-height:100vh}
.logo{text-align:center;margin:20px 0 10px}
.logo span{color:#00F068;font-size:28px;font-weight:700}
h2{text-align:center;color:#868686;font-size:14px;margin-bottom:24px;font-weight:400}
.card{background:#080C08;border:1px solid #2A2C2A;border-radius:16px;padding:16px;margin-bottom:12px}
.net{display:flex;justify-content:space-between;align-items:center;padding:14px 0;border-bottom:1px solid #222;cursor:pointer}
.net:last-child{border:none}
.net .name{font-size:16px}
.net .rssi{color:#868686;font-size:12px}
.net:hover .name{color:#00F068}
.form{display:none}
.form.active{display:block}
.form h3{font-size:16px;margin-bottom:12px;color:#00F068}
input[type=text]{width:100%;background:#1A1A1A;border:1px solid #2A2C2A;border-radius:12px;padding:14px;color:#fff;font-size:16px;margin-bottom:16px;outline:none}
input[type=text]:focus{border-color:#00F068}
button{width:100%;background:#00F068;color:#000;border:none;border-radius:12px;padding:14px;font-size:16px;font-weight:600;cursor:pointer}
button:active{opacity:0.8}
.back{background:none;color:#868686;border:1px solid #2A2C2A;margin-top:8px}
.status{text-align:center;padding:40px 0}
.status .spin{display:inline-block;width:32px;height:32px;border:3px solid #2A2C2A;border-top-color:#00F068;border-radius:50%;animation:sp .8s linear infinite}
@keyframes sp{to{transform:rotate(360deg)}}
.status p{margin-top:16px;color:#868686}
</style>
</head>
<body>
<div class="logo"><span>lemon</span></div>
<h2>Selecciona tu red WiFi</h2>
<div id="list" class="card"></div>
<div id="form" class="form card">
<h3 id="selNet"></h3>
<input type="text" id="pw" placeholder="Contrasena" autocomplete="off">
<button onclick="send()">Conectar</button>
<button class="back" onclick="back()">Volver</button>
</div>
<div id="status" class="form">
<div class="status"><div class="spin"></div><p>Conectando...</p></div>
</div>
<script>
let sel='';
function init(){fetch('/scan').then(r=>r.json()).then(d=>{
let h='';d.forEach(n=>{h+='<div class="net" onclick="pick(\''+n.s.replace(/'/g,"\\'")+'\')"><span class="name">'+n.s+'</span><span class="rssi">'+n.r+' dBm</span></div>'});
document.getElementById('list').innerHTML=h||'<p style="color:#868686;text-align:center;padding:20px">No se encontraron redes</p>';})}
function pick(s){sel=s;document.getElementById('selNet').textContent=s;document.getElementById('list').style.display='none';document.getElementById('form').classList.add('active')}
function back(){document.getElementById('form').classList.remove('active');document.getElementById('list').style.display='block';document.getElementById('pw').value=''}
function send(){let p=document.getElementById('pw').value;document.getElementById('form').classList.remove('active');document.getElementById('status').classList.add('active');
fetch('/connect?ssid='+encodeURIComponent(sel)+'&pass='+encodeURIComponent(p)).then(()=>{document.querySelector('.status p').textContent='Credenciales guardadas. El dispositivo se reiniciara.'}).catch(()=>{document.querySelector('.status p').textContent='Error. Intenta de nuevo.'})}
init();
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
    int pixPerModule = 6;
    int qrSize = modules * pixPerModule;  // 246
    int padding = 12;
    int totalSize = qrSize + 2 * padding;

    int qrX = (SCREEN_W - totalSize) / 2;
    int qrY = 60;

    // Clear screen
    tft.fillScreen(Colors::BG_BASE);

    // Isotipo + "lemon" title at top
    int headerY = 10;
    drawLemonIsotipo28(tft, SCREEN_W / 2 - 70, headerY);
    tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_BASE);
    tft.setTextDatum(lgfx::middle_left);
    tft.drawString("lemon", SCREEN_W / 2 - 70 + 28 + 8, headerY + 14, &SatoshiMedium18);

    // White background for QR
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

    // Instructions below QR
    int textY = qrY + totalSize + 12;
    tft.setTextColor(Colors::TEXT_PRIMARY, Colors::BG_BASE);
    tft.setTextDatum(lgfx::top_center);
    tft.drawString("1. Escanea el QR con tu celular", SCREEN_W / 2, textY, &Satoshi12);

    textY += 22;
    tft.drawString("2. Abre el navegador en:", SCREEN_W / 2, textY, &Satoshi12);

    // URL prominente
    textY += 24;
    tft.setTextColor(Colors::LEMON_GREEN, Colors::BG_BASE);
    tft.drawString("http://192.168.4.1", SCREEN_W / 2, textY, &SatoshiMedium18);

    // Fallback manual
    textY += 32;
    tft.setTextColor(Colors::TEXT_TERTIARY, Colors::BG_BASE);
    tft.drawString("O conectate manualmente:", SCREEN_W / 2, textY, &Satoshi9);
    textY += 16;
    tft.drawString("Red: Lemon-Setup  |  Clave: lemon1234", SCREEN_W / 2, textY, &Satoshi9);
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
