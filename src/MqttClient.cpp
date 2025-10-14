#include "MqttClient.h"
#include <ArduinoJson.h>

// static members
PubSubClient MqttClient::mqtt = PubSubClient((Client*)nullptr);
LedController* MqttClient::s_ledController = nullptr;
Preferences* MqttClient::s_prefs = nullptr;

MqttClient::MqttClient(LedController &ledController, Preferences &prefs)
{
    s_ledController = &ledController;
    s_prefs = &prefs;
}

void MqttClient::begin(const char *host, uint16_t port)
{
    _host = host;
    _port = port;
    mqtt.setClient(wifiClient);
    mqtt.setServer(_host, _port);
    mqtt.setCallback(MqttClient::mqttCallback);
}

static void publishGroupState(int group)
{
    if (!MqttClient::mqtt.connected()) return;
    LedGroup g = MqttClient::s_ledController->getGroup(group);
    StaticJsonDocument<256> doc;
    doc["state"] = g.isOn ? "ON" : "OFF";
    doc["brightness"] = g.brightness;
    JsonArray arr = doc.createNestedArray("color");
    arr.add(g.color.r);
    arr.add(g.color.g);
    arr.add(g.color.b);
    char buf[256];
    size_t n = serializeJson(doc, buf);
    String topic = String("figurine/group/") + String(group) + String("/state");
    MqttClient::mqtt.publish(topic.c_str(), buf, true);
}

void MqttClient::publishState()
{
    // Publish all group states periodically
    for (int i = 0; i < NUM_GROUPS; i++)
    {
        publishGroupState(i);
    }
}

void MqttClient::publishDiscovery()
{
    if (!MqttClient::mqtt.connected()) return;
    // Publish HA discovery for each group
    for (int i = 0; i < NUM_GROUPS; i++)
    {
        StaticJsonDocument<512> doc;
        String deviceName = String("Figurine Group ") + String(i + 1);
        String unique = String("figurine_group_") + String(i + 1);
        doc["name"] = deviceName;
        doc["unique_id"] = unique;
        doc["schema"] = "json";
        doc["command_topic"] = String("figurine/group/") + String(i) + String("/set");
        doc["state_topic"] = String("figurine/group/") + String(i) + String("/state");
        doc["brightness"] = true;
        doc["rgb"] = true;
        char buf[512];
        size_t n = serializeJson(doc, buf);
        String topic = String("homeassistant/light/") + unique + String("/config");
        MqttClient::mqtt.publish(topic.c_str(), buf, true);
    }
}

void MqttClient::connect()
{
    if (mqtt.connected()) return;
    Serial.printf("Connecting to MQTT %s:%d\n", _host, _port);
    if (mqtt.connect("figurine_client"))
    {
        Serial.println("MQTT connected");
        // subscribe to commands
        for (int i = 0; i < NUM_GROUPS; i++)
        {
            String t = String("figurine/group/") + String(i) + String("/set");
            mqtt.subscribe(t.c_str());
        }
        mqtt.subscribe("figurine/all/set");
        // publish discovery and initial state
        publishDiscovery();
        publishState();
    }
    else
    {
        Serial.println("MQTT connect failed");
    }
}

void MqttClient::loop()
{
    if (!mqtt.connected())
    {
        connect();
    }
    else
    {
        mqtt.loop();
        unsigned long now = millis();
        if (now - _lastPublish > 30000)
        {
            publishState();
            _lastPublish = now;
        }
    }
}

void MqttClient::mqttCallback(char* topic, byte* payload, unsigned int length)
{
    String t(topic);
    String body;
    for (unsigned int i = 0; i < length; i++) body += (char)payload[i];
    Serial.printf("MQTT msg on %s: %s\n", topic, body.c_str());

    // group command
    if (t.startsWith("figurine/group/"))
    {
        int idxStart = String("figurine/group/").length();
        int slash = t.indexOf('/', idxStart);
        if (slash == -1) return;
        String grpStr = t.substring(idxStart, slash);
        int g = grpStr.toInt();
        // expect JSON {"state":"ON","brightness":128,"color":[r,g,b]}
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) return;
        const char *state = doc["state"] | "";
        if (strcmp(state, "ON") == 0) s_ledController->setGroupState(g, true);
        else if (strcmp(state, "OFF") == 0) s_ledController->setGroupState(g, false);
        if (doc.containsKey("brightness"))
        {
            int b = doc["brightness"].as<int>();
            s_ledController->setGroupBrightness(g, (uint8_t)b);
        }
        if (doc.containsKey("color"))
        {
            JsonArray arr = doc["color"].as<JsonArray>();
            if (arr.size() >= 3)
            {
                int r = arr[0];
                int gcol = arr[1];
                int bcol = arr[2];
                s_ledController->setGroupColor(g, r, gcol, bcol);
            }
        }
        // save
        s_prefs->putString("settings", ""); // cheap way to mark dirty; consumer can call saveSettings later
        // publish updated state
        publishGroupState(g);
        return;
    }

    if (t == "figurine/all/set")
    {
        StaticJsonDocument<128> doc;
        DeserializationError err = deserializeJson(doc, body);
        if (!err)
        {
            if (doc.containsKey("state"))
            {
                const char *state = doc["state"];
                if (strcmp(state, "ON") == 0) s_ledController->setAllOn();
                else s_ledController->setAllOff();
            }
            if (doc.containsKey("brightness"))
            {
                int b = doc["brightness"].as<int>();
                for (int i = 0; i < NUM_GROUPS; i++) s_ledController->setGroupBrightness(i, (uint8_t)b);
            }
            publishState();
        }
    }
}
