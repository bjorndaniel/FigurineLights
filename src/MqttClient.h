#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "LedController.h"

class MqttClient {
public:
    MqttClient(LedController &ledController, Preferences &prefs);
    void begin(const char *host, uint16_t port = 1883);
    void loop();
    void publishState();
    void publishDiscovery();
private:
    void connect();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);

    WiFiClient wifiClient;
    static PubSubClient mqtt;
    static LedController *s_ledController;
    static Preferences *s_prefs;
    const char *_host;
    uint16_t _port;
    unsigned long _lastPublish = 0;
};
