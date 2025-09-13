/*
 * Simple GPIO Test - Upload this to verify GPIO 27 is working
 * Connect an LED with 220Ω resistor between GPIO 27 and GND
 * LED should blink on/off every second
 */

#include <Arduino.h>

void setup()
{
    Serial.begin(115200);
    pinMode(27, OUTPUT);
    Serial.println("GPIO 27 Test - LED should blink");
}

void loop()
{
    digitalWrite(27, HIGH);
    Serial.println("GPIO 27 HIGH");
    delay(1000);

    digitalWrite(27, LOW);
    Serial.println("GPIO 27 LOW");
    delay(1000);
}