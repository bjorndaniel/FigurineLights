#include "MqttClient.h"
#include "StatusLog.h"

// static members
MqttClient* MqttClient::s_instance = nullptr;
LedController* MqttClient::s_ledController = nullptr;
Preferences* MqttClient::s_prefs = nullptr;

MqttClient::MqttClient(LedController &ledController, Preferences &prefs)
{
    s_ledController = &ledController;
    s_prefs = &prefs;
    s_instance = this;
    // mqtt is an object member
    _host = String();
    _port = 1883;
    _useTls = false;
    _lastPublish = 0;
    _lastConnectAttempt = 0;
}

void MqttClient::begin(const char *host, uint16_t port, bool useTls)
{
    _host = String(host);
    _port = port;
    _useTls = useTls;
    // allocate PubSubClient and attach client
    // attach client to the member PubSubClient
    if (_useTls)
    {
        wifiClientSecure.setInsecure();
        mqtt.setClient(wifiClientSecure);
    }
    else
    {
        mqtt.setClient(wifiClient);
    }

    mqtt.setServer(_host.c_str(), _port);
    mqtt.setCallback(MqttClient::mqttCallback);
}

void MqttClient::connect()
{
    if (mqtt.connected()) return;
    Serial.printf("Connecting to MQTT %s:%d\n", _host.c_str(), _port);
    String user = s_prefs->getString("mqtt_user", "");
    String pass = s_prefs->getString("mqtt_pass", "");
    bool ok = false;
    if (user.length() > 0)
    {
    ok = mqtt.connect("figurine_client", user.c_str(), pass.c_str());
    }
    else
    {
    ok = mqtt.connect("figurine_client");
    }

    if (!ok)
    {
        Serial.println("MQTT connect failed");
        return;
    }

    Serial.println("MQTT connected");
    for (int i = 0; i < NUM_GROUPS; i++)
    {
        String t = String("figurine/group/") + String(i) + String("/set");
        mqtt.subscribe(t.c_str());
    }
    mqtt.subscribe("figurine/all/set");

    publishDiscovery();
    publishState();
}

void MqttClient::loop()
{
    unsigned long now = millis();
    if (!mqtt.connected())
    {
        // attempt to connect at most every 10 seconds
        if (now - _lastConnectAttempt > 10000)
        {
            _lastConnectAttempt = now;
            connect();
        }
        // return early so we don't block the webserver
        return;
    }

    mqtt.loop();
    now = millis();
    if (now - _lastPublish > 30000)
    {
        publishState();
        _lastPublish = now;
    }
}

void MqttClient::publishGroupState(int group)
{
    if (!mqtt.connected()) return;
    LedGroup g = s_ledController->getGroup(group);
    // Build small JSON payload manually: {"state":"ON","brightness":NN,"color":[r,g,b]}
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "{\"state\":\"%s\",\"brightness\":%d,\"color\":[%d,%d,%d]}",
                       g.isOn ? "ON" : "OFF",
                       g.brightness,
                       g.color.r, g.color.g, g.color.b);
    String topic = String("figurine/group/") + String(group) + String("/state");
    mqtt.publish(topic.c_str(), buf, true);
}

void MqttClient::publishState()
{
    for (int i = 0; i < NUM_GROUPS; i++)
    {
        publishGroupState(i);
    }
}

void MqttClient::publishDiscovery()
{
    if (!mqtt.connected()) return;
    for (int i = 0; i < NUM_GROUPS; i++)
    {
        String deviceName = String("Figurine Group ") + String(i + 1);
        String unique = String("figurine_group_") + String(i + 1);
        // Build HA discovery JSON manually
        String payload = "{";
        payload += "\"name\":\"" + deviceName + "\",";
        payload += "\"unique_id\":\"" + unique + "\",";
        payload += "\"schema\":\"json\",";
        payload += "\"command_topic\":\"" + String("figurine/group/") + String(i) + String("/set") + "\",";
        payload += "\"state_topic\":\"" + String("figurine/group/") + String(i) + String("/state") + "\",";
        payload += "\"brightness\":true,";
        payload += "\"rgb\":true";
        payload += "}";

        String topic = String("homeassistant/light/") + unique + String("/config");
        mqtt.publish(topic.c_str(), payload.c_str(), true);
    }
}

void MqttClient::mqttCallback(char* topic, byte* payload, unsigned int length)
{
    if (s_instance)
    {
        s_instance->handleMqttMessage(topic, payload, length);
    }
}

