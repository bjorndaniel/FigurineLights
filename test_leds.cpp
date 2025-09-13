#include <Arduino.h>
#include <FastLED.h>

#define LED_PIN 18
#define NUM_LEDS 8

CRGB leds[NUM_LEDS];

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("Simple LED Test");

    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(255);

    // Test 1: All red at full brightness
    Serial.println("Setting all LEDs to RED");
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();
    delay(2000);

    // Test 2: All green
    Serial.println("Setting all LEDs to GREEN");
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    delay(2000);

    // Test 3: All blue
    Serial.println("Setting all LEDs to BLUE");
    fill_solid(leds, NUM_LEDS, CRGB::Blue);
    FastLED.show();
    delay(2000);

    // Test 4: All white
    Serial.println("Setting all LEDs to WHITE");
    fill_solid(leds, NUM_LEDS, CRGB::White);
    FastLED.show();
    delay(2000);

    Serial.println("Test complete");
}

void loop()
{
    // Flash pattern to show it's working
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();
    delay(500);

    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(500);
}