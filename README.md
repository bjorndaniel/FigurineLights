# Figurine Lights Controller

An ESP32-based WiFi-enabled controller for WS2812 LED strips, specifically designed to control 8 LEDs arranged in 4 pairs for figurine lighting.

## Features

- **WiFi Setup**: Automatic captive portal for easy WiFi configuration
- **Web Interface**: Modern, responsive web UI for LED control
- **LED Control**: Individual control of 4 LED pairs (8 LEDs total)
- **Color & Brightness**: Full RGB color control and brightness adjustment for each pair
- **Presets**: Quick preset buttons for common lighting scenarios
- **Auto-Connect**: Automatically connects to saved WiFi networks
- **Reset Function**: Hardware reset button for WiFi settings

## Hardware Requirements

- ESP32 development board
- WS2812/WS2812B LED strip (8 LEDs)
- 5V power supply (adequate for your LED count)
- Optional: Reset button connected to GPIO0

## Pin Configuration

- **LED Data Pin**: GPIO 18 (changed for AZ-Delivery board compatibility)
- **Reset Button**: GPIO 0 (BOOT button)  
- **Status LED**: GPIO 2 (built-in LED)

## Wiring Diagram

```
ESP32                WS2812 Strip
GPIO 18   ---------> Data In
GND       ---------> GND
5V*       ---------> VCC

* Use external 5V power supply for LED strips with more than a few LEDs
```

## Installation

1. Install PlatformIO in VS Code
2. Clone this repository
3. Open the project in PlatformIO
4. Connect your ESP32
5. Build and upload: `platformio run -t upload`

> **Note**: This project uses built-in ESP32 libraries (WiFi, WebServer, DNSServer, Preferences) for maximum compatibility and reliability, avoiding external library conflicts.

## First Setup

1. After uploading, the ESP32 will create a WiFi access point named "FigurineLights-Setup"
2. Connect to this network with your phone/computer (**currently open - no password required**)
3. A captive portal will open automatically (or go to 192.168.4.1)
4. Select your WiFi network and enter the password
5. The ESP32 will connect and show its IP address in the serial monitor
6. Access the web interface at the displayed IP address

## Web Interface

The web interface provides:

- **System Information**: IP address, WiFi network, signal strength
- **Global Controls**: Turn all LEDs on/off, reset WiFi settings
- **Individual Pair Control**: 
  - Power toggle for each pair
  - Color picker
  - Brightness slider (0-100%)
- **Quick Presets**: 
  - Warm White
  - Cool White  
  - Red, Green, Blue
  - Rainbow (different color per pair)

### Status Display

The web interface includes a real-time status display showing the 5 most recent actions:
- Power on/off commands for individual pairs
- Color changes with pair identification
- Brightness adjustments with percentage values
- All on/off commands
- System events (startup, WiFi reset)

Each status entry shows the action and relative timestamp (seconds/minutes/hours ago), automatically updating every 3 seconds.

## API Endpoints

- `GET /api/status` - Get all LED pair status
- `POST /api/pair` - Control individual pair (JSON body)
- `POST /api/all/on` - Turn all LEDs on
- `POST /api/all/off` - Turn all LEDs off
- `POST /api/wifi/reset` - Reset WiFi settings
- `GET /api/info` - System information

### Example API Usage

```json
// Turn on pair 0 with red color at 50% brightness
POST /api/pair
{
  "pair": 0,
  "isOn": true,
  "color": {"r": 255, "g": 0, "b": 0},
  "brightness": 128
}
```

## Hardware Reset

Hold the BOOT button (GPIO0) for 3 seconds to reset WiFi settings. The device will restart in setup mode.

## Customization

### Change LED Count
Modify `NUM_LEDS` and `NUM_PAIRS` in `LedController.h`:

```cpp
#define NUM_LEDS 16      // Total LEDs
#define NUM_PAIRS 8      // Number of pairs
#define LEDS_PER_PAIR 2  // LEDs per pair
```

### Change LED Pin
Modify `LED_PIN` in `LedController.h`:

```cpp
#define LED_PIN 18  // GPIO pin for LED data
```

**Alternative pins for AZ-Delivery boards:** 2, 4, 16, 17, 18, 19, 21, 22, 23

### Add More Presets
Edit the `preset()` function in the JavaScript code within `main.cpp` to add custom color presets.

## Troubleshooting

### LEDs Not Working
- Check wiring (Data, GND, VCC)
- Verify power supply capacity
- Ensure LED strip is WS2812/WS2812B compatible

### WiFi Issues
- Use hardware reset (hold BOOT button 3 seconds)
- Check serial monitor for error messages
- Ensure WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)

### Web Interface Not Loading
- Check IP address in serial monitor for connected mode
- For setup mode, connect to "FigurineLights-Setup" WiFi and go to 192.168.4.1
- Ensure device is connected to same network as your computer
- Try accessing by IP address directly

## Development

### Project Structure
```
src/
├── main.cpp              # Main application with WiFi & web server
├── LedController.h/.cpp  # LED control logic
platformio.ini            # PlatformIO configuration
```

### Dependencies
- FastLED - LED control library
- ArduinoJson - JSON handling for API responses
- Built-in ESP32 libraries (WiFi, WebServer, DNSServer, Preferences)

## License

MIT License - Feel free to modify and use for your own projects!

## Contributing

Pull requests welcome! Please ensure code follows existing style and test thoroughly before submitting.