#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <FastLED.h>
#include <ArduinoJson.h>

#define LED_PIN 18
#define NUM_LEDS 4
#define NUM_GROUPS 4
#define LEDS_PER_GROUP 1

struct LedGroup
{
    CRGB color;
    uint8_t brightness;
    bool isOn;
};

class LedController
{
public:
    CRGB leds[NUM_LEDS]; // Make public for direct testing

private:
    LedGroup groups[NUM_GROUPS];

public:
    LedController();
    void init();
    void updateLeds();
    void setGroupColor(int groupIndex, uint8_t r, uint8_t g, uint8_t b);
    void setGroupBrightness(int groupIndex, uint8_t brightness);
    void setGroupState(int groupIndex, bool state);
    void setAllOff();
    void setAllOn();
    String getGroupStatus(int groupIndex);
    String getAllStatus();
    LedGroup getGroup(int groupIndex);
};

#endif