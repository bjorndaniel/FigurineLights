#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_log.h>
#include "LedController.h"

// Global objects
LedController ledController;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

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

    // Initialize preferences
    preferences.begin("wificonfig", false);
    savedSSID = preferences.getString("ssid", "");
    savedPassword = preferences.getString("password", "");

    // Initialize WiFi
    setupWiFi();

    // Setup web server
    setupWebServer();

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
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>Figurine Lights Controller</h1>";
    html += "<div>";
    html += "<button class='btn btn-success' onclick='allOn()'>All On</button>";
    html += "<button class='btn btn-danger' onclick='allOff()'>All Off</button>";
    html += "<button class='btn btn-warning' onclick='resetWifi()'>Reset WiFi</button>";
    html += "</div>";
    html += "<div class='groups' id='groups'></div>";
    html += "</div>";

    // JavaScript
    html += "<script>";
    html += "let status={};";
    html += "function init(){createGroups();loadStatus();setInterval(loadStatus,3000);}";
    html += "function createGroups(){";
    html += "const container=document.getElementById('groups');";
    html += "for(let i=0;i<4;i++){";
    html += "const div=document.createElement('div');";
    html += "div.className='group';div.id='group'+i;";
    html += "div.innerHTML='<div class=\"group-header\"><h3>Group '+(i+1)+'</h3><button class=\"power-btn off\" onclick=\"toggleGroup('+i+')\" id=\"power'+i+'\"></button></div><div class=\"control-row\"><label>Color:</label><input type=\"color\" class=\"color-input\" id=\"color'+i+'\" onchange=\"updateColor('+i+')\"></div><div class=\"control-row\"><label>Brightness:</label><input type=\"range\" class=\"range-input\" min=\"0\" max=\"255\" id=\"brightness'+i+'\" oninput=\"updateBrightnessDisplay('+i+')\" onchange=\"updateBrightness('+i+')\"><span class=\"brightness-val\" id=\"brightVal'+i+'\">50%</span></div><div class=\"status-text\" id=\"status'+i+'\">OFF</div>';";
    html += "container.appendChild(div);}}";
    html += "function loadStatus(){fetch('/api/status').then(r=>r.json()).then(data=>{status=data;updateUI(data);});}";
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
    html += "}";
    html += "function toggleGroup(i){const isOn=status.groups[i].isOn;sendCommand({group:i,isOn:!isOn});}";
    html += "function updateColor(i){const hex=document.getElementById('color'+i).value;const r=parseInt(hex.slice(1,3),16);const g=parseInt(hex.slice(3,5),16);const b=parseInt(hex.slice(5,7),16);sendCommand({group:i,color:{r:r,g:g,b:b}});}";
    html += "function updateBrightnessDisplay(i){const val=parseInt(document.getElementById('brightness'+i).value);const percent=Math.round(val/255*100);document.getElementById('brightVal'+i).textContent=percent+'%';}";
    html += "function updateBrightness(i){const val=parseInt(document.getElementById('brightness'+i).value);const percent=Math.round(val/255*100);document.getElementById('brightVal'+i).textContent=percent+'%';sendCommand({group:i,brightness:val});}";
    html += "function allOn(){fetch('/api/all/on',{method:'POST'}).then(()=>setTimeout(loadStatus,100));}";
    html += "function allOff(){fetch('/api/all/off',{method:'POST'}).then(()=>setTimeout(loadStatus,100));}";
    html += "function resetWifi(){if(confirm('Reset WiFi settings? Device will restart.')){fetch('/api/reset',{method:'POST'}).then(()=>{alert('WiFi reset! Device restarting...');});}}";
    html += "function sendCommand(data){fetch('/api/group',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}).then(()=>setTimeout(loadStatus,500));}";
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

    int group = -1;
    bool isOn = false;
    int brightness = -1;
    int r = -1, g = -1, b = -1;

    // Simple JSON parsing
    int groupPos = body.indexOf("\"group\":");
    if (groupPos != -1)
    {
        int colonPos = body.indexOf(':', groupPos);
        int commaPos = body.indexOf(',', colonPos);
        int bracePos = body.indexOf('}', colonPos);
        int endPos = (commaPos != -1 && commaPos < bracePos) ? commaPos : bracePos;
        group = body.substring(colonPos + 1, endPos).toInt();
    }

    int isOnPos = body.indexOf("\"isOn\":");
    if (isOnPos != -1)
    {
        int colonPos = body.indexOf(':', isOnPos);
        int commaPos = body.indexOf(',', colonPos);
        int bracePos = body.indexOf('}', colonPos);
        int endPos = (commaPos != -1 && commaPos < bracePos) ? commaPos : bracePos;
        String isOnStr = body.substring(colonPos + 1, endPos);
        isOnStr.trim();
        isOn = (isOnStr == "true");
        ledController.setGroupState(group, isOn);
        addStatusEntry("Group " + String(group + 1) + (isOn ? " turned ON" : " turned OFF"));
    }

    int brightPos = body.indexOf("\"brightness\":");
    if (brightPos != -1)
    {
        int colonPos = body.indexOf(':', brightPos);
        int commaPos = body.indexOf(',', colonPos);
        int bracePos = body.indexOf('}', colonPos);
        int endPos = (commaPos != -1 && commaPos < bracePos) ? commaPos : bracePos;
        brightness = body.substring(colonPos + 1, endPos).toInt();
        ledController.setGroupBrightness(group, brightness);
        addStatusEntry("Group " + String(group + 1) + " brightness: " + String((brightness * 100) / 255) + "%");
    }

    int colorPos = body.indexOf("\"color\":{");
    if (colorPos != -1)
    {
        int rPos = body.indexOf("\"r\":", colorPos);
        int gPos = body.indexOf("\"g\":", colorPos);
        int bPos = body.indexOf("\"b\":", colorPos);

        if (rPos != -1)
            r = body.substring(rPos + 4, body.indexOf(',', rPos)).toInt();
        if (gPos != -1)
            g = body.substring(gPos + 4, body.indexOf(',', gPos)).toInt();
        if (bPos != -1)
            b = body.substring(bPos + 4, body.indexOf('}', bPos)).toInt();

        if (r >= 0 && g >= 0 && b >= 0)
        {
            ledController.setGroupColor(group, r, g, b);
            addStatusEntry("Group " + String(group + 1) + " color changed");
        }
    }

    server.send(200, "application/json", ledController.getGroupStatus(group));
}

void handleAllOn()
{
    ledController.setAllOn();
    addStatusEntry("All groups turned ON");
    server.send(200, "text/plain", "OK");
}

void handleAllOff()
{
    ledController.setAllOff();
    addStatusEntry("All groups turned OFF");
    server.send(200, "text/plain", "OK");
}

void handleReset()
{
    Serial.println("WiFi reset requested");
    preferences.clear();
    addStatusEntry("WiFi settings reset - restarting");
    server.send(200, "text/plain", "WiFi reset - device restarting");
    delay(1000);
    ESP.restart();
}

void addStatusEntry(const String &action)
{
    statusHistory[statusIndex].action = action;
    statusHistory[statusIndex].timestamp = millis();
    statusIndex = (statusIndex + 1) % 5;
    Serial.println("Status: " + action);
}