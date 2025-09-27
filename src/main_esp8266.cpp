#include <Arduino.h>

#ifdef ESP8266_BUILD
    // ESP8266 specific includes
    #include <ESP8266WiFi.h>
    #include <ESP8266WebServer.h>
    #include <DNSServer.h>
    #include <EEPROM.h>
    
    // Type aliases for compatibility
    #define WebServer ESP8266WebServer
    #define LED_PIN 2  // GPIO2 on ESP8266 (NodeMCU D4)
#else
    // ESP32 specific includes
    #include <WiFi.h>
    #include <WebServer.h>
    #include <DNSServer.h>
    #include <Preferences.h>
    #include <esp_log.h>
    
    #define LED_PIN 18  // GPIO18 on ESP32
#endif

#include "LedController.h"

// Global objects
LedController ledController;
WebServer server(80);
DNSServer dnsServer;

#ifndef ESP8266_BUILD
Preferences preferences;
#endif

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

// ESP8266 EEPROM Settings Storage (addresses)
#define EEPROM_SIZE 512
#define SSID_ADDRESS 0
#define PASSWORD_ADDRESS 100
#define SSID_MAX_LENGTH 32
#define PASSWORD_MAX_LENGTH 64

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
void handleInfo();
void handleReset();
void addStatusEntry(const String &action);

#ifdef ESP8266_BUILD
// ESP8266 specific storage functions
void saveCredentials(const String &ssid, const String &password) {
    EEPROM.begin(EEPROM_SIZE);
    
    // Clear and write SSID
    for (int i = 0; i < SSID_MAX_LENGTH; i++) {
        EEPROM.write(SSID_ADDRESS + i, 0);
    }
    for (int i = 0; i < ssid.length() && i < SSID_MAX_LENGTH - 1; i++) {
        EEPROM.write(SSID_ADDRESS + i, ssid[i]);
    }
    
    // Clear and write Password
    for (int i = 0; i < PASSWORD_MAX_LENGTH; i++) {
        EEPROM.write(PASSWORD_ADDRESS + i, 0);
    }
    for (int i = 0; i < password.length() && i < PASSWORD_MAX_LENGTH - 1; i++) {
        EEPROM.write(PASSWORD_ADDRESS + i, password[i]);
    }
    
    EEPROM.commit();
}

String loadSSID() {
    EEPROM.begin(EEPROM_SIZE);
    String ssid = "";
    for (int i = 0; i < SSID_MAX_LENGTH; i++) {
        char c = EEPROM.read(SSID_ADDRESS + i);
        if (c == 0) break;
        ssid += c;
    }
    return ssid;
}

String loadPassword() {
    EEPROM.begin(EEPROM_SIZE);
    String password = "";
    for (int i = 0; i < PASSWORD_MAX_LENGTH; i++) {
        char c = EEPROM.read(PASSWORD_ADDRESS + i);
        if (c == 0) break;
        password += c;
    }
    return password;
}

void clearCredentials() {
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < SSID_MAX_LENGTH + PASSWORD_MAX_LENGTH; i++) {
        EEPROM.write(SSID_ADDRESS + i, 0);
    }
    EEPROM.commit();
}
#endif

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("Starting Figurine Lights Controller...");
    
#ifdef ESP8266_BUILD
    Serial.println("Running on ESP8266");
    EEPROM.begin(EEPROM_SIZE);
#else
    Serial.println("Running on ESP32");
    
    // Reduce log verbosity (ESP32 only)
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("WiFiUdp", ESP_LOG_NONE);
#endif

    // Initialize LED controller
    ledController.init();

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

    // Load saved credentials
#ifdef ESP8266_BUILD
    savedSSID = loadSSID();
    savedPassword = loadPassword();
#else
    preferences.begin("wificonfig", false);
    savedSSID = preferences.getString("ssid", "");
    savedPassword = preferences.getString("password", "");
#endif

    // Initialize WiFi
    setupWiFi();

    // Setup web server
    setupWebServer();

    Serial.println("Setup complete!");
}

void loop()
{
    server.handleClient();
    if (isAccessPoint)
    {
        dnsServer.processNextRequest();
    }
}

