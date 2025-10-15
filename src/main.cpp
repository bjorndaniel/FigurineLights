#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_log.h>
#include <time.h>
#include "LedController.h"
#include "MqttClient.h"

// Global objects
LedController ledController;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
MqttClient *mqttClient = nullptr;

// Configuration
#define RESET_BUTTON_PIN 0
#define STATUS_LED_PIN 2

// WiFi AP settings
const char *AP_SSID = "FigurineLights-Setup";
const char *AP_PASSWORD = "12345678";

// Variables
bool isAccessPoint = false;
String savedSSID = "";
String savedPassword = "";

// Status tracking
struct StatusEntry
{
    String action;
    unsigned long timestamp;
};
StatusEntry statusHistory[5];
int statusIndex = 0;

// Simple in-memory debug log buffer (most recent 50 entries)
#define LOG_BUFFER_SIZE 50
String debugLog[LOG_BUFFER_SIZE];
int debugLogIndex = 0;
bool debugEnabled = false;

// Function declarations
void setupWiFi();
void startAccessPoint();
bool connectToWiFi(const String &ssid, const String &password);
void setupWebServer();
void handleRoot();
void handleSetup();
void handleConnect();
void handleStatus();
void handleGroup();
void handleAllOn();
void handleAllOff();
void handleAllBrightness();
void handleInfo();
void handleReset();
void handleSettings();
void handleSaveSettings();
void handleGetSettings();
void handleLogs();
void addStatusEntry(const String &action);

// Persistence helpers
void saveSettings();
void loadSettings();

// Save current groups and global settings to Preferences as JSON
void saveSettings()
{
    // Persist the canonical status JSON produced by the LED controller
    String out = ledController.getAllStatus();
    preferences.putString("settings", out);
    Serial.println("Settings saved");
    Serial.println(out);
    // also persist debug flag
    preferences.putBool("debug", debugEnabled);
}

// Load settings from Preferences and apply to LED controller
void loadSettings()
{
    String s = preferences.getString("settings", "");
    if (s.length() == 0)
    {
        Serial.println("No saved settings found");
        return;
    }
    // Parse the canonical JSON produced by getAllStatus(): {"groups":[{...},{...}]}
    int pos = s.indexOf('[');
    if (pos == -1) {
        Serial.println("Saved settings malformed: no array");
        return;
    }
    pos++; // move past '['
    int groupIdx = 0;
    while (groupIdx < NUM_GROUPS && pos < (int)s.length()) {
        // find next object by scanning braces
        while (pos < (int)s.length() && s.charAt(pos) != '{') pos++;
        if (pos >= (int)s.length()) break;
        int depth = 0;
        int start = pos;
        int end = -1;
        for (int i = pos; i < (int)s.length(); i++) {
            char c = s.charAt(i);
            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) { end = i; break; }
            }
        }
        if (end == -1) break;
        String obj = s.substring(start + 1, end);
        auto extractInt = [&](const String &key, int defaultVal)->int {
            String keyPattern;
            keyPattern.reserve(key.length() + 2);
            keyPattern = "\"";
            keyPattern += key;
            keyPattern += "\"";
            int k = obj.indexOf(keyPattern);
            if (k == -1) return defaultVal;
            int colon = obj.indexOf(':', k);
            if (colon == -1) return defaultVal;
            int start = colon + 1;
            while (start < (int)obj.length() && isspace(obj.charAt(start))) start++;
            int finish = start;
            while (finish < (int)obj.length() && (isDigit(obj.charAt(finish)) || obj.charAt(finish) == '-')) finish++;
            String v = obj.substring(start, finish);
            v.trim();
            if (v.length() == 0) return defaultVal;
            return v.toInt();
        };
        auto extractBool = [&](const String &key, bool defaultVal)->bool {
            String keyPattern;
            keyPattern.reserve(key.length() + 2);
            keyPattern = "\"";
            keyPattern += key;
            keyPattern += "\"";
            int k = obj.indexOf(keyPattern);
            if (k == -1) return defaultVal;
            int colon = obj.indexOf(':', k);
            if (colon == -1) return defaultVal;
            int start = colon + 1;
            while (start < (int)obj.length() && isspace(obj.charAt(start))) start++;
            if (obj.startsWith("true", start)) return true;
            if (obj.startsWith("false", start)) return false;
            return defaultVal;
        };

        int r = extractInt("r", 0);
        int g = extractInt("g", 0);
        int b = extractInt("b", 0);
        int brightness = extractInt("brightness", 128);
        bool on = extractBool("isOn", false);

        ledController.setGroupColor(groupIdx, r, g, b);
        ledController.setGroupBrightness(groupIdx, brightness);
        ledController.setGroupState(groupIdx, on);

        groupIdx++;
        pos = end + 1;
    }

    Serial.println("Settings loaded and applied");

    // load debug flag
    debugEnabled = preferences.getBool("debug", false);
}

