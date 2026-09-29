#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "time.h"

// --- Credentials ---
const char* ssid = "";
const char* password = "";

// --- Pins ---
const int p1 = 23; 
const int p2 = 22;
const int leds[4] = {25, 26, 27, 14};

// --- NTP Thailand ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;
const int   daylightOffset_sec = 0;

// --- Logic ---
unsigned long lastCheck = 0;
const unsigned long interval = 1000UL*10UL;
unsigned long filterStart = 0;
bool isVLow = false;
String lastStatus = "";

// --- 2D History (Time, Status, S1, S2) ---
String logHistory[10][4]; 
int hIdx = 0;
bool hFull = false;

// --- LINE API ---
String TOKEN = "";
String UID = "";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

String getClock() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "Syncing...";
  char buff[20];
  strftime(buff, sizeof(buff), "%Y/%m/%d %H:%M:%S", &timeinfo);
  return String(buff);
}

void LEDs(int bin){
    for (int i = 0; i < 4; i++){
        digitalWrite(leds[i],(bin >> (3-i))&1);
    }
}

void updateDashboard(String status, int s1, int s2) {
  String t = getClock();
  logHistory[hIdx][0] = t;
  logHistory[hIdx][1] = status;
  logHistory[hIdx][2] = String(s1);
  logHistory[hIdx][3] = String(s2);
  
  hIdx = (hIdx + 1) % 10;
  if (hIdx == 0) hFull = true;

  // JSON keys: t=time, st=status, s1=sensor1, s2=sensor2
  String json = "{\"t\":\""+t+"\",\"st\":\""+status+"\",\"s1\":"+String(s1)+",\"s2\":"+String(s2)+"}";
  ws.textAll(json);
}

void sendLine(String text) {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient https;
  if (https.begin(client, "https://api.line.me/v2/bot/message/push")) {
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + TOKEN);
    String p = "{\"to\":\"" + UID + "\",\"messages\":[{\"type\":\"text\",\"text\":\"" + text + "\"}]}";
    https.POST(p);
    https.end();
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    int count = hFull ? 10 : hIdx;
    int start = hFull ? hIdx : 0;
    for (int i = 0; i < count; i++) {
      int idx = (start + i) % 10;
      String json = "{\"t\":\""+logHistory[idx][0]+"\",\"st\":\""+logHistory[idx][1]+"\",\"s1\":"+logHistory[idx][2]+",\"s2\":"+logHistory[idx][3]+"}";
      client->text(json);
      delay(50); 
    }
  }
}