void setupWiFi()
{
    if (savedSSID.length() > 0)
    {
        Serial.println("Attempting to connect to saved WiFi...");
        if (connectToWiFi(savedSSID, savedPassword))
        {
            Serial.println("Connected to saved WiFi network");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
            addStatusEntry("Connected to " + savedSSID);
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
    server.on("/api/connect", HTTP_POST, handleConnect);
    server.on("/api/status", handleStatus);
    server.on("/api/group", HTTP_POST, handleGroup);
    server.on("/api/all/on", HTTP_POST, handleAllOn);
    server.on("/api/all/off", HTTP_POST, handleAllOff);
    server.on("/api/info", handleInfo);
    server.on("/api/reset", HTTP_POST, handleReset);

    server.begin();
    Serial.println("HTTP server started");
}

void handleRoot()
{
    if (isAccessPoint)
    {
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
        return;
    }

    String html = "<!DOCTYPE html><html><head><title>Figurine Lights</title>";
    html += "<style>";
    html += "body{font-family:Arial;margin:40px;background:#f0f0f0;}";
    html += ".container{max-width:800px;margin:auto;background:white;padding:30px;border-radius:10px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}";
    html += "h1{color:#333;text-align:center;margin-bottom:30px;}";
    html += ".btn{padding:12px 20px;margin:5px;border:none;border-radius:5px;cursor:pointer;font-size:16px;transition:all 0.3s;}";
    html += ".btn-success{background:#28a745;color:white;}.btn-success:hover{background:#218838;}";
    html += ".btn-danger{background:#dc3545;color:white;}.btn-danger:hover{background:#c82333;}";
    html += ".btn-warning{background:#ffc107;color:#212529;}.btn-warning:hover{background:#e0a800;}";
    html += ".groups{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px;margin-top:30px;}";
    html += ".group{border:2px solid #ddd;border-radius:10px;padding:20px;background:#f8f9fa;}";
    html += ".group h3{margin:0 0 15px 0;color:#495057;}";
    html += ".controls{display:flex;flex-direction:column;gap:10px;}";
    html += ".color-row,.brightness-row{display:flex;align-items:center;gap:10px;}";
    html += "input[type=color]{width:50px;height:40px;border:none;border-radius:5px;cursor:pointer;}";
    html += "input[type=range]{flex:1;height:25px;}";
    html += ".toggle{width:60px;height:30px;background:#ccc;border-radius:15px;position:relative;cursor:pointer;transition:0.3s;}";
    html += ".toggle.on{background:#28a745;}";
    html += ".toggle .slider{width:26px;height:26px;background:white;border-radius:50%;position:absolute;top:2px;left:2px;transition:0.3s;}";
    html += ".toggle.on .slider{transform:translateX(30px);}";
    html += ".status{margin-top:10px;font-size:14px;color:#666;}";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>Figurine Lights Controller</h1>";
    html += "<div>";
    html += "<button class='btn btn-success' onclick='allOn()'>All On</button>";
    html += "<button class='btn btn-danger' onclick='allOff()'>All Off</button>";
    html += "<button class='btn btn-warning' onclick='resetWifi()' style='margin-left: 20px; font-weight: bold;'>⚠️ Reset WiFi</button>";
    html += "</div>";
    html += "<div class='groups' id='groups'></div>";
    html += "</div>";

    // JavaScript
    html += "<script>";
    html += "let groups=[];";
    html += "function init(){loadStatus();setInterval(loadStatus,2000);}";
    html += "function loadStatus(){fetch('/api/status').then(r=>r.json()).then(updateGroups);}";
    html += "function updateGroups(data){groups=data.groups;let html='';";
    html += "for(let i=0;i<groups.length;i++){let g=groups[i];";
    html += "html+=`<div class='group'><h3>Group ${i+1}</h3><div class='controls'>`;";
    html += "html+=`<div class='color-row'><input type='color' value='${rgbToHex(g.r,g.g,g.b)}' onchange='updateColor(${i},this.value)'><label>Color</label></div>`;";
    html += "html+=`<div class='brightness-row'><input type='range' min='1' max='100' value='${g.brightness}' oninput='updateBrightness(${i},this.value)'><label>Brightness: ${g.brightness}%</label></div>`;";
    html += "html+=`<div class='toggle ${g.on?'on':''}' onclick='toggleGroup(${i})'><div class='slider'></div></div>`;";
    html += "html+=`<div class='status'>${g.on?'ON':'OFF'}</div>`;";
    html += "html+='</div></div>';}";
    html += "document.getElementById('groups').innerHTML=html;}";
    html += "function rgbToHex(r,g,b){return '#'+[r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('');}";
    html += "function hexToRgb(hex){let r=parseInt(hex.slice(1,3),16),g=parseInt(hex.slice(3,5),16),b=parseInt(hex.slice(5,7),16);return{r,g,b};}";
    html += "function updateColor(group,hex){let rgb=hexToRgb(hex);sendCommand({group,r:rgb.r,g:rgb.g,b:rgb.b});}";
    html += "function updateBrightness(group,brightness){sendCommand({group,brightness:parseInt(brightness)});}";
    html += "function toggleGroup(group){sendCommand({group,on:!groups[group].on});}";
    html += "function allOn(){fetch('/api/all/on',{method:'POST'}).then(()=>setTimeout(loadStatus,100));}";
    html += "function allOff(){fetch('/api/all/off',{method:'POST'}).then(()=>setTimeout(loadStatus,100));}";
    html += "function resetWifi(){";
    html += "if(confirm('⚠️ WARNING: Reset WiFi Settings?\\n\\nThis will:\\n• Clear saved WiFi credentials\\n• Restart the device\\n• Return to setup mode\\n\\nAre you sure?')){";
    html += "if(confirm('FINAL CONFIRMATION:\\n\\nThis action cannot be undone!\\n\\nClick OK to proceed with WiFi reset.')){";
    html += "fetch('/api/reset',{method:'POST'}).then(()=>{alert('WiFi reset initiated! Device restarting in 3 seconds...');});";
    html += "}else{alert('WiFi reset cancelled.');}";
    html += "}else{alert('WiFi reset cancelled.');}";
    html += "}";
    html += "function sendCommand(data){fetch('/api/group',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}).then(()=>setTimeout(loadStatus,500));}";
    html += "document.addEventListener('DOMContentLoaded',init);";
    html += "</script></body></html>";

    server.send(200, "text/html", html);
}

void handleSetup()
{
    String html = "<!DOCTYPE html><html><head><title>WiFi Setup</title>";
    html += "<style>body{font-family:Arial;margin:40px;background:#f0f0f0;}";
    html += ".container{max-width:600px;margin:auto;background:white;padding:30px;border-radius:10px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}";
    html += "h1{color:#333;text-align:center;}input,select{width:100%;padding:12px;margin:10px 0;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;}";
    html += "button{width:100%;padding:15px;background:#007bff;color:white;border:none;border-radius:5px;cursor:pointer;font-size:16px;}";
    html += "button:hover{background:#0056b3;}</style></head><body>";
    html += "<div class='container'><h1>WiFi Configuration</h1>";
    html += "<form onsubmit='connect(event)'>";
    html += "<input type='text' id='ssid' placeholder='WiFi Network Name (SSID)' required>";
    html += "<input type='password' id='password' placeholder='WiFi Password'>";
    html += "<button type='submit'>Connect</button></form>";
    html += "<script>function connect(e){e.preventDefault();";
    html += "let ssid=document.getElementById('ssid').value;";
    html += "let password=document.getElementById('password').value;";
    html += "fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/json'},";
    html += "body:JSON.stringify({ssid,password})}).then(r=>r.text()).then(msg=>{alert(msg);if(msg.includes('Success'))setTimeout(()=>window.location.reload(),3000);});}</script>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);
}

void handleConnect()
{
    String body = server.arg("plain");
    
    // Parse JSON manually for ESP8266 compatibility
    int ssidStart = body.indexOf("\"ssid\":\"") + 8;
    int ssidEnd = body.indexOf("\"", ssidStart);
    int passwordStart = body.indexOf("\"password\":\"") + 12;
    int passwordEnd = body.indexOf("\"", passwordStart);
    
    String ssid = body.substring(ssidStart, ssidEnd);
    String password = body.substring(passwordStart, passwordEnd);

    if (connectToWiFi(ssid, password))
    {
#ifdef ESP8266_BUILD
        saveCredentials(ssid, password);
#else
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
#endif
        savedSSID = ssid;
        savedPassword = password;
        
        server.send(200, "text/plain", "Success! Connected to " + ssid);
        addStatusEntry("Connected to " + ssid);
        
        delay(1000);
        isAccessPoint = false;
        WiFi.mode(WIFI_STA);
    }
    else
    {
        server.send(400, "text/plain", "Failed to connect to " + ssid);
        addStatusEntry("Failed: " + ssid);
    }
}

void handleStatus()
{
    String json = "{\"groups\":[";
    for (int i = 0; i < 4; i++)
    {
        if (i > 0) json += ",";
        LedGroup group = ledController.getGroup(i);
        json += "{\"on\":" + String(group.isOn ? "true" : "false");
        json += ",\"r\":" + String(group.color.r);
        json += ",\"g\":" + String(group.color.g);
        json += ",\"b\":" + String(group.color.b);
        json += ",\"brightness\":" + String(group.brightness) + "}";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleGroup()
{
    String body = server.arg("plain");
    
    // Simple JSON parsing for group commands
    int groupStart = body.indexOf("\"group\":") + 8;
    int groupEnd = body.indexOf(",", groupStart);
    if (groupEnd == -1) groupEnd = body.indexOf("}", groupStart);
    
    int groupId = body.substring(groupStart, groupEnd).toInt();
    
    if (groupId < 0 || groupId >= 4) {
        server.send(400, "text/plain", "Invalid group");
        return;
    }

    // Check for different parameters
    if (body.indexOf("\"on\":") != -1) {
        bool state = body.indexOf("\"on\":true") != -1;
        ledController.setGroupState(groupId, state);
    }
    
    if (body.indexOf("\"r\":") != -1) {
        int rStart = body.indexOf("\"r\":") + 4;
        int rEnd = body.indexOf(",", rStart);
        if (rEnd == -1) rEnd = body.indexOf("}", rStart);
        int r = body.substring(rStart, rEnd).toInt();
        
        int gStart = body.indexOf("\"g\":") + 4;
        int gEnd = body.indexOf(",", gStart);
        if (gEnd == -1) gEnd = body.indexOf("}", gStart);
        int g = body.substring(gStart, gEnd).toInt();
        
        int bStart = body.indexOf("\"b\":") + 4;
        int bEnd = body.indexOf(",", bStart);
        if (bEnd == -1) bEnd = body.indexOf("}", bStart);
        int b = body.substring(bStart, bEnd).toInt();
        
        ledController.setGroupColor(groupId, r, g, b);
    }
    
    if (body.indexOf("\"brightness\":") != -1) {
        int brightStart = body.indexOf("\"brightness\":") + 13;
        int brightEnd = body.indexOf(",", brightStart);
        if (brightEnd == -1) brightEnd = body.indexOf("}", brightStart);
        int brightness = body.substring(brightStart, brightEnd).toInt();
        ledController.setGroupBrightness(groupId, brightness);
    }

    server.send(200, "text/plain", "OK");
    addStatusEntry("Group " + String(groupId + 1) + " updated");
}

void handleAllOn()
{
    ledController.setAllOn();
    server.send(200, "text/plain", "All groups turned on");
    addStatusEntry("All groups ON");
}

void handleAllOff()
{
    ledController.setAllOff();
    server.send(200, "text/plain", "All groups turned off");
    addStatusEntry("All groups OFF");
}

void handleInfo()
{
    String info = "Figurine Lights Controller\\n";
#ifdef ESP8266_BUILD
    info += "Platform: ESP8266\\n";
#else
    info += "Platform: ESP32\\n";
#endif
    info += "IP: " + WiFi.localIP().toString() + "\\n";
    info += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\\n";
    info += "Uptime: " + String(millis() / 1000) + " seconds";
    
    server.send(200, "text/plain", info);
}

void handleReset()
{
#ifdef ESP8266_BUILD
    clearCredentials();
#else
    preferences.clear();
#endif
    
    server.send(200, "text/plain", "WiFi settings cleared. Restarting...");
    addStatusEntry("WiFi Reset");
    delay(1000);
    ESP.restart();
}

void addStatusEntry(const String &action)
{
    statusHistory[statusIndex] = {action, millis()};
    statusIndex = (statusIndex + 1) % 5;
}