// Handle setting brightness for all groups
void handleAllBrightness()
{
    String body = server.arg("plain");
    // Expect JSON: {"brightness":NN}
    int idx = body.indexOf("\"brightness\":");
    if (idx == -1)
    {
        server.send(400, "text/plain", "Missing brightness");
        return;
    }
    int start = idx + 13;
    int end = body.indexOf(',', start);
    if (end == -1) end = body.indexOf('}', start);
    int brightness = body.substring(start, end).toInt();
    if (brightness < 1) brightness = 1;
    if (brightness > 100) brightness = 100;

    // Convert percentage (1-100) to 0-255 LED brightness scale
    int brightness255 = (brightness * 255 + 50) / 100; // rounded
    if (brightness255 < 1) brightness255 = 1;
    if (brightness255 > 255) brightness255 = 255;

    for (int i = 0; i < NUM_GROUPS; i++)
    {
        LedGroup prev = ledController.getGroup(i);
        if (prev.brightness != brightness255) {
            ledController.setGroupBrightness(i, (uint8_t)brightness255);
        }
    }

    String msg = String("All brightness set to ") + String(brightness) + "%";
    addStatusEntry(msg);
    server.send(200, "text/plain", "Brightness set");
    saveSettings();
}

void setup()
{
    Serial.begin(115200);

    // Reduce log verbosity to avoid WiFiUdp spam
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("WiFiUdp", ESP_LOG_NONE);

    Serial.println("Starting Figurine Lights Controller...");

    // Initialize LED controller
    ledController.init();

    // Initialize preferences early so loadSettings can read persisted values
    preferences.begin("wificonfig", false);
    savedSSID = preferences.getString("ssid", "");
    savedPassword = preferences.getString("password", "");

    // Load persisted settings (if any)
    loadSettings();

    Serial.println("Testing LEDs...");
    Serial.printf("LED PIN: %d, NUM_LEDS: %d\n", LED_PIN, 4);

    // Test each group individually
    for (int i = 0; i < 4; i++)
    {
        Serial.printf("Setting group %d to white, brightness 100\n", i);
        ledController.setGroupColor(i, 255, 255, 255);
        ledController.setGroupBrightness(i, 100);
        ledController.setGroupState(i, true);
        delay(200);
    }

    Serial.println("All LEDs should now be white for 2 seconds...");
    delay(2000);

    // Turn off all LEDs after test
    ledController.setAllOff();
    Serial.println("Test complete, LEDs turned off");

    // preferences already initialized earlier

    // Initialize MQTT client (reads broker from preferences key 'mqtt_broker')
    String broker = preferences.getString("mqtt_broker", "");
    int mqtt_port = preferences.getInt("mqtt_port", 1883);
    bool mqtt_tls = preferences.getBool("mqtt_tls", false);
    mqttClient = new MqttClient(ledController, preferences);
    if (broker.length() > 0)
    {
        mqttClient->begin(broker.c_str(), mqtt_port, mqtt_tls);
    }

    // Initialize WiFi
    setupWiFi();

    // Setup web server
    setupWebServer();
    // If mqttClient was created and not begun with host earlier, attempt default to localhost
    if (mqttClient && broker.length() == 0)
    {
        // try local broker
        mqttClient->begin("192.168.1.2", 1883, false);
    }
    // Register brightness endpoint (handler is defined above)
    server.on("/api/all/brightness", HTTP_POST, handleAllBrightness);

    Serial.println("Setup complete!");
}

void loop()
{
    static int dnsCounter = 0;

    // Only process DNS requests every 100ms instead of every 10ms to reduce log spam
    if (dnsCounter++ >= 10)
    {
        dnsServer.processNextRequest();
        dnsCounter = 0;
    }

    server.handleClient();
    if (mqttClient) mqttClient->loop();
    delay(10);
}

void setupWiFi()
{
    if (savedSSID.length() > 0)
    {
        Serial.println("Attempting to connect to saved WiFi...");
        if (connectToWiFi(savedSSID, savedPassword))
        {
            Serial.println("Connected to WiFi successfully!");
            Serial.println("IP address: " + WiFi.localIP().toString());
            isAccessPoint = false;
            return;
        }
    }

    Serial.println("Starting Access Point mode...");
    startAccessPoint();
}

void startAccessPoint()
{
    isAccessPoint = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    IPAddress IP = WiFi.softAPIP();
    Serial.println("AP IP address: " + IP.toString());
    dnsServer.start(53, "*", IP);
}

bool connectToWiFi(const String &ssid, const String &password)
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    return WiFi.status() == WL_CONNECTED;
}

void setupWebServer()
{
    server.on("/", handleRoot);
    server.on("/setup", handleSetup);
    server.on("/connect", HTTP_POST, handleConnect);
    server.on("/api/status", handleStatus);
    server.on("/api/group", HTTP_POST, handleGroup);
    server.on("/api/all/on", HTTP_POST, handleAllOn);
    server.on("/api/all/off", HTTP_POST, handleAllOff);
    server.on("/api/reset", HTTP_POST, handleReset);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/logs", HTTP_GET, handleLogs);
    server.on("/api/prefs", HTTP_GET, [](){
        String out = "{";
        out += "\"mqtt_broker\":\"" + preferences.getString("mqtt_broker", "") + "\",";
        out += "\"mqtt_port\":" + String(preferences.getInt("mqtt_port", 1883)) + ",";
        out += "\"mqtt_tls\":" + String(preferences.getBool("mqtt_tls", false) ? "true" : "false") + ",";
        out += "\"mqtt_user\":\"" + preferences.getString("mqtt_user", "") + "\",";
        String settingsRaw = preferences.getString("settings", "");
        out += "\"settings_raw\":\"" + settingsRaw + "\"";
        out += "}";
        server.send(200, "application/json", out);
    });
    server.on("/api/clientlog", HTTP_POST, [](){
        String body = server.arg("plain");
        if (body.length() == 0) { server.send(400, "text/plain", "Empty"); return; }
        // Use the central logging function so client logs get timestamped and recorded consistently
        addStatusEntry(String("CLIENT: ") + body);
        server.send(200, "text/plain", "OK");
    });
    server.on("/settings", handleSettings);
    server.on("/api/settings", HTTP_POST, handleSaveSettings);
    server.begin();
    Serial.println("Web server started");
}

