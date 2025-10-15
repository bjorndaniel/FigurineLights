#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "LedController.h"

class MqttClient {
public:
    MqttClient(LedController &ledController, Preferences &prefs);
    void begin(const char *host, uint16_t port = 1883, bool useTls = false);
    void loop();
    void publishState();
    void publishDiscovery();
private:
    void connect();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    void handleMqttMessage(char* topic, byte* payload, unsigned int length);

    WiFiClient wifiClient;
    WiFiClientSecure wifiClientSecure;
    PubSubClient mqtt;
    static MqttClient *s_instance;
    static LedController *s_ledController;
    static Preferences *s_prefs;
    String _host;
    uint16_t _port;
    bool _useTls = false;
    unsigned long _lastPublish = 0;
    unsigned long _lastConnectAttempt = 0;
    void publishGroupState(int group);
};
