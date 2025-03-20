# Matrixmate

Overview

This project drives an LED matrix display using an ESP32 and a HUB75 panel. It offers multiple display modes—including a clock, cryptocurrency ticker, weather display, and custom animations—and provides a web-based configuration interface for setting up WiFi, display settings, time zone, and API keys.
Pinouts are provided in the sketch for everything connected for the diy minded. MatrixMate is a handy plug on board for the hub75 displays and available for a small fee at www.brokencircuitranch.com.

Features

    Multi-Mode Display:
        Clock: Displays the current time and date, synced using both an RTC (DS3231) and NTP.
        Crypto Ticker: Retrieves and displays cryptocurrency prices and 24-hour percentage changes via the CoinCap API.
        Weather Display: Shows current temperature (with unit conversion between Fahrenheit and Celsius) and weather description using the OpenWeatherMap API.
        Animation Mode: Plays animations stored on an SD card, with support for uploading new animation files via the web interface.

    Web Configuration Interface:
        Configure WiFi credentials, display matrix type, brightness, colors, timezone, crypto list, and weather API credentials.
        Manage animation files (upload, download, delete).

    Robust Connectivity:
        Automatic reconnection logic for WiFi.
        Retry logic for SD card initialization.

    Customizable UI:
        Adjustable fonts and colors for different screens.
        User-defined settings for 12/24-hour time format and temperature units.

Hardware Requirements

    MatrixMate Board  (www.brokencircuitranch.com)
    HUB75 LED Matrix Panel(s) (compatible with the ESP32-HUB75-MatrixPanel-I2S-DMA library)
    SD Card Module (for storing animation files)
    Push Button (for reset options)

Software Dependencies

    ESP32 Arduino Core
    ESP32-HUB75-MatrixPanel-I2S-DMA
    Adafruit GFX Library
    SD and SPI libraries (included with the ESP32 Arduino core)
    WiFi, WiFiUDP, HTTPClient, AsyncTCP, ESPAsyncWebServer libraries
    NTPClient
    RTClib
    TimeLib
    Timezone
    ArduinoJson
    Various fonts (FreeSansBold9pt7b, FreeSans12pt7b, TomThumb, Org_01, Picopixel)

Installation

    Hardware Setup:
        Connect the Matrixmate to your HUB75 LED matrix using the pin configuration specified in the code.
        Wire up the SD card module, RTC module, and push button to the corresponding pins.

    Software Setup:
        Install the ESP32 board definitions via the Arduino IDE Board Manager.
        Install all required libraries either via the Library Manager or from their respective GitHub repositories.
        Import the project files into your Arduino IDE.

    Compile and Upload:
        Open the project in the Arduino IDE.
        Select the correct board and COM port.
        Compile and upload the sketch to your ESP32.

Configuration

    Initial Setup:
    On first boot, if WiFi credentials are not set, the MatrixMate creates an access point named Matrixmate with the           password pass1234.
    Connect to this network and open your browser at http://192.168.4.1 to access the configuration page.

    Using the Web Interface:
        WiFi Settings: Enter your WiFi SSID and password.
        Display Configuration: Select the matrix type, adjust brightness, and choose colors for various UI elements.
        Time and API Settings: Set your timezone, cryptocurrency list, and provide weather API credentials.
        Animation Management: Upload new animations (in a supported .c format), download, or delete existing files.

Usage

    Screen Cycling:
    The device automatically cycles through different display modes every 30 seconds.

    Data Updates:
        Cryptocurrency prices update every 2 minutes.
        Weather data updates every 10 minutes.
        The clock refreshes every second.

    Reset Options:
        A short press of the button restarts the ESP32.
        A long press (approx. 10 seconds) triggers a factory reset by clearing stored preferences.

Troubleshooting

    WiFi Issues:
    Ensure correct WiFi credentials are set. The device attempts to reconnect if the connection drops.

    SD Card Errors:
    Verify the SD card is inserted and good.

    RTC/NTP Sync:
    If the time isn’t correct, The device will use NTP to sync if necessary.

    Animation Files:
    Downscale your animations to more closely match the X,Y size of your screen. the sprite sheet needs to be one image        wide and as many images high as you want.
    Confirm that animation files are in the correct format (use the LVGL Image Converter with ARGB8888 output).
    Once converted to a .c file the Matrixmate will convert them to .anim files on the sd card for playback.
    

License

    MIT License with Commons Clause (No Commercial Use):

This project is provided "as is" without any warranty and not fit for any particular purpose. For legal advice regarding licensing, please consult a professional.

Credits and Acknowledgements

    ESP32-HUB75-MatrixPanel-I2S-DMA by mrfaptastic
    Adafruit GFX Library
    ArduinoJson by Benoit Blanchon
    Thanks to all contributors of the open source libraries used in this project.