void handleRoot()
{
    if (isAccessPoint)
    {
        handleSetup();
        return;
    }

    String html = "<!DOCTYPE html><html><head><title>Figurine Lights</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial;background:#1a1a1a;color:white;padding:20px}";
    html += ".container{max-width:1200px;margin:0 auto}";
    html += "h1{text-align:center;color:#2196F3}";
    html += ".groups{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px}";
    html += ".group{background:#2d2d2d;padding:20px;border-radius:10px;border:2px solid #404040}";
    html += ".group.active{border-color:#2196F3}";
    html += ".group-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:15px}";
    html += ".power-btn{width:50px;height:25px;border:none;border-radius:15px;cursor:pointer}";
    html += ".power-btn.on{background:#4CAF50}.power-btn.off{background:#666}";
    html += ".control-row{display:flex;gap:15px;align-items:center;margin:10px 0}";
    html += ".control-row label{min-width:80px}";
    html += ".color-input{width:60px;height:40px;border:none;border-radius:5px}";
    html += ".range-input{flex:1}";
    html += ".brightness-val{min-width:50px;text-align:right}";
    html += ".status-text{margin-top:15px;padding:8px;background:#1a1a1a;border-radius:5px;font-family:monospace;font-size:12px;color:#888}";
    html += ".btn{padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px}";
    html += ".btn-success{background:#4CAF50;color:white}";
    html += ".btn-danger{background:#f44336;color:white}";
    html += ".btn-warning{background:#ff9800;color:white}";
    html += "#spinner{opacity:0;visibility:hidden;transition:opacity .25s ease,visibility .25s ease;position:fixed;top:16px;right:16px;width:40px;height:40px;border-radius:50%;border:5px solid rgba(255,255,255,0.15);border-top-color:#2196F3;animation:spin 1s linear infinite;z-index:9999;}";
    html += "#backdrop{position:fixed;inset:0;background:rgba(0,0,0,0.35);backdrop-filter:blur(4px);opacity:0;visibility:hidden;transition:opacity .25s ease,visibility .25s ease;z-index:9998;pointer-events:none;}";
    html += "#spinnerBox{opacity:0;visibility:hidden;transition:opacity .25s ease,visibility .25s ease;position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);width:320px;max-width:90%;height:96px;border-radius:8px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;padding:12px;background:rgba(0,0,0,0.75);z-index:9999;}";
    html += "@keyframes spin{to{transform:rotate(360deg)}} .busy #spinner{opacity:1;visibility:visible;} .busy #spinnerBox{opacity:1;visibility:visible;} .busy #backdrop{opacity:1;visibility:visible;pointer-events:auto;} .busy button, .busy input[type=range], .busy input[type=color]{pointer-events:none;opacity:0.6;}";
    html += "</style></head><body>";
    html += "<h1>Figurine Lights Controller</h1>";
    // spinner + backdrop
    html += "<div id='backdrop'></div>";
    html += "<div style='display:flex;align-items:center;gap:12px;'>";
        html += "<div id='spinnerBox' title='Working...'><div id='spinner' style='width:40px;height:40px;border-radius:50%;border:5px solid rgba(255,255,255,0.15);border-top-color:#2196F3;animation:spin 1s linear infinite;'></div><div id='spinnerText' style='color:#fff;font-size:12px;margin-top:6px;text-align:center;'></div></div>";
    html += "<button class='btn btn-success' onclick='allOn()'>All On</button>";
    html += "<button class='btn btn-danger' onclick='allOff()'>All Off</button>";
    html += "<button class='btn btn-warning' onclick='resetWifi()' style='margin-left: 20px; font-weight: bold;'>&#9888; Reset WiFi</button>";
    html += "</div>";
    html += "<div style='text-align:right;'>";
    html += "<button class='btn btn-warning' onclick=\"location.href='/settings'\">Settings</button>";
    html += "</div>";
    html += "</div>";
    // Global brightness row
    html += "<div style='margin-top:12px;display:flex;align-items:center;gap:12px;'>";
    html += "<label style='min-width:140px;font-size:14px;color:#ccc;'>Global Brightness</label>";
    html += "<input id='allBrightness' type='range' min='1' max='100' value='100' oninput='updateAllBrightnessLabel(this.value)' style='flex:1;'>";
    html += "<span id='allBrightnessLabel' style='min-width:50px;text-align:right;color:#ccc;'>100%</span>";
    html += "<button class='btn btn-warning' onclick='resetBrightness()' style='margin-left:10px;'>Reset Brightness</button>";
    html += "</div>";
    html += "<div class='groups' id='groups'></div>";
    // Debug log panel placeholder
    html += "<div id='logPanel' style='margin-top:16px;display:none;background:#111;padding:10px;border-radius:8px;color:#9e9e9e;font-family:monospace;max-height:200px;overflow:auto;'></div>";
    html += "</div>";

    // JavaScript
    html += "<script>";
    html += "let status={};";
    html += "let allBrightnessDebounce=null;";
    html += "let debugEnabled=false;let logPoll=null;";
    html += "function init(){createGroups();fetch('/api/settings').then(r=>r.json()).then(s=>{debugEnabled=!!s.debug;document.getElementById('logPanel').style.display=debugEnabled?'block':'none';if(debugEnabled){pollLogs();logPoll=setInterval(pollLogs,3000);}else{if(logPoll){clearInterval(logPoll);logPoll=null;}}});loadStatus();setInterval(loadStatus,3000);document.getElementById('allBrightness').addEventListener('input',function(e){updateAllBrightnessLabel(e.target.value);if(allBrightnessDebounce)clearTimeout(allBrightnessDebounce);allBrightnessDebounce=setTimeout(()=>setAllBrightness(e.target.value),300);});document.addEventListener('visibilitychange',function(){if(!document.hidden){fetch('/api/settings').then(r=>r.json()).then(s=>{debugEnabled=!!s.debug;document.getElementById('logPanel').style.display=debugEnabled?'block':'none';if(debugEnabled){pollLogs();if(!logPoll)logPoll=setInterval(pollLogs,3000);}else{if(logPoll){clearInterval(logPoll);logPoll=null;}}});loadStatus();}});}";
    html += "function createGroups(){";
    html += "const container=document.getElementById('groups');";
    html += "for(let i=0;i<4;i++){";
    html += "const div=document.createElement('div');";
    html += "div.className='group';div.id='group'+i;";
    html += "div.innerHTML='<div class=\"group-header\"><h3>Group '+(i+1)+'</h3><button class=\"power-btn off\" onclick=\"toggleGroup('+i+')\" id=\"power'+i+'\"></button></div><div class=\"control-row\"><label>Color:</label><input type=\"color\" class=\"color-input\" id=\"color'+i+'\" onchange=\"updateColor('+i+')\"></div><div class=\"control-row\"><label>Brightness:</label><input type=\"range\" class=\"range-input\" min=\"0\" max=\"255\" id=\"brightness'+i+'\" oninput=\"updateBrightnessDisplay('+i+')\" onchange=\"updateBrightness('+i+')\"><span class=\"brightness-val\" id=\"brightVal'+i+'\">50%</span></div><div class=\"status-text\" id=\"status'+i+'\">OFF</div>';";
    html += "container.appendChild(div);}}";
    html += "function loadStatus(){fetch('/api/status').then(r=>r.json()).then(data=>{status=data;updateUI(data);});}";
    html += "function pollLogs(){fetch('/api/logs').then(r=>{if(r.status==200)return r.json();return Promise.resolve([]);} ).then(lines=>{const panel=document.getElementById('logPanel');panel.innerHTML='';lines.forEach(l=>{const div=document.createElement('div');div.textContent=l;panel.appendChild(div);});});}";
    html += "function setBusy(on){document.body.classList.toggle('busy', !!on); /* disable/enable interactive elements for older browsers */ const els=document.querySelectorAll('button,input[type=range],input[type=color]'); els.forEach(function(e){try{e.disabled=on;}catch(x){} });}";
    html += "function apiPost(url, opts, action){setBusy(true);document.getElementById('spinnerText').textContent = action||'Working...';opts=opts||{};opts.method=opts.method||'POST';opts.headers=opts.headers||{};return fetch(url,opts).then(function(r){setBusy(false);if(!r.ok)throw new Error('HTTP '+r.status);return r;}).then(function(r){try{fetch('/api/clientlog',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:action||'',url:url,body:(opts.body||'')})});}catch(e){};return r;}).catch(function(err){setBusy(false);console.error(err);try{fetch('/api/clientlog',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:action||'error',url:url,error:err.message})});}catch(e){};throw err;});}";
    html += "function updateUI(data){data.groups.forEach((group,i)=>{";
    html += "const groupEl=document.getElementById('group'+i);";
    html += "const powerBtn=document.getElementById('power'+i);";
    html += "const colorInput=document.getElementById('color'+i);";
    html += "const brightnessInput=document.getElementById('brightness'+i);";
    html += "const brightVal=document.getElementById('brightVal'+i);";
    html += "const statusText=document.getElementById('status'+i);";
    html += "groupEl.className='group'+(group.isOn?' active':'');";
    html += "powerBtn.className='power-btn '+(group.isOn?'on':'off');";
    html += "const hex='#'+((1<<24)+(group.color.r<<16)+(group.color.g<<8)+group.color.b).toString(16).slice(1);";
    html += "colorInput.value=hex;";
    html += "brightnessInput.value=group.brightness;brightVal.textContent=Math.round(group.brightness/255*100)+'%';";
    html += "statusText.textContent=group.isOn?'ON (R'+group.color.r+',G'+group.color.g+',B'+group.color.b+',Br'+group.brightness+')':'OFF';});";
    // Initialize global slider from average brightness
    html += "let avg=0;data.groups.forEach(g=>avg+=g.brightness);avg=Math.round((avg/data.groups.length)/255*100);document.getElementById('allBrightness').value=avg;document.getElementById('allBrightnessLabel').textContent=avg+'%';";
    html += "}";
    html += "function toggleGroup(i){const isOn=status.groups[i].isOn;sendCommand({group:i,isOn:!isOn});}";
    html += "function updateColor(i){const hex=document.getElementById('color'+i).value;const r=parseInt(hex.slice(1,3),16);const g=parseInt(hex.slice(3,5),16);const b=parseInt(hex.slice(5,7),16);sendCommand({group:i,color:{r:r,g:g,b:b}});}";
    html += "function resetBrightness(){document.getElementById('allBrightness').value=100;document.getElementById('allBrightnessLabel').textContent='100%';setAllBrightness(100);}";
    html += "function updateBrightnessDisplay(i){const val=parseInt(document.getElementById('brightness'+i).value);const percent=Math.round(val/255*100);document.getElementById('brightVal'+i).textContent=percent+'%';}";
    html += "function updateBrightness(i){const val=parseInt(document.getElementById('brightness'+i).value);const percent=Math.round(val/255*100);document.getElementById('brightVal'+i).textContent=percent+'%';sendCommand({group:i,brightness:val});}";
    html += "function allOn(){apiPost('/api/all/on',{headers:{}},'Turning all on').then(()=>setTimeout(loadStatus,100)).catch(e=>alert('Failed: '+e.message));}";
    html += "function allOff(){apiPost('/api/all/off',{headers:{}},'Turning all off').then(()=>setTimeout(loadStatus,100)).catch(e=>alert('Failed: '+e.message));}";
    html += "function updateAllBrightnessLabel(v){document.getElementById('allBrightnessLabel').innerText=v;}";
    html += "function setAllBrightness(v){apiPost('/api/all/brightness',{headers:{'Content-Type':'application/json'},body:JSON.stringify({brightness:parseInt(v)})},'Setting global brightness to '+v+'%').then(()=>setTimeout(loadStatus,200)).catch(e=>alert('Failed: '+e.message));}";
    html += "function resetWifi(){";
    html += "if(confirm('WARNING: Reset WiFi Settings?\\n\\nThis will:\\n- Clear saved WiFi credentials\\n- Restart the device\\n- Return to setup mode\\n\\nAre you sure?')){";
    html += "if(confirm('FINAL CONFIRMATION:\\n\\nThis action cannot be undone!\\n\\nClick OK to proceed with WiFi reset.')){";
    html += "apiPost('/api/reset',{},'Resetting WiFi settings').then(()=>{alert('WiFi reset initiated! Device restarting in 3 seconds...');}).catch(e=>alert('Failed: '+e.message));";
    html += "}else{alert('WiFi reset cancelled.');}";
    html += "}else{alert('WiFi reset cancelled.');}}";
    html += "function sendCommand(data){let label=''; if(data.hasOwnProperty('isOn')) label='Toggle group '+(data.group+1)+' to '+(data.isOn?'ON':'OFF'); else if(data.hasOwnProperty('brightness')) label='Set group '+(data.group+1)+' brightness to '+Math.round(data.brightness/255*100)+'%'; else if(data.hasOwnProperty('color')) label='Set group '+(data.group+1)+' color to rgb('+data.color.r+','+data.color.g+','+data.color.b+')'; else label='Update group '+(data.group+1); apiPost('/api/group',{headers:{'Content-Type':'application/json'},body:JSON.stringify(data)},label).then(()=>setTimeout(loadStatus,500)).catch(e=>alert('Failed: '+e.message));}";
    html += "document.addEventListener('DOMContentLoaded',init);";
    html += "</script></body></html>";

    server.send(200, "text/html", html);
}

