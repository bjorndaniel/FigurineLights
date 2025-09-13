#include "LedController.h"

LedController::LedController()
{
    // Initialize groups with default values
    for (int i = 0; i < NUM_GROUPS; i++)
    {
        groups[i].color = CRGB::White;
        groups[i].brightness = 128; // 50% brightness
        groups[i].isOn = false;
    }
}

void LedController::init()
{
    Serial.println("Initializing FastLED with GRB color order...");
    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(255);
    FastLED.clear();
    FastLED.show();
    Serial.println("FastLED initialization complete");
}

void LedController::updateLeds()
{
    Serial.print("updateLeds() called - Status: ");
    for (int group = 0; group < NUM_GROUPS; group++)
    {
        int ledIndex = group; // Each group has 1 LED, so index = group number

        if (groups[group].isOn)
        {
            CRGB color = groups[group].color;
            // Apply brightness scaling
            color.nscale8(groups[group].brightness);

            Serial.printf("G%d:ON(R%d,G%d,B%d,Br%d) ", group, groups[group].color.r, groups[group].color.g, groups[group].color.b, groups[group].brightness);

            leds[ledIndex] = color;
        }
        else
        {
            Serial.printf("G%d:OFF ", group);
            leds[ledIndex] = CRGB::Black;
        }
    }
    Serial.println();

    FastLED.show();
    Serial.println("FastLED.show() called");
}

void LedController::setGroupColor(int groupIndex, uint8_t r, uint8_t g, uint8_t b)
{
    if (groupIndex >= 0 && groupIndex < NUM_GROUPS)
    {
        groups[groupIndex].color = CRGB(r, g, b);
        updateLeds();
    }
}

void LedController::setGroupBrightness(int groupIndex, uint8_t brightness)
{
    if (groupIndex >= 0 && groupIndex < NUM_GROUPS)
    {
        groups[groupIndex].brightness = brightness;
        updateLeds();
    }
}

void LedController::setGroupState(int groupIndex, bool state)
{
    if (groupIndex >= 0 && groupIndex < NUM_GROUPS)
    {
        Serial.printf("setGroupState: Group %d -> %s\n", groupIndex, state ? "ON" : "OFF");
        groups[groupIndex].isOn = state;
        updateLeds();
    }
    else
    {
        Serial.printf("setGroupState: Invalid group index %d\n", groupIndex);
    }
}

void LedController::setAllOff()
{
    Serial.println("setAllOff: Turning off all groups");
    for (int i = 0; i < NUM_GROUPS; i++)
    {
        groups[i].isOn = false;
    }
    updateLeds();
}

void LedController::setAllOn()
{
    Serial.println("setAllOn: Turning on all groups");
    for (int i = 0; i < NUM_GROUPS; i++)
    {
        groups[i].isOn = true;
    }
    updateLeds();
}

String LedController::getGroupStatus(int groupIndex)
{
    if (groupIndex < 0 || groupIndex >= NUM_GROUPS)
    {
        return "{}";
    }

    String result = "{\"group\":" + String(groupIndex);
    result += ",\"isOn\":" + String(groups[groupIndex].isOn ? "true" : "false");
    result += ",\"brightness\":" + String(groups[groupIndex].brightness);
    result += ",\"color\":{";
    result += "\"r\":" + String(groups[groupIndex].color.r);
    result += ",\"g\":" + String(groups[groupIndex].color.g);
    result += ",\"b\":" + String(groups[groupIndex].color.b);
    result += "}}";
    return result;
}

String LedController::getAllStatus()
{
    String result = "{\"groups\":[";

    for (int i = 0; i < NUM_GROUPS; i++)
    {
        if (i > 0)
            result += ",";
        result += "{\"group\":" + String(i);
        result += ",\"isOn\":" + String(groups[i].isOn ? "true" : "false");
        result += ",\"brightness\":" + String(groups[i].brightness);
        result += ",\"color\":{";
        result += "\"r\":" + String(groups[i].color.r);
        result += ",\"g\":" + String(groups[i].color.g);
        result += ",\"b\":" + String(groups[i].color.b);
        result += "}}";
    }

    result += "]}";
    return result;
}

LedGroup LedController::getGroup(int groupIndex)
{
    if (groupIndex >= 0 && groupIndex < NUM_GROUPS)
    {
        return groups[groupIndex];
    }
    return LedGroup();
}