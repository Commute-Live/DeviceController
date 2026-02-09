#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_Protomatter.h>
#include <Adafruit_GFX.h>

#include "Transit.h"
#include "wifi_manager.h"

// LED MATRIX PINS (MatrixPortal S3)
uint8_t rgbPins[]  = {42, 41, 40, 38, 39, 37}; // R1,G1,B1,R2,G2,B2
uint8_t addrPins[] = {45, 36, 48, 35, 21};     // A,B,C,D,E

uint8_t clockPin = 2;
uint8_t latchPin = 47;
uint8_t oePin    = 14;

// MATRIX OBJECT
Adafruit_Protomatter matrix(
  64,
  4,
  1,
  rgbPins,     
  4,           
  addrPins,    
  clockPin,
  latchPin,
  oePin,
  true         
);

AsyncWebServer server(80);
unsigned long nextCatFactMs = 0;
const unsigned long CAT_FACT_INTERVAL_MS = 60000;
bool logoDrawn = false;
bool connectDrawn = false;

void drawText(const String& text) {
  matrix.fillScreen(0);
  matrix.setTextSize(1);
  matrix.setTextWrap(false);                 
  matrix.setCursor(0, 8);                 
  matrix.setTextColor(matrix.color565(120,120,120));
  matrix.print(text.substring(0, 10));    
  matrix.show();
}