void handleSetup()
{
    String html = "<!DOCTYPE html><html><head><title>WiFi Setup</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;padding:20px;background:#1a1a1a;color:white}";
    html += ".container{max-width:400px;margin:0 auto}";
    html += "h1{color:#2196F3;text-align:center}";
    html += ".form-group{margin:15px 0}";
    html += "label{display:block;margin-bottom:5px}";
    html += "input{width:100%;padding:10px;border:1px solid #ccc;border-radius:5px;box-sizing:border-box}";
    html += ".btn{background:#2196F3;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;width:100%}";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>Figurine Lights Setup</h1>";
    html += "<form action='/connect' method='POST'>";
    html += "<div class='form-group'><label>WiFi Network:</label><input type='text' name='ssid' required></div>";
    html += "<div class='form-group'><label>Password:</label><input type='password' name='password'></div>";
    html += "<button type='submit' class='btn'>Connect</button>";
    html += "</form></div></body></html>";

    server.send(200, "text/html", html);
}

void handleConnect()
{
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    if (connectToWiFi(ssid, password))
    {
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);

        String html = "<!DOCTYPE html><html><head><title>Success</title>";
        html += "<style>body{font-family:Arial;padding:20px;background:#1a1a1a;color:white;text-align:center}</style></head><body>";
        html += "<h1>Connected Successfully!</h1>";
        html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
        html += "</body></html>";

        server.send(200, "text/html", html);
        dnsServer.stop();
        isAccessPoint = false;
    }
    else
    {
        server.send(200, "text/html", "<h1>Connection Failed!</h1><a href='/setup'>Try Again</a>");
    }
}

