#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <Adafruit_Protomatter.h>
#include <Adafruit_GFX.h>

// LED MATRIX PINS (MatrixPortal S3)
uint8_t rgbPins[]  = {42, 41, 40, 38, 39, 37}; // R1,G1,B1,R2,G2,B2
uint8_t addrPins[] = {45, 36, 48, 35, 21};     // A,B,C,D,E

uint8_t clockPin = 2;
uint8_t latchPin = 47;
uint8_t oePin    = 14;

// MATRIX OBJECT (CORRECT SIGNATURE)
Adafruit_Protomatter matrix(
  64,
  6,           
  6,           
  rgbPins,     
  5,           
  addrPins,    
  clockPin,
  latchPin,
  oePin,
  true         
);

// WIFI
const char* AP_SSID = "CommuteLive-Setup-shimu";
const char* AP_PASS = "12345678";

AsyncWebServer server(80);

// WIFI STATS
String lastError = "";
bool isConnecting = false;
unsigned long nextCatFactMs = 0;
const unsigned long CAT_FACT_INTERVAL_MS = 60000;

// DRAW TEXT FUNCTION
void drawText(const String& text) {
  matrix.fillScreen(0);
  matrix.setCursor(0, 0);
  matrix.setTextColor(matrix.color565(0, 60, 0));
  matrix.print(text);
  matrix.show();
}