const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Water Filter Monitor</title>
    <style>
        body { font-family: 'Segoe UI', sans-serif; background: #ececec; margin: 20px; color: #333; }
        h2 { text-align: center; color: #2c3e50; }
        .conn-status { text-align: center; font-size: 0.9em; font-weight: bold; margin-bottom: 20px; }
        .main-layout { display: flex; gap: 80px; justify-content: center; flex-wrap: wrap; }
        .container { width: 220px; height: 450px; display: flex; flex-direction: column; }
        .tank { flex: 1; border: 5px solid #34495e; border-radius: 5px 5px 20px 20px; background: white; display: flex; justify-content: center; align-items: center; font-weight: bold; position: relative; }
        .sensor-marker { position: absolute; right: -15px; width: 30px; height: 6px; background: #e74c3c; border-radius: 3px; display: flex; align-items: center; }
        .sensor-label { position: absolute; left: 35px; font-size: 28px; color: #e74c3c; font-weight: 900; }
        #s2-pos { bottom: 65%; } #s1-pos { bottom: 25%; }
        .log { width: 500px; height: 450px; background: white; border: 2px solid #34495e; border-radius: 10px; padding: 15px; display: flex; flex-direction: column; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
        .table-wrapper { flex: 1; overflow-y: auto; }
        table { width: 100%; border-collapse: collapse; font-size: 14px; }
        th { background: #34495e; color: white; padding: 10px; position: sticky; top: 0; }
        td { border: 1px solid #ddd; padding: 8px; text-align: center; }
        .panel { width: 250px; background: white; border: 2px solid #34495e; border-radius: 10px; padding: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
        .status, .replace-filter { padding: 15px; margin: 8px 0; border: 2px solid #ccc; text-align: center; font-weight: bold; opacity: 0.1; transition: 0.4s; border-radius: 8px; }
        .active { opacity: 1 !important; transform: scale(1.05); box-shadow: 0 4px 10px rgba(0,0,0,0.2); border-color: #000; }
        .good { background: #27ae60; color: white; } .ready { background: #f1c40f; color: black; }
        .warning { background: #e74c3c; color: white; } .replace-filter { background: #8e44ad; color: white; }
    </style>
</head>
<body>
    <h2>Water System Dashboard</h2>
    <div id="status-bar" class="conn-status" style="color: #e67e22;">CONNECTING...</div>
    <div class="main-layout">
        <div class="container">
            <div class="tank">
                <span>WATER TANK</span>
                <div id="s2-pos" class="sensor-marker"><span class="sensor-label">S<sub>2</sub></span></div>
                <div id="s1-pos" class="sensor-marker"><span class="sensor-label">S<sub>1</sub></span></div>
            </div>
        </div>
        <div class="log">
            <h3>Recent Activity</h3>
            <div class="table-wrapper">
                <table>
                    <thead><tr><th>Time</th><th>Status</th><th>S1</th><th>S2</th></tr></thead>
                    <tbody id="logTable"></tbody>
                </table>
            </div>
        </div>
        <div class="panel">
            <h3>Current State</h3>
            <div id="st-Ok" class="status good">OK</div>
            <div id="st-Low" class="status ready">LOW</div>
            <div id="st-VeryLow" class="status warning">VERY LOW</div>
            <div id="st-Filter" class="replace-filter">REPLACE FILTER</div>
        </div>
    </div>
<script>
    let socket;
    let historyLog = []; 

    function initWebSocket() {
        socket = new WebSocket('ws://' + window.location.host + '/ws');
        socket.onopen = () => {
            document.getElementById('status-bar').innerText = "SYSTEM ONLINE";
            document.getElementById('status-bar').style.color = "#27ae60";
        };
        socket.onclose = () => {
            document.getElementById('status-bar').innerText = "SYSTEM OFFLINE - RECONNECTING...";
            document.getElementById('status-bar').style.color = "#c0392b";
            setTimeout(initWebSocket, 2000);
        };
        socket.onmessage = (e) => {
            try {
                const data = JSON.parse(e.data);
                // JSON structure from ESP32: {t, st, s1, s2}
                historyLog.unshift(data);
                if(historyLog.length > 10) historyLog.pop();
                renderTable();
                updateUI(data.st);
            } catch (err) { console.error("Data Error", err); }
        };
    }

    function renderTable() {
        const tableBody = document.getElementById("logTable");
        tableBody.innerHTML = ""; 
        historyLog.forEach(entry => {
            const row = tableBody.insertRow();
            row.insertCell(0).innerText = entry.t;
            row.insertCell(1).innerText = entry.st.toUpperCase();
            row.insertCell(2).innerText = entry.s1;
            row.insertCell(3).innerText = entry.s2;
            if (entry.st.toLowerCase().includes("filter") || entry.st.toLowerCase().includes("very low")) {
                row.style.backgroundColor = "#ffebee";
                row.style.color = "#c62828";
            }
        });
    }

    function updateUI(st) {
        document.querySelectorAll('.status, .replace-filter').forEach(item => item.classList.remove('active'));
        const id = 'st-' + st.replace(/\s+/g, '');
        const target = document.getElementById(id);
        if(target) target.classList.add('active');
    }
    window.onload = initWebSocket;
</script>
</body></html>
)=====";

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println(WiFi.localIP());
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  pinMode(p1, INPUT); pinMode(p2, INPUT);
  for (int i=0; i<4; i++) pinMode(leds[i], OUTPUT);
  
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", index_html); });
  server.begin();
}

void loop() {
  ws.cleanupClients();
  if (millis() - lastCheck >= interval) {
    lastCheck = millis();
    int s1 = digitalRead(p1); int s2 = digitalRead(p2);
    String st = ""; String msg = "";
    
    if (s2 == HIGH && s1 == HIGH) { st = "Ok"; isVLow = false; LEDs(0b1000);} 
    else if (s1 == HIGH && s2 == LOW) { st = "Low"; isVLow = false; LEDs(0b0100); } 
    else {
      if (!isVLow) { filterStart = millis(); isVLow = true;  }
      if (millis() - filterStart >= (120UL*1000UL*60UL)) { st = "Filter"; msg = "Change Filter!"; LEDs(0b0001); }
      else { st = "Very Low"; msg = "Water Very Low!"; LEDs(0b0010); }
    }
    
    if (lastStatus != st) {
      updateDashboard(st, s1, s2);
      if (msg != "") sendLine(msg);
      lastStatus = st;
    }
  }
}