void handleStatus()
{
    String ledStatus = ledController.getAllStatus();
    server.send(200, "application/json", ledStatus);
}

void handleGroup()
{
    String body = server.arg("plain");
    Serial.printf("handleGroup received: %s\n", body.c_str());

    // helper: find a numeric value for a key anywhere in the JSON-like body
    auto extractNumber = [&](const String &key, int defaultVal)->int {
    String keyPattern;
    keyPattern.reserve(key.length() + 2);
    keyPattern = "\"";
    keyPattern += key;
    keyPattern += "\"";
    int k = body.indexOf(keyPattern);
        if (k == -1) return defaultVal;
        int colon = body.indexOf(':', k);
        if (colon == -1) return defaultVal;
        int start = colon + 1;
        // skip whitespace
        while (start < (int)body.length() && isspace(body.charAt(start))) start++;
        int end = start;
        // accept negative? unlikely
        while (end < (int)body.length() && (isDigit(body.charAt(end)) || body.charAt(end)=='-')) end++;
        String val = body.substring(start, end);
        val.trim();
        if (val.length() == 0) return defaultVal;
        return val.toInt();
    };

    // helper: extract boolean value
    auto extractBool = [&](const String &key, bool defaultVal)->bool {
    String keyPattern;
    keyPattern.reserve(key.length() + 2);
    keyPattern = "\"";
    keyPattern += key;
    keyPattern += "\"";
    int k = body.indexOf(keyPattern);
        if (k == -1) return defaultVal;
        int colon = body.indexOf(':', k);
        if (colon == -1) return defaultVal;
        int start = colon + 1;
        while (start < (int)body.length() && isspace(body.charAt(start))) start++;
        if (body.startsWith("true", start)) return true;
        if (body.startsWith("false", start)) return false;
        return defaultVal;
    };

    int group = extractNumber("group", -1);
    if (group == -1) {
        Serial.println("handleGroup: group not found in payload");
        server.send(400, "text/plain", "Missing group");
        return;
    }

    // handle on/off
    if (body.indexOf("\"isOn\":") != -1) {
        bool isOn = extractBool("isOn", false);
        Serial.printf("Parsed isOn=%s for group %d\n", isOn?"true":"false", group);
        // log previous state
        LedGroup prev = ledController.getGroup(group);
        bool prevOn = prev.isOn;
        if (prevOn != isOn) {
            ledController.setGroupState(group, isOn);
            String msg = String("Group ") + String(group + 1) + String(" state: ") + (prevOn?"ON":"OFF") + " -> " + (isOn?"ON":"OFF");
            addStatusEntry(msg);
        } else {
            // no change
            Serial.println("No state change");
        }
        saveSettings();
    }

    // handle brightness
    if (body.indexOf("\"brightness\":") != -1) {
        int brightness = extractNumber("brightness", -1);
        if (brightness >= 0) {
            Serial.printf("Parsed brightness=%d for group %d\n", brightness, group);
            // log previous brightness
            LedGroup prev = ledController.getGroup(group);
            int prevBr = prev.brightness;
            if (prevBr != brightness) {
                ledController.setGroupBrightness(group, brightness);
                int prevPct = (prevBr * 100 + 127) / 255;
                int newPct = (brightness * 100 + 127) / 255;
                String msg = String("Group ") + String(group + 1) + String(" brightness: ") + String(prevPct) + "% -> " + String(newPct) + "%";
                addStatusEntry(msg);
            }
            saveSettings();
        }
    }

    // handle color object
    int colorStart = body.indexOf(String("\"color\""));
    if (colorStart != -1) {
        int braceOpen = body.indexOf('{', colorStart);
        int braceClose = body.indexOf('}', braceOpen);
        if (braceOpen != -1 && braceClose != -1) {
            String obj = body.substring(braceOpen + 1, braceClose);
            auto extractFromObj = [&](const String &k)->int {
                int kk = obj.indexOf(String("\"") + k + String("\""));
                if (kk == -1) return -1;
                int colon = obj.indexOf(':', kk);
                if (colon == -1) return -1;
                int start = colon + 1;
                while (start < (int)obj.length() && isspace(obj.charAt(start))) start++;
                int end = start;
                while (end < (int)obj.length() && (isDigit(obj.charAt(end)) || obj.charAt(end)=='-')) end++;
                String v = obj.substring(start, end);
                v.trim();
                if (v.length() == 0) return -1;
                return v.toInt();
            };
            int r = extractFromObj("r");
            int g = extractFromObj("g");
            int b = extractFromObj("b");
            Serial.printf("Parsed color r=%d g=%d b=%d for group %d\n", r, g, b, group);
            if (r >= 0 && g >= 0 && b >= 0) {
                // log previous color
                LedGroup prev = ledController.getGroup(group);
                int pr = prev.color.r;
                int pg = prev.color.g;
                int pb = prev.color.b;
                if (pr != r || pg != g || pb != b) {
                    ledController.setGroupColor(group, r, g, b);
                    char bufOld[32];
                    char bufNew[32];
                    snprintf(bufOld, sizeof(bufOld), "rgb(%d,%d,%d)", pr, pg, pb);
                    snprintf(bufNew, sizeof(bufNew), "rgb(%d,%d,%d)", r, g, b);
                    String msg = String("Group ") + String(group + 1) + String(" color: ") + String(bufOld) + " -> " + String(bufNew);
                    addStatusEntry(msg);
                }
                saveSettings();
            }
        }
    }

    // respond with the group status so frontend can refresh
    String status = ledController.getGroupStatus(group);
    Serial.println(status);
    server.send(200, "application/json", status);
}