void setup() {
  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && millis() - serialStart < 2000) {
    delay(10);
  }
  Serial.setDebugOutput(true);
  delay(200);
  Serial.println("COMMUTE_LIVE: Boot");

  // Start LED matrix
  if (matrix.begin() != PROTOMATTER_OK) {
    while (1);
  }

  matrix.setTextWrap(true);
  matrix.setTextSize(1);

  wifiManagerInit(server);
  drawText("CONNECT WIFI ENTER 192.168.4.1");
  connectDrawn = true;

  // Main web page to select and connect to wifi
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>Wi-Fi Setup</title>
  <style>
    body{margin:0;font-family:Georgia,serif;background:#f4f1ed;min-height:100vh;display:grid;place-items:center;color:#2b2b2b}
    .card{width:min(680px,92vw);background:#fff7ee;border:1px solid #e2d7cc;border-radius:18px;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,.08)}
    .badge{display:inline-block;background:#efe1d3;color:#6b4b2a;padding:4px 10px;border-radius:999px;font-size:12px;letter-spacing:.6px;text-transform:uppercase}
    .status{display:flex;align-items:center;gap:10px;padding:10px 12px;background:#fff;border:1px dashed #e2d7cc;border-radius:10px;margin:10px 0 18px}
    .dot{width:10px;height:10px;border-radius:50%;background:#c5c5c5}
    .controls{display:grid;gap:12px}
    select,input{height:42px;border-radius:10px;border:1px solid #e2d7cc;padding:0 12px;font-size:15px;background:#fff}
    .row{display:flex;align-items:center;gap:12px}
    .btn{border:1px solid #e2d7cc;background:#f7efe7;color:#2b2b2b;padding:10px 14px;border-radius:10px;cursor:pointer}
    .btn.primary{background:#4c7c59;color:#fff;border-color:#4c7c59}
    .meta{color:#6a6a6a;font-size:13px}
    .error{border:1px solid #e7b3ae;background:#fdebea;color:#b43c2f;padding:10px 12px;border-radius:10px}
    @media (max-width:520px){.row{flex-direction:column;align-items:stretch}}
  </style>
</head>
<body>
  <main class="card">
    <header>
      <div class="badge">Device Setup</div>
      <h1>Connect to Wi‑Fi</h1>
      <p>Select a network and enter credentials. Campus Wi‑Fi with username is supported.</p>
    </header>
    <section class="status">
      <div class="dot" id="statusDot"></div>
      <div id="statusText">Checking status…</div>
    </section>
    <section class="controls">
      <div class="row">
        <button id="scanBtn" class="btn">Scan Networks</button>
        <span id="scanMeta" class="meta"></span>
      </div>
      <label>Wi‑Fi Network<br><select id="ssidSelect"></select></label>
      <label>Network Type<br>
        <select id="netType">
          <option value="home">Home / Personal</option>
          <option value="enterprise">Enterprise (Username)</option>
        </select>
      </label>
      <label>Password<br><input id="pass" type="password" placeholder="Leave blank if open"></label>
      <label id="userRow" style="display:none">Username<br><input id="user" type="text" placeholder="For campus Wi‑Fi"></label>
      <div class="row">
        <button id="connectBtn" class="btn primary">Connect</button>
        <span id="connectMeta" class="meta"></span>
      </div>
      <div id="errorBox" class="error" style="display:none"></div>
    </section>
  </main>
  <script>
    const statusDot=document.getElementById("statusDot");
    const statusText=document.getElementById("statusText");
    const ssidSelect=document.getElementById("ssidSelect");
    const scanBtn=document.getElementById("scanBtn");
    const scanMeta=document.getElementById("scanMeta");
    const connectBtn=document.getElementById("connectBtn");
    const connectMeta=document.getElementById("connectMeta");
    const errorBox=document.getElementById("errorBox");
    const netType=document.getElementById("netType");
    const userRow=document.getElementById("userRow");
    let scanAttempts=0;
    function updateNetType(){const isEnt=netType.value==="enterprise";userRow.style.display=isEnt?"block":"none";}
    function setStatus(connected,connecting,text){statusDot.style.background=connected?"#4c7c59":connecting?"#d9984a":"#c5c5c5";statusText.textContent=text;}
    function showError(msg){if(!msg){errorBox.style.display="none";errorBox.textContent="";return;}errorBox.style.display="block";errorBox.textContent=msg;}
    async function refreshStatus(){const res=await fetch("/api/status");const data=await res.json();
      if(data.connected){window.location.href="/hello";return;}
      if(data.connecting){setStatus(false,true,"Connecting…");}else{setStatus(false,false,"Not connected");}
      showError(data.error||"");
    }
    async function scan(){scanMeta.textContent="Scanning…";scanAttempts=0;await doScan();}
    async function doScan(){
      const res=await fetch("/api/scan");
      if(!res.ok){showError("Scan failed");scanMeta.textContent="";return;}
      const data=await res.json();
      const list=data.results||[];
      if(data.count&&list.length===0){console.warn("Scan count",data.count,"but list empty");}
      if(data.scanning&&list.length===0){
        scanAttempts++;
        scanMeta.textContent="Scanning…";
        if(scanAttempts<=10){setTimeout(doScan,800);return;}
      }
      ssidSelect.innerHTML="";
      list.sort((a,b)=>b.rssi-a.rssi);
      if(list.length===0){
        const opt=document.createElement("option");opt.value="";opt.textContent="No networks found";
        ssidSelect.appendChild(opt);scanMeta.textContent="";return;
      }
      scanAttempts=0;
      list.forEach(net=>{const opt=document.createElement("option");opt.value=net.ssid;opt.textContent=net.ssid;ssidSelect.appendChild(opt);});
      scanMeta.textContent=`${list.length} networks`;
    }
    async function connect(){
      showError("");
      connectMeta.textContent="Sending…";
      const ssid=ssidSelect.value;
      const body=new URLSearchParams();
      body.set("ssid",ssid);
      body.set("pass",document.getElementById("pass").value);
      if(netType.value==="enterprise"){body.set("user",document.getElementById("user").value);}else{body.set("user","");}
      const res=await fetch("/api/connect",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});
      const data=await res.json();
      if(!data.ok){showError(data.error||"Failed to start connection");connectMeta.textContent="";return;}
      connectMeta.textContent=ssid;
    }
    netType.addEventListener("change",updateNetType);
    updateNetType();
    scanBtn.addEventListener("click",scan);
    connectBtn.addEventListener("click",connect);
    scan();
    setInterval(refreshStatus,1500);
    refreshStatus();
  </script>
</body>
</html>
)HTML");

  // /heartbeat endpoint returns device status
  server.on("/heartbeat", HTTP_GET, [](AsyncWebServerRequest *request) {
    String deviceId = String((uint32_t)ESP.getEfuseMac(), HEX); // Unique ID from MAC
    String firmware = String("v1.0.0"); // Set your firmware version here
    int numStations = WiFi.softAPgetStationNum();
    bool connected = numStations > 0;
    String json = "{";
    json += "\"connected\":" + String(connected ? "true" : "false") + ",";
    json += "\"device_id\":\"" + deviceId + "\",";
    json += "\"firmware\":\"" + firmware + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });
  server.begin();

  });

  // Page thats loaded once youre connected to wifi
  server.on("/hello", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!wifiManagerIsConnected()) {
      request->redirect("/");
      return;
    }
    request->send(200, "text/html", R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>Hello</title>
  <style>
    body{margin:0;font-family:Georgia,serif;background:#f4f1ed;min-height:100vh;display:grid;place-items:center;color:#2b2b2b}
    .card{width:min(680px,92vw);background:#fff7ee;border:1px solid #e2d7cc;border-radius:18px;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,.08)}
    .badge{display:inline-block;background:#efe1d3;color:#6b4b2a;padding:4px 10px;border-radius:999px;font-size:12px;letter-spacing:.6px;text-transform:uppercase}
    .btn{border:1px solid #e2d7cc;background:#f7efe7;color:#2b2b2b;padding:10px 14px;border-radius:10px;cursor:pointer}
  </style>
</head>
<body>
  <main class="card">
    <header>
      <div class="badge">Connected</div>
      <h1>Hello world</h1>
      <p>Your device is connected to Wi‑Fi.</p>
      <button class="btn" id="disconnectBtn">Disconnect</button>
    </header>
  </main>
  <script>
    document.getElementById("disconnectBtn").addEventListener("click", async () => {
      await fetch("/api/disconnect", { method: "POST" });
      window.location.href = "/";
    });
  </script>
</body>
</html>
)HTML");
  });

  server.begin();
}

// temp function to use wifi and get cat facts for now.
void loop() {
  wifiManagerLoop();

  unsigned long now = millis();
  if (wifiManagerIsConnected() && !logoDrawn) {
    draw_transit_logo(12, matrix.height() / 2, 'E', "purple", 10, 60, true);
    draw_transit_logo(36, matrix.height() / 2, '7', "blue", 10, 60, false);
    Serial.printf("drew logo\n");
    delay(2000);
    logoDrawn = true;
    connectDrawn = false;
  }
  if (!wifiManagerIsConnected()) {
    logoDrawn = false;
    if (!connectDrawn) {
      drawText("CONNECT WIFI ENTER 192.168.4.1");
      connectDrawn = true;
    }
  }

  if (wifiManagerIsConnected() && (long)(now - nextCatFactMs) >= 0) {
    nextCatFactMs = now + CAT_FACT_INTERVAL_MS;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);
    HTTPClient http;
    http.setTimeout(15000);

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
            Serial.printf("Cat fact: %s\n", fact.c_str());
          }
        }
      } else {
        Serial.printf("Cat fact HTTP error: %d\n", code);
      }
      http.end();
    }
  }
}