String wifiDisconnectReason(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "Wrong username or password";
    case WIFI_REASON_NO_AP_FOUND:
      return "Network not found";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_ASSOC_LEAVE:
      return "Wrong username or password";
    default:
      return "Disconnected from Wi-Fi";
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && millis() - serialStart < 2000) {
    delay(10);
  }
  Serial.setDebugOutput(true);
  delay(200);
  Serial.println("Boot");

  // Start LED matrix
  if (matrix.begin() != PROTOMATTER_OK) {
    while (1);
  }

  matrix.setTextWrap(true);
  matrix.setTextSize(1);

  // Start Wi-Fi AP (keep AP+STA for recovery)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  (void)ip;


  drawText("CONNECT WIFI ENTER 192.168.4.1");

  // Wi-Fi events
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      isConnecting = false;
      lastError = "";
      nextCatFactMs = millis(); // fetch immediately on connect
      Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      isConnecting = false;
      lastError = wifiDisconnectReason(info.wifi_sta_disconnected.reason);
      Serial.printf("WiFi disconnected: %s\n", lastError.c_str());
    } else if (event == ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE) {
      lastError = "Auth mode changed";
      Serial.println("WiFi auth mode changed");
    }
  });

  // Main web page to select and connect to wifi
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html",
      "<!doctype html>"
      "<html lang='en'>"
      "<head>"
      "<meta charset='utf-8'/>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
      "<title>Wi-Fi Setup</title>"
      "<style>"
      "body{margin:0;font-family:Georgia,serif;background:#f4f1ed;min-height:100vh;display:grid;place-items:center;color:#2b2b2b}"
      ".card{width:min(680px,92vw);background:#fff7ee;border:1px solid #e2d7cc;border-radius:18px;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,.08)}"
      ".badge{display:inline-block;background:#efe1d3;color:#6b4b2a;padding:4px 10px;border-radius:999px;font-size:12px;letter-spacing:.6px;text-transform:uppercase}"
      ".status{display:flex;align-items:center;gap:10px;padding:10px 12px;background:#fff;border:1px dashed #e2d7cc;border-radius:10px;margin:10px 0 18px}"
      ".dot{width:10px;height:10px;border-radius:50%;background:#c5c5c5}"
      ".controls{display:grid;gap:12px}"
      "select,input{height:42px;border-radius:10px;border:1px solid #e2d7cc;padding:0 12px;font-size:15px;background:#fff}"
      ".row{display:flex;align-items:center;gap:12px}"
      ".btn{border:1px solid #e2d7cc;background:#f7efe7;color:#2b2b2b;padding:10px 14px;border-radius:10px;cursor:pointer}"
      ".btn.primary{background:#4c7c59;color:#fff;border-color:#4c7c59}"
      ".meta{color:#6a6a6a;font-size:13px}"
      ".error{border:1px solid #e7b3ae;background:#fdebea;color:#b43c2f;padding:10px 12px;border-radius:10px}"
      "@media (max-width:520px){.row{flex-direction:column;align-items:stretch}}"
      "</style>"
      "</head>"
      "<body>"
      "<main class='card'>"
      "<header>"
      "<div class='badge'>Device Setup</div>"
      "<h1>Connect to Wi‑Fi</h1>"
      "<p>Select a network and enter credentials. Campus Wi‑Fi with username is supported.</p>"
      "</header>"
      "<section class='status'>"
      "<div class='dot' id='statusDot'></div>"
      "<div id='statusText'>Checking status…</div>"
      "</section>"
      "<section class='controls'>"
      "<div class='row'>"
      "<button id='scanBtn' class='btn'>Scan Networks</button>"
      "<span id='scanMeta' class='meta'></span>"
      "</div>"
      "<label>Wi‑Fi Network<br><select id='ssidSelect'></select></label>"
      "<label>Password<br><input id='pass' type='password' placeholder='Leave blank if open'></label>"
      "<label>Username (optional)<br><input id='user' type='text' placeholder='For campus Wi‑Fi'></label>"
      "<div class='row'>"
      "<button id='connectBtn' class='btn primary'>Connect</button>"
      "<span id='connectMeta' class='meta'></span>"
      "</div>"
      "<div id='errorBox' class='error' style='display:none'></div>"
      "</section>"
      "</main>"
      "<script>"
      "const statusDot=document.getElementById('statusDot');"
      "const statusText=document.getElementById('statusText');"
      "const ssidSelect=document.getElementById('ssidSelect');"
      "const scanBtn=document.getElementById('scanBtn');"
      "const scanMeta=document.getElementById('scanMeta');"
      "const connectBtn=document.getElementById('connectBtn');"
      "const connectMeta=document.getElementById('connectMeta');"
      "const errorBox=document.getElementById('errorBox');"
      "function setStatus(connected,connecting,text){statusDot.style.background=connected?'#4c7c59':connecting?'#d9984a':'#c5c5c5';statusText.textContent=text;}"
      "function showError(msg){if(!msg){errorBox.style.display='none';errorBox.textContent='';return;}errorBox.style.display='block';errorBox.textContent=msg;}"
      "async function refreshStatus(){const res=await fetch('/api/status');const data=await res.json();"
      "if(data.connected){window.location.href='/hello';return;}"
      "if(data.connecting){setStatus(false,true,'Connecting…');}else{setStatus(false,false,'Not connected');}"
      "showError(data.error||'');}"
      "async function scan(){scanMeta.textContent='Scanning…';"
      "const res=await fetch('/api/scan');"
      "if(!res.ok){showError('Scan failed');scanMeta.textContent='';return;}"
      "const list=await res.json();"
      "ssidSelect.innerHTML='';list.sort((a,b)=>b.rssi-a.rssi);"
      "if(list.length===0){const opt=document.createElement('option');opt.value='';opt.textContent='No networks found';ssidSelect.appendChild(opt);scanMeta.textContent='';return;}"
      "list.forEach(net=>{const opt=document.createElement('option');opt.value=net.ssid;opt.textContent=`${net.ssid} (${net.secure?'Secure':'Open'}, ${net.rssi} dBm)`;ssidSelect.appendChild(opt);});"
      "scanMeta.textContent=`${list.length} networks`;}"
      "async function connect(){showError('');connectMeta.textContent='Sending…';"
      "const body=new URLSearchParams();body.set('ssid',ssidSelect.value);body.set('pass',document.getElementById('pass').value);body.set('user',document.getElementById('user').value);"
      "const res=await fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
      "const data=await res.json();if(!data.ok){showError(data.error||'Failed to start connection');connectMeta.textContent='';return;}"
      "connectMeta.textContent='Connecting…';}"
      "scanBtn.addEventListener('click',scan);connectBtn.addEventListener('click',connect);"
      "scan();setInterval(refreshStatus,1500);refreshStatus();"
      "</script>"
      "</body></html>"
    );
  });

  // Page thats loaded once youre connected to wifi
  server.on("/hello", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html",
      "<!doctype html><html lang='en'><head><meta charset='utf-8'/>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
      "<title>Hello</title>"
      "<style>body{margin:0;font-family:Georgia,serif;background:#f4f1ed;min-height:100vh;display:grid;place-items:center;color:#2b2b2b}"
      ".card{width:min(680px,92vw);background:#fff7ee;border:1px solid #e2d7cc;border-radius:18px;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,.08)}"
      ".badge{display:inline-block;background:#efe1d3;color:#6b4b2a;padding:4px 10px;border-radius:999px;font-size:12px;letter-spacing:.6px;text-transform:uppercase}</style>"
      "</head><body><main class='card'><header>"
      "<div class='badge'>Connected</div><h1>Hello world</h1><p>Your device is connected to Wi‑Fi.</p>"
      "</header></main></body></html>"
    );
  });

  // Scan Wi-Fi networks that are available
  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    if (n < 0) {
      request->send(500, "application/json", "{\"error\":\"Scan failed\"}");
      return;
    }
    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      wifi_auth_mode_t auth = WiFi.encryptionType(i);
      bool secure = (auth != WIFI_AUTH_OPEN);
      json += "{";
      json += "\"ssid\":\"" + ssid + "\",";
      json += "\"rssi\":" + String(rssi) + ",";
      json += "\"secure\":" + String(secure ? "true" : "false");
      json += "}";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  // Status of the wifi connection
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool connected = (WiFi.status() == WL_CONNECTED);
    String json = "{";
    json += "\"connected\":" + String(connected ? "true" : "false") + ",";
    json += "\"connecting\":" + String(isConnecting ? "true" : "false") + ",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"ip\":\"" + (connected ? WiFi.localIP().toString() : String("")) + "\",";
    json += "\"error\":\"" + lastError + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // Connect to selected wifi
  server.on("/api/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("ssid", true)) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing SSID\"}");
      return;
    }

    String ssid = request->getParam("ssid", true)->value();
    String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";
    String user = request->hasParam("user", true) ? request->getParam("user", true)->value() : "";

    lastError = "";
    isConnecting = true;

    WiFi.disconnect(true, true);
    delay(200);

    if (user.length() > 0) {
      // WPA2-Enterprise (e.g., campus Wi-Fi)
      esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)user.c_str(), user.length());
      esp_wifi_sta_wpa2_ent_set_username((uint8_t*)user.c_str(), user.length());
      if (pass.length() > 0) {
        esp_wifi_sta_wpa2_ent_set_password((uint8_t*)pass.c_str(), pass.length());
      }
      esp_wifi_sta_wpa2_ent_enable();
      WiFi.begin(ssid.c_str());
    } else {
      WiFi.begin(ssid.c_str(), pass.c_str());
    }

    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();
}

// temp function to use wifi and get cat facts for now.
void loop() {
  unsigned long now = millis();
  if (WiFi.status() == WL_CONNECTED && (long)(now - nextCatFactMs) >= 0) {
    nextCatFactMs = now + CAT_FACT_INTERVAL_MS;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (http.begin(client, "https://catfact.ninja/fact")) {
      int code = http.GET();
      if (code == HTTP_CODE_OK) {
        String body = http.getString();
        int key = body.indexOf("\"fact\":\"");
        if (key >= 0) {
          int start = key + 8;
          int end = body.indexOf("\"", start);
          if (end > start) {
            String fact = body.substring(start, end);
            drawText(fact);
          }
        }
      }
      http.end();
    }
  }
}