void handleAllOn()
{
    // Log previous states and set all groups on
    String changes = "All ON:";
    for (int i = 0; i < NUM_GROUPS; i++) {
        LedGroup prev = ledController.getGroup(i);
        if (!prev.isOn) {
            changes += " " + String(i + 1) + "(OFF->ON)";
        }
        ledController.setGroupState(i, true);
    }
    addStatusEntry(changes);
    server.send(200, "text/plain", "OK");
    saveSettings();
}

void handleAllOff()
{
    String changes = "All OFF:";
    for (int i = 0; i < NUM_GROUPS; i++) {
        LedGroup prev = ledController.getGroup(i);
        if (prev.isOn) {
            changes += " " + String(i + 1) + "(ON->OFF)";
        }
        ledController.setGroupState(i, false);
    }
    addStatusEntry(changes);
    server.send(200, "text/plain", "OK");
    saveSettings();
}

void handleReset()
{
    Serial.println("WiFi reset requested");
    preferences.clear();
    // Also clear saved LED settings
    preferences.remove("settings");
    addStatusEntry("WiFi settings reset - restarting");
    server.send(200, "text/plain", "WiFi reset - device restarting");
    delay(1000);
    ESP.restart();
}

void handleSettings()
{
    String html = "<!DOCTYPE html><html><head><title>Settings</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;padding:20px;background:#1a1a1a;color:white} .container{max-width:600px;margin:0 auto} label{display:block;margin-top:10px} ";
    html += "input[type='text'],input[type='password'],input[type='number']{width:100%;padding:8px;margin-top:6px;border-radius:4px;border:1px solid #444;background:#222;color:#fff} ";
    html += "input[type='checkbox']{width:auto;margin-top:0;margin-right:8px;vertical-align:middle} ";
    html += "label.checkbox{display:flex;align-items:center;gap:8px;margin-top:10px} ";
    html += ".btn{margin-top:12px;padding:10px 16px;border-radius:6px;border:none;background:#2196F3;color:white}</style></head><body>";
    html += "<div class='container'><h1>Settings</h1>";
    String broker = preferences.getString("mqtt_broker", "");
    String user = preferences.getString("mqtt_user", "");
    int port = preferences.getInt("mqtt_port", 1883);
    bool tls = preferences.getBool("mqtt_tls", false);
    // Do not expose password in plain text
    bool dbg = preferences.getBool("debug", false);
    html += "<form id='settingsForm'>";
    html += "<label>MQTT Broker (host or IP)</label><input id='broker' name='broker' value='" + broker + "'>";
    html += "<label>MQTT Port</label><input id='port' name='port' type='number' value='" + String(port) + "'>";
    html += "<label class='checkbox'><input type='checkbox' id='tls' name='tls' " + String(tls?"checked":"") + "> Use TLS</label>";
    html += "<label>MQTT Username (optional)</label><input id='user' name='user' value='" + user + "'>";
    html += "<label>MQTT Password (optional)</label><input id='pass' name='pass' type='password' value=''>";
    html += "<label class='checkbox'><input type='checkbox' id='debug' name='debug' " + String(dbg?"checked":"") + "> Enable debug logging (show logs in main UI)</label>";
    html += "<button class='btn' type='button' onclick='saveSettings()'>Save</button>";
    html += "</form>";
    html += "<p><a href='/'>Back to main UI</a></p>";
    html += "</div>";
    html += "<script>function saveSettings(){const b=document.getElementById('broker').value;const u=document.getElementById('user').value;const p=document.getElementById('pass').value;const port=document.getElementById('port').value;const tls=document.getElementById('tls').checked;const debug=document.getElementById('debug').checked; if(!confirm('Save settings and restart device?')) return;fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({broker:b,port:parseInt(port),tls:tls,user:u,pass:p,debug:debug})}).then(r=>{if(r.ok){alert('Saved. Device will restart.');}else{alert('Save failed');}});}</script></body></html>";
    server.send(200, "text/html", html);
}