void MqttClient::handleMqttMessage(char* topic, byte* payload, unsigned int length)
{
    String t(topic);
    String body;
    for (unsigned int i = 0; i < length; i++) body += (char)payload[i];
    Serial.printf("MQTT msg on %s: %s\n", topic, body.c_str());

    if (t.startsWith("figurine/group/"))
    {
        int idxStart = String("figurine/group/").length();
        int slash = t.indexOf('/', idxStart);
        if (slash == -1) return;
        String grpStr = t.substring(idxStart, slash);
        int g = grpStr.toInt();

        // Simple parser: look for "state":"ON" or brightness and color array
        if (body.indexOf("\"state\":\"ON\"") != -1) {
            LedGroup prev = s_ledController->getGroup(g);
            if (!prev.isOn) {
                s_ledController->setGroupState(g, true);
                addStatusEntry(String("MQTT: Group ") + String(g+1) + String(" state: OFF -> ON"));
            }
        }
        else if (body.indexOf("\"state\":\"OFF\"") != -1) {
            LedGroup prev = s_ledController->getGroup(g);
            if (prev.isOn) {
                s_ledController->setGroupState(g, false);
                addStatusEntry(String("MQTT: Group ") + String(g+1) + String(" state: ON -> OFF"));
            }
        }

        int bpos = body.indexOf("\"brightness\":");
        if (bpos != -1) {
            int start = bpos + 13;
            int end = body.indexOf(',', start);
            if (end == -1) end = body.indexOf('}', start);
            int val = body.substring(start, end).toInt();
            LedGroup prev = s_ledController->getGroup(g);
            if (prev.brightness != (uint8_t)val) {
                int prevPct = (prev.brightness * 100 + 127) / 255;
                int newPct = ((uint8_t)val * 100 + 127) / 255;
                s_ledController->setGroupBrightness(g, (uint8_t)val);
                addStatusEntry(String("MQTT: Group ") + String(g+1) + String(" brightness: ") + String(prevPct) + "% -> " + String(newPct) + "%");
            }
        }

        int cpos = body.indexOf("\"color\":[");
        if (cpos != -1) {
            int start = cpos + 9;
            int end = body.indexOf(']', start);
            if (end != -1) {
                String arr = body.substring(start, end);
                int comma1 = arr.indexOf(',');
                int comma2 = arr.indexOf(',', comma1 + 1);
                if (comma1 != -1 && comma2 != -1) {
                    int r = arr.substring(0, comma1).toInt();
                    int gcol = arr.substring(comma1 + 1, comma2).toInt();
                    int bcol = arr.substring(comma2 + 1).toInt();
                    LedGroup prevc = s_ledController->getGroup(g);
                    if (prevc.color.r != r || prevc.color.g != gcol || prevc.color.b != bcol) {
                        char oldc[32], newc[32];
                        snprintf(oldc, sizeof(oldc), "rgb(%d,%d,%d)", prevc.color.r, prevc.color.g, prevc.color.b);
                        snprintf(newc, sizeof(newc), "rgb(%d,%d,%d)", r, gcol, bcol);
                        s_ledController->setGroupColor(g, r, gcol, bcol);
                        addStatusEntry(String("MQTT: Group ") + String(g+1) + String(" color: ") + String(oldc) + " -> " + String(newc));
                    }
                }
            }
        }

    // persist settings after applying MQTT command
    s_prefs->putString("settings", s_ledController->getAllStatus());
        publishGroupState(g);
        return;
    }

    if (t == "figurine/all/set")
    {
        if (body.indexOf("\"state\":\"ON\"") != -1) s_ledController->setAllOn();
        else if (body.indexOf("\"state\":\"OFF\"") != -1) s_ledController->setAllOff();

        int bpos = body.indexOf("\"brightness\":");
        if (bpos != -1) {
            int start = bpos + 13;
            int end = body.indexOf(',', start);
            if (end == -1) end = body.indexOf('}', start);
            int val = body.substring(start, end).toInt();
            for (int i = 0; i < NUM_GROUPS; i++) s_ledController->setGroupBrightness(i, (uint8_t)val);
        }
        // Persist new settings so MQTT-driven changes survive reboot
        if (s_prefs) {
            s_prefs->putString("settings", s_ledController->getAllStatus());
        }
        publishState();
    }
}