void handleSaveSettings()
{
    String body = server.arg("plain");
    // Very small JSON parse for expected body {broker:,port:,tls:,user:,pass:}
        auto extractString = [&](const String &key)->String {
    String keyPattern;
    keyPattern.reserve(key.length() + 2);
    keyPattern = "\"";
    keyPattern += key;
    keyPattern += "\"";
    int k = body.indexOf(keyPattern);
        if (k == -1) return String("");
        int colon = body.indexOf(':', k);
        if (colon == -1) return String("");
        int start = body.indexOf('"', colon + 1);
        if (start == -1) return String("");
        int end = body.indexOf('"', start + 1);
        if (end == -1) return String("");
        return body.substring(start + 1, end);
    };
    auto extractInt = [&](const String &key, int def)->int {
    String keyPattern;
    keyPattern.reserve(key.length() + 2);
    keyPattern = "\"";
    keyPattern += key;
    keyPattern += "\"";
    int k = body.indexOf(keyPattern);
        if (k == -1) return def;
        int colon = body.indexOf(':', k);
        if (colon == -1) return def;
        int start = colon + 1;
        int end = body.indexOf(',', start);
        if (end == -1) end = body.indexOf('}', start);
        if (end == -1) end = body.length();
        String val = body.substring(start, end);
        val.trim();
        return val.toInt();
    };
    auto extractBool = [&](const String &key, bool def)->bool {
    String keyPattern;
    keyPattern.reserve(key.length() + 2);
    keyPattern = "\"";
    keyPattern += key;
    keyPattern += "\"";
    int k = body.indexOf(keyPattern);
        if (k == -1) return def;
        int colon = body.indexOf(':', k);
        if (colon == -1) return def;
        int start = colon + 1;
        int end = body.indexOf(',', start);
        if (end == -1) end = body.indexOf('}', start);
        if (end == -1) end = body.length();
        String val = body.substring(start, end);
        val.trim();
        return val == "true";
    };

    String broker = extractString("broker");
    int port = extractInt("port", 1883);
    bool tls = extractBool("tls", false);
    String user = extractString("user");
    String pass = extractString("pass");
    bool dbg = extractBool("debug", false);

    if (broker.length() > 0) preferences.putString("mqtt_broker", broker);
    preferences.putInt("mqtt_port", port);
    preferences.putBool("mqtt_tls", tls);
    preferences.putBool("debug", dbg);
    debugEnabled = dbg;
    if (user.length() > 0) preferences.putString("mqtt_user", user);
    if (pass.length() > 0) preferences.putString("mqtt_pass", pass);
    // trigger restart to pick up new settings
    server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
}

void addStatusEntry(const String &action)
{
    // Prepare a human-friendly timestamp. If system time is available (RTC/NTP), use ISO8601.
    // Otherwise fall back to elapsed time since boot HH:MM:SS.mmm
    auto getTimestampString = [&]() -> String {
        time_t now = time(nullptr);
        if (now > 1600000000) {
            // we have a real epoch time
            struct tm tminfo;
            localtime_r(&now, &tminfo);
            char buf[64];
            // e.g. 2025-10-15T14:03:12
            strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tminfo);
            return String(buf);
        } else {
            unsigned long ms = millis();
            unsigned long s = ms / 1000;
            unsigned long hh = s / 3600;
            unsigned long mm = (s % 3600) / 60;
            unsigned long ss = s % 60;
            unsigned long msec = ms % 1000;
            char buf[64];
            snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu.%03lu", hh, mm, ss, msec);
            return String(buf);
        }
    };

    String ts = getTimestampString();
    // keep a compact status history for quick/status display
    statusHistory[statusIndex].action = action;
    statusHistory[statusIndex].timestamp = millis();
    statusIndex = (statusIndex + 1) % 5;

    // Print to Serial with timestamp
    Serial.println(ts + " " + action);

    // always append to the in-memory debug log with timestamp; visibility of the log panel is controlled by the debug flag
    debugLog[debugLogIndex] = ts + " " + action;
    debugLogIndex = (debugLogIndex + 1) % LOG_BUFFER_SIZE;
}

// Return current settings including mqtt and debug flag and groups
void handleGetSettings()
{
    // Build JSON with broker/port/tls/user and debug
    String broker = preferences.getString("mqtt_broker", "");
    int port = preferences.getInt("mqtt_port", 1883);
    bool tls = preferences.getBool("mqtt_tls", false);
    String user = preferences.getString("mqtt_user", "");

    String out = "{";
    out += "\"broker\":\"" + broker + "\",";
    out += "\"port\":" + String(port) + ",";
    out += "\"tls\":" + String(tls ? "true" : "false") + ",";
    out += "\"user\":\"" + user + "\",";
    out += "\"debug\":" + String(debugEnabled ? "true" : "false");
    out += "}";
    server.send(200, "application/json", out);
}

// Return debug logs (if enabled)
void handleLogs()
{
    // Return the current in-memory logs. The main UI decides whether to display them based on /api/settings.debug
    String out = "[";
    // iterate from oldest to newest
    for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
        int idx = (debugLogIndex + i) % LOG_BUFFER_SIZE;
        if (debugLog[idx].length() == 0) continue;
        if (out.length() > 1) out += ",";
        // escape quotes not necessary since we control contents
        out += "\"" + debugLog[idx] + "\"";
    }
    out += "]";
    server.send(200, "application/json", out);
}