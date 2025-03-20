/* 
  MATRIXMATE v3
MIT License Commons Clause

Copyright (c) [2025] [Kent Andersen]
*/

#include <Adafruit_GFX.h> 
#include <SD.h>
#include "esp_heap_caps.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/TomThumb.h>
#include <Fonts/Org_01.h>
#include <Fonts/Picopixel.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <map>

// SD Card SPI Pin Definitions
#define SD_SCK  33
#define SD_MISO 35
#define SD_MOSI 2
#define SD_CS   32

bool sdFailed = false;  // Flag indicating whether SD initialization failed

// Unused buffer (declared but never used)
static uint8_t hexParseBuffer[512];

// Preferences for saving configuration settings in NVS
Preferences preferences;

// Async web server for configuration and file management on port 80
AsyncWebServer server(80);

// RTC object using DS3231
RTC_DS3231 rtc;

// NTP and time client objects
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// Pointer for the HUB75 LED matrix display instance
MatrixPanel_I2S_DMA *dma_display = nullptr;

// Global strings for crypto and weather configuration
String cryptoList = "bitcoin,ethereum", weatherZipCode = "", weatherApiKey = "", currentWeatherDesc = "N/A";

// Maps to store cryptocurrency prices and percentage changes
std::map<String, float> lastCryptoPrices, currentCryptoPrices;
std::map<String, String> cryptoTickers = {{"bitcoin", "BTC"}, {"ethereum", "ETH"}, {"litecoin", "LTC"}};

// Global weather variable
float currentTemperature = 0.0;

// Timers and update flags for different sections
unsigned long lastCryptoUpdate = 0, lastScreenSwitch = 0, lastRtcSync = 0, cryptoFetchTimer = 0, weatherFetchTimer = 0, lastAnimationFrame = 0, lastClockUpdate = 0;

// Update interval constants in milliseconds
const unsigned long SCREEN_DURATION = 30000;
const unsigned long CRYPTO_UPDATE_INTERVAL = 120000;
const unsigned long WEATHER_UPDATE_INTERVAL = 600000;
const unsigned long ANIMATION_FRAME_DURATION = 120;
const unsigned long CLOCK_UPDATE_INTERVAL = 1000;

// Current display screen (0: clock, 1: crypto, 2: weather, 3: animation)
int currentScreen = 0;

// Display dimensions, determined later by configuration
int displayWidth = 64, displayHeight = 32;

// Global color variables for various UI elements
uint16_t myGREEN, myRED, myBLUE, customBgColor, customFontColor, tzColor;
uint16_t clockTimeColor, clockDateColor;
uint16_t cryptoColor1, cryptoColor2;
uint16_t weatherTempColor, weatherDescColor;

// Animation frame control
int currentAnimationFrame = 0;
bool clockNeedsUpdate = true, cryptoNeedsUpdate = true, weatherNeedsUpdate = true, animationNeedsUpdate = true;
bool rtcFailed = false;

// Button and reset definitions
#define BUTTON_PIN 34
#define RESET_TIME 3000
#define FACTORY_RESET_TIME 10000
#define DEBOUNCE_TIME 250

unsigned long buttonPressTime = 0, lastButtonEvent = 0;
bool buttonPressed = false, firstEntry = true;

// Global file used for animation operations is declared but not actually used (a local static is used instead)
File animationFile;

// Image dimension constants (unused)
const int IMAGE_WIDTH = 32;
const int IMAGE_HEIGHT = 32;

// Track last animation frame drawn to avoid unnecessary redraws
static int lastAnimationFrameDrawn = -1;

// Timezone Definitions for various regions
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};
TimeChangeRule usCDT = {"CDT", Second, Sun, Mar, 2, -300};
TimeChangeRule usCST = {"CST", First, Sun, Nov, 2, -360};
TimeChangeRule usMDT = {"MDT", Second, Sun, Mar, 2, -360};
TimeChangeRule usMST = {"MST", First, Sun, Nov, 2, -420};
TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};
TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};
TimeChangeRule usAZT = {"MST", First, Sun, Jan, 1, -420};
Timezone *currentTimezone = nullptr;

/*
 * Function: displaySetup
 * ----------------------
 * Reads display configuration and color settings from preferences and initializes the
 * HUB75 LED matrix display based on the selected matrix configuration.
 */
void displaySetup() {
  preferences.begin("config", true);
  String matrixConfig = preferences.getString("matrixConfig", "1x64x64");
  weatherZipCode = preferences.getString("weatherZip", "");
  weatherApiKey = preferences.getString("weatherApi", "");
  String tzName = preferences.getString("timezone", "Eastern");
  int brightness = preferences.getInt("brightness", 80);
  String fgColor = preferences.getString("fontColor", "#FFFFFF");
  String bgColor = preferences.getString("bgColor", "#000000");
  String tzColorStr = preferences.getString("tzColor", "#00FF00");
  String clockTimeColorStr = preferences.getString("clockTimeColor", "#FFFFFF");
  String clockDateColorStr = preferences.getString("clockDateColor", "#00FFFF");
  String cryptoColor1Str = preferences.getString("cryptoColor1", "#FFFF00");
  String cryptoColor2Str = preferences.getString("cryptoColor2", "#FF00FF");
  String weatherDescColorStr = preferences.getString("weatherDescColor", "#FFFFFF");
  preferences.end();

  // Set timezone based on saved configuration
  setTimezone(tzName);

  // Determine matrix dimensions based on selected configuration string
  int width, height, chain;
  if (matrixConfig == "1x64x64") { width = 64; height = 64; chain = 1; }
  else if (matrixConfig == "2x64x64") { width = 64; height = 64; chain = 2; }
  else if (matrixConfig == "3x64x64") { width = 64; height = 64; chain = 3; }
  else if (matrixConfig == "1x32x64") { width = 64; height = 32; chain = 1; }
  else if (matrixConfig == "2x32x64") { width = 64; height = 32; chain = 2; }
  else if (matrixConfig == "3x32x64") { width = 64; height = 32; chain = 3; }
  else if (matrixConfig == "4x32x64") { width = 64; height = 32; chain = 4; }
  else if (matrixConfig == "1x64x128") { width = 128; height = 64; chain = 1; }
  else if (matrixConfig == "2x64x128") { width = 128; height = 64; chain = 2; }
  else { width = 64; height = 64; chain = 1; }

  // Setup the HUB75 configuration structure
  HUB75_I2S_CFG mxconfig(width, height, chain);
  mxconfig.gpio.r1 = 25; mxconfig.gpio.g1 = 26; mxconfig.gpio.b1 = 27;
  mxconfig.gpio.r2 = 14; mxconfig.gpio.g2 = 12; mxconfig.gpio.b2 = 13;
  mxconfig.gpio.a = 23; mxconfig.gpio.b = 19; mxconfig.gpio.c = 5;
  mxconfig.gpio.d = 17; mxconfig.gpio.e = 18; mxconfig.gpio.clk = 16;
  mxconfig.gpio.lat = 4; mxconfig.gpio.oe = 15;
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_15M;
  mxconfig.clkphase = false;
  
  // Instantiate the display and set brightness and colors
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setPanelBrightness(brightness);
  myGREEN = dma_display->color565(0, 255, 0);
  myRED = dma_display->color565(255, 0, 0);
  myBLUE = dma_display->color565(0, 0, 255);
  customFontColor = hexToColor565(fgColor);
  customBgColor = hexToColor565(bgColor);
  tzColor = hexToColor565(tzColorStr);
  clockTimeColor = hexToColor565(clockTimeColorStr);
  clockDateColor = hexToColor565(clockDateColorStr);
  cryptoColor1 = hexToColor565(cryptoColor1Str);
  cryptoColor2 = hexToColor565(cryptoColor2Str);
  weatherDescColor = hexToColor565(weatherDescColorStr);
  
  displayWidth = width * chain;
  displayHeight = height;
}

/*
 * Function: serveConfigPage
 * -------------------------
 * Sets up the various web server endpoints used for:
 *  - Serving the configuration page (GET "/")
 *  - Handling file uploads and listing/deleting animation files (GET/POST "/upload", "/download", "/delete")
 *  - Saving configuration settings (POST "/save")
 */
void serveConfigPage() {
    // Serve main configuration form
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<html><body><h1>Config</h1><form method='post' action='/save'>"
                      "WiFi Name: <input type='text' name='wifiName' maxlength='32'><br>"
                      "Password: <input type='password' name='wifiPassword' maxlength='64'><br>"
                      "Matrix: <select name='matrixConfig'>"
                      "<option value='1x64x64'>1x64x64</option><option value='2x64x64'>2x64x64</option>"
                      "<option value='3x64x64'>3x64x64</option><option value='1x32x64'>1x32x64</option>"
                      "<option value='2x32x64'>2x32x64</option><option value='3x32x64'>3x32x64</option>"
                      "<option value='4x32x64'>4x32x64</option><option value='1x64x128'>1x64x128</option>"
                      "<option value='2x64x128'>2x64x128</option></select><br>"
                      "Timezone: <select name='timezone'><option value='Eastern'>Eastern</option>"
                      "<option value='Central'>Central</option><option value='Mountain'>Mountain</option>"
                      "<option value='Pacific'>Pacific</option><option value='Arizona'>Arizona</option></select><br>"
                      "Crypto List: <input type='text' name='cryptoList' value='bitcoin,ethereum' maxlength='100'><br>"
                      "Weather ZIP: <input type='text' name='weatherZip' maxlength='10'><br>"
                      "Weather API Key: <input type='text' name='weatherApi' maxlength='32'><br>"
                      "Brightness: <input type='number' name='brightness' min='0' max='255' value='80'><br>"
                      "Font Color: <input type='color' name='fontColor' value='#FFFFFF'><br>"
                      "BG Color: <input type='color' name='bgColor' value='#000000'><br>"
                      "TZ Color: <input type='color' name='tzColor' value='#00FF00'><br>"
                      "Clock Time Color: <input type='color' name='clockTimeColor' value='#FFFFFF'><br>"
                      "Clock Date Color: <input type='color' name='clockDateColor' value='#00FFFF'><br>"
                      "Crypto Color 1: <input type='color' name='cryptoColor1' value='#FFFF00'><br>"
                      "Crypto Color 2: <input type='color' name='cryptoColor2' value='#FF00FF'><br>"
                      "Weather Text Color: <input type='color' name='weatherDescColor' value='#FFFFFF'><br>"
                      "24-Hour: <input type='checkbox' name='use24Hour'><br>"
                      "Celsius: <input type='checkbox' name='useCelsius'><br>" // New toggle for Celsius/Fahrenheit
                      "<input type='submit' value='Save'></form>"
                      "<h2><a href='/upload'>Upload Animation</a></h2></body></html>";
        request->send(200, "text/html", html);
    });

    // Serve upload page and list existing .anim files
    server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<html><body><h1>Upload Animation</h1>"
                      "<form method='post' action='/upload' enctype='multipart/form-data'>"
                      "<input type='file' name='file'><br><input type='submit' value='Upload'></form>"
                      "<p>Convert GIFs to .c via LVGL Image Converter (ARGB8888)</p>";
        if (!sdFailed) {
            html += "<h2>Manage .anim Files</h2><ul>";
            File root = SD.open("/");
            File entry;
            while ((entry = root.openNextFile())) {
                String name = entry.name();
                if (name[0] != '.' && name.endsWith(".anim")) {
                    html += "<li>" + name + " "
                            "<a href='/download?file=" + name + "' download>Download</a> "
                            "<form action='/delete' method='post' style='display:inline;'>"
                            "<input type='hidden' name='file' value='" + name + "'>"
                            "<input type='submit' value='Delete' onclick='return confirm(\"Delete " + name + "?\");'></form></li>";
                }
                entry.close();
            }
            root.close();
            html += "</ul>";
        } else {
            html += "<p>SD card not available</p>";
        }
        html += "</body></html>";
        request->send(200, "text/html", html);
    });

    // Endpoint to download an animation file from the SD card
    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!sdFailed && request->hasParam("file")) {
            String fileName = request->getParam("file")->value();
            if (fileName.endsWith(".anim")) {
                String filePath = "/" + fileName;
                if (SD.exists(filePath)) {
                    File file = SD.open(filePath, FILE_READ);
                    if (file) {
                        request->send(file, fileName, "application/octet-stream");
                        file.close();
                        return;
                    }
                }
            }
        }
        request->send(404, "text/plain", "File not found or SD error");
    });

    // Endpoint to delete an animation file from the SD card
    server.on("/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!sdFailed && request->hasArg("file")) {
            String fileName = request->arg("file");
            if (fileName.endsWith(".anim")) {
                String filePath = "/" + fileName;
                if (SD.exists(filePath)) {
                    if (SD.remove(filePath)) {
                        Serial.println("Deleted file: " + filePath);
                        request->send(200, "text/html", "<html><body><h1>File Deleted</h1><p>" + fileName + " deleted successfully.</p><a href='/upload'>Back to Upload</a></body></html>");
                    } else {
                        request->send(500, "text/plain", "Failed to delete file: " + fileName);
                    }
                } else {
                    request->send(404, "text/plain", "File not found: " + fileName);
                }
            } else {
                request->send(400, "text/plain", "Only .anim files can be deleted");
            }
        } else {
            request->send(400, "text/plain", "Invalid request");
        }
    });

    // Endpoint to save configuration settings
    server.on("/save", HTTP_POST, handleSaveConfig);

    // Endpoint to handle file uploads for animation data
    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Upload complete");
    }, handleFileUpload);

    server.begin();
}

/*
 * Function: handleSaveConfig
 * --------------------------
 * Saves configuration parameters submitted from the web form into preferences.
 * After saving, it sends a confirmation response and then restarts the ESP32.
 */
void handleSaveConfig(AsyncWebServerRequest *request) {
    preferences.begin("config", false);
    preferences.putString("wifiName", request->arg("wifiName").substring(0, 32));
    preferences.putString("wifiPassword", request->arg("wifiPassword").substring(0, 64));
    preferences.putString("matrixConfig", request->arg("matrixConfig").substring(0, 10));
    preferences.putString("timezone", request->arg("timezone").substring(0, 10));
    preferences.putString("cryptoList", request->arg("cryptoList").substring(0, 100));
    preferences.putString("weatherZip", request->arg("weatherZip").substring(0, 10));
    preferences.putString("weatherApi", request->arg("weatherApi").substring(0, 32));
    preferences.putInt("brightness", constrain(request->arg("brightness").toInt(), 0, 255));
    preferences.putString("fontColor", request->arg("fontColor").substring(0, 7));
    preferences.putString("bgColor", request->arg("bgColor").substring(0, 7));
    preferences.putString("tzColor", request->arg("tzColor").substring(0, 7));
    preferences.putString("clockTimeColor", request->arg("clockTimeColor").substring(0, 7));
    preferences.putString("clockDateColor", request->arg("clockDateColor").substring(0, 7));
    preferences.putString("cryptoColor1", request->arg("cryptoColor1").substring(0, 7));
    preferences.putString("cryptoColor2", request->arg("cryptoColor2").substring(0, 7));
    preferences.putString("weatherDescColor", request->arg("weatherDescColor").substring(0, 7));
    preferences.putBool("use24Hour", request->hasArg("use24Hour"));
    preferences.putBool("useCelsius", request->hasArg("useCelsius")); // Save Celsius preference
    preferences.putBool("setupComplete", true);
    preferences.end();
    request->send(200, "text/html", "<html><body><h1>Saved</h1><p>Rebooting...</p></body></html>");
    delay(1000);
    ESP.restart();
}

/*
 * Function: handleFileUpload
 * --------------------------
 * Handles the file upload of an animation file (in .c format).
 * It extracts the image data defined as a hex map from the uploaded file,
 * writes the binary data to a new .anim file on the SD card, and verifies the data.
 */
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File animFile;
    static uint16_t w = 0, h = 0;
    static uint32_t dataSize = 0, bytesWritten = 0;
    static bool mapStarted = false;
    static String tailBuffer;
    static String hexBuffer;
    static String mapName;

    auto isHexDigit = [](char c) -> bool {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    };

    if (sdFailed) {
        request->send(500, "text/plain", "SD card not available");
        return;
    }

    // On first call, prepare the file and buffers
    if (!index) {
        filename.toLowerCase();
        if (!filename.endsWith(".c")) {
            Serial.println("Rejected file: " + filename + " (not .c)");
            request->send(400, "text/plain", "Only .c files allowed. Detected: " + filename);
            return;
        }
        String animName = "/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim";
        SD.remove(animName);
        animFile = SD.open(animName, FILE_WRITE);
        if (!animFile) {
            request->send(500, "text/plain", "Failed to create file: " + animName);
            return;
        }
        // Write 16 bytes of 0xFF as a header placeholder
        uint8_t placeholder[16];
        memset(placeholder, 0xFF, 16);
        if (animFile.write(placeholder, 16) != 16) {
            animFile.close();
            request->send(500, "text/plain", "Failed to write header placeholder");
            return;
        }
        mapStarted = false;
        bytesWritten = 0;
        dataSize = 0;
        tailBuffer = "";
        hexBuffer = "";
        mapName = "";
        tailBuffer.reserve(1024);
        hexBuffer.reserve(147456);
        Serial.println("Started upload: " + animName);
    }

    // Append current chunk data to the tail buffer
    String chunk((char*)data, len);
    tailBuffer += chunk;
    if (tailBuffer.length() > 1024) {
        tailBuffer = tailBuffer.substring(tailBuffer.length() - 1024);
    }

    // Search for the start of the hex map in the file
    if (!mapStarted) {
        int mapPos = chunk.indexOf("uint8_t ");
        if (mapPos >= 0) {
            int mapEnd = chunk.indexOf("_map[] = {", mapPos);
            if (mapEnd >= 0) {
                mapStarted = true;
                mapName = chunk.substring(mapPos + 8, mapEnd + 4);
                Serial.println("Found map: " + mapName);
                int start = mapEnd + 10;
                hexBuffer = chunk.substring(start);
            }
        }
    } else {
        hexBuffer += chunk;
    }

    // Process hex buffer: remove formatting and convert hex strings to binary bytes
    if (mapStarted) {
        hexBuffer.replace("0x", "");
        hexBuffer.replace(", ", "");
        hexBuffer.replace(",", "");
        hexBuffer.replace("\n", "");
        hexBuffer.replace("\r", "");
        hexBuffer.replace("\t", "");
        hexBuffer.replace(" ", "");
        hexBuffer.trim();

        if (bytesWritten == 0) {
            Serial.println("First hex chunk: " + hexBuffer.substring(0, min(16, (int)hexBuffer.length())));
        }

        int i = 0;
        while (i + 1 < hexBuffer.length()) {
            if (!isHexDigit(hexBuffer.charAt(i)) || !isHexDigit(hexBuffer.charAt(i + 1))) break;
            String byteStr = hexBuffer.substring(i, i + 2);
            uint8_t byte = strtol(byteStr.c_str(), NULL, 16);
            animFile.write(byte);
            bytesWritten++;
            i += 2;
        }
        hexBuffer = hexBuffer.substring(i);
        Serial.println("Bytes written this chunk: " + String(i / 2) + ", total bytesWritten=" + String(bytesWritten));
    }

    if (!final) return;

    // Finalize file upload: validate data and parse header info from the LVGL struct
    Serial.println("Final bytesWritten before check: " + String(bytesWritten));
    Serial.println("Tail buffer length: " + String(tailBuffer.length()));
    Serial.println("Remaining hex buffer: " + hexBuffer.substring(0, min(50, (int)hexBuffer.length())));

    int structPos = tailBuffer.indexOf("lv_image_dsc_t ");
    if (structPos < 0) {
        animFile.close();
        SD.remove("/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim");
        Serial.println("Error: No 'lv_image_dsc_t' struct found");
        request->send(400, "text/plain", "Invalid LVGL .c file: struct not found");
        return;
    }

    String structBlock = tailBuffer.substring(structPos);
    if (structBlock.indexOf("LV_COLOR_FORMAT_ARGB8888") < 0) {
        animFile.close();
        SD.remove("/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim");
        Serial.println("Error: Only ARGB8888 supported");
        request->send(400, "text/plain", "Only ARGB8888 supported");
        return;
    }

    int dataPos = structBlock.indexOf(".data = " + mapName);
    if (dataPos < 0) {
        animFile.close();
        SD.remove("/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim");
        Serial.println("Error: Struct does not reference " + mapName);
        request->send(400, "text/plain", "Invalid LVGL .c file: struct does not match map");
        return;
    }

    int wPos = structBlock.indexOf(".header.w =");
    int hPos = structBlock.indexOf(".header.h =");
    int dataSizePos = structBlock.indexOf(".data_size =");
    if (wPos < 0 || hPos < 0 || dataSizePos < 0) {
        animFile.close();
        SD.remove("/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim");
        Serial.println("Invalid LVGL .c format");
        request->send(400, "text/plain", "Invalid LVGL .c file format");
        return;
    }

    w = structBlock.substring(wPos + 11, structBlock.indexOf(",", wPos)).toInt();
    h = structBlock.substring(hPos + 11, structBlock.indexOf(",", hPos)).toInt();
    String dataSizeStr = structBlock.substring(dataSizePos + 12, structBlock.indexOf(",", dataSizePos));
    if (dataSizeStr.indexOf("*") >= 0) {
        int base = dataSizeStr.substring(0, dataSizeStr.indexOf("*")).toInt();
        int mult = dataSizeStr.substring(dataSizeStr.indexOf("*") + 1).toInt();
        dataSize = base * mult;
    } else {
        dataSize = dataSizeStr.toInt();
    }

    Serial.println("Parsed values: w=" + String(w) + ", h=" + String(h) + ", dataSize=" + String(dataSize));

    if (dataSize != bytesWritten) {
        if (bytesWritten < dataSize) {
            while (hexBuffer.length() >= 2 && bytesWritten < dataSize) {
                if (!isHexDigit(hexBuffer.charAt(0)) || !isHexDigit(hexBuffer.charAt(1))) break;
                String byteStr = hexBuffer.substring(0, 2);
                uint8_t byte = strtol(byteStr.c_str(), NULL, 16);
                animFile.write(byte);
                bytesWritten++;
                hexBuffer = hexBuffer.substring(2);
            }
        }
        if (dataSize != bytesWritten) {
            animFile.close();
            SD.remove("/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim");
            Serial.println("Error: Data size mismatch: expected " + String(dataSize) + ", wrote " + String(bytesWritten));
            request->send(400, "text/plain", "Data size mismatch");
            return;
        }
    }

    if (dataSize > 262144) {
        animFile.close();
        SD.remove("/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim");
        Serial.println("Error: Animation too large (max 256KB)");
        request->send(400, "text/plain", "Animation too large (max 256KB)");
        return;
    }

    // Calculate frame count and validate frame dimensions
    uint16_t frameHeight = (h <= 150) ? h : 32;
    uint16_t frameCount = (w * frameHeight * 4) ? dataSize / (w * frameHeight * 4) : 1;
    if (dataSize % (w * frameHeight * 4) != 0) {
        animFile.close();
        SD.remove("/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim");
        Serial.println("Error: Data size not aligned to frames");
        request->send(400, "text/plain", "Data size not aligned to frames");
        return;
    }
    if (w > 150 || frameHeight > 150) {
        animFile.close();
        SD.remove("/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim");
        Serial.println("Error: Invalid frame dimensions (max 150x150)");
        request->send(400, "text/plain", "Invalid frame dimensions (max 150x150)");
        return;
    }

    // Write the header with proper image dimensions and frame count
    uint8_t header[16] = {0x46, 0x49, 0x56, 0x4c, 0x08, 0x88,
                         w & 0xff, (w >> 8) & 0xff,
                         frameHeight & 0xff, (frameHeight >> 8) & 0xff,
                         frameCount & 0xff, (frameCount >> 8) & 0xff,
                         0, 0, 0, 0};
    animFile.seek(0);
    if (animFile.write(header, 16) != 16) {
        animFile.close();
        SD.remove("/" + filename.substring(0, filename.lastIndexOf('.')) + ".anim");
        Serial.println("Error: Failed to write header");
        request->send(500, "text/plain", "Failed to write header");
        return;
    }

    animFile.close();
    Serial.println("Uploaded " + filename + ": " + String(w) + "x" + String(frameHeight) + ", " + String(frameCount) + " frames, dataSize=" + String(dataSize));
    request->send(200, "text/plain", "Uploaded: " + filename + " (" + w + "x" + frameHeight + ", " + frameCount + " frames)");
}

/*
 * Function: syncTime
 * ------------------
 * Synchronizes the RTC and system time using NTP.
 * It only updates if the device is connected to WiFi and if the RTC is functioning.
 */
void syncTime() {
  if (WiFi.status() != WL_CONNECTED || rtcFailed) return;
  timeClient.update();
  if (timeClient.getEpochTime() > 0) {
    unsigned long epoch = timeClient.getEpochTime();
    rtc.adjust(DateTime(epoch));
    setTime(epoch);
    Serial.println("Time synced: " + String(epoch));
  }
}

/*
 * Function: displayClock
 * ----------------------
 * Displays the current time and date on the matrix.
 * It retrieves the time (using either the RTC or system time), formats it based on 24-hour or 12-hour mode,
 * and draws the time and date strings centered on the display.
 */
void displayClock() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < CLOCK_UPDATE_INTERVAL) return;
  lastUpdate = millis();
  
  dma_display->fillScreen(customBgColor);
  time_t localTime = currentTimezone->toLocal(rtcFailed ? now() : rtc.now().unixtime());
  preferences.begin("config", true);
  bool use24Hour = preferences.getBool("use24Hour", false);
  preferences.end();
  
  String timeStr = use24Hour ? String(hour(localTime)) + ":" + (minute(localTime) < 10 ? "0" : "") + String(minute(localTime))
                            : String(hourFormat12(localTime)) + ":" + (minute(localTime) < 10 ? "0" : "") + String(minute(localTime));
  String ampmStr = use24Hour ? "" : (isPM(localTime) ? " PM" : " AM");
  
  int16_t x1, y1; uint16_t w, h, ampmW = 0, ampmH = 0;
  
  dma_display->setFont(displayHeight <= 32 ? &Picopixel : &FreeSansBold9pt7b);
  dma_display->getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  int yCenter = (displayHeight - h) / 2;

  if (!use24Hour) {
    dma_display->setFont(&Picopixel);
    dma_display->getTextBounds(ampmStr, 0, 0, &x1, &y1, &ampmW, &ampmH);
  }

  int totalWidth = w + (use24Hour ? 0 : ampmW + 2);
  int startX = (displayWidth - totalWidth) / 2;

  dma_display->setFont(displayHeight <= 32 ? &Picopixel : &FreeSansBold9pt7b);
  dma_display->setTextColor(clockTimeColor);
  dma_display->setCursor(startX, yCenter);
  dma_display->print(timeStr);

  if (!use24Hour) {
    dma_display->setFont(&Picopixel);
    dma_display->setCursor(startX + w + 2, yCenter - (h - ampmH) / 2);
    dma_display->print(ampmStr);
  }

  String dateStr = String(day(localTime)) + " " + String(monthShortStr(month(localTime))) + " " + String(year(localTime));
  dma_display->setFont(displayHeight <= 32 ? &TomThumb : &Org_01);
  dma_display->getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  dma_display->setTextColor(clockDateColor);
  dma_display->setCursor((displayWidth - w) / 2, displayHeight - h - 2);
  dma_display->print(dateStr);
  
  clockNeedsUpdate = false;
}

/*
 * Function: displayCrypto
 * -----------------------
 * Displays cryptocurrency information on the matrix.
 * It loops through the current crypto prices, shows the ticker symbol, current price,
 * and the 24-hour percentage change (using different colors to indicate gains or losses).
 */
void displayCrypto() {
  dma_display->fillScreen(customBgColor);
  int16_t x1, y1; uint16_t w, h;
  dma_display->setFont(&FreeSansBold9pt7b);
  dma_display->getTextBounds("A", 0, 0, &x1, &y1, &w, &h);
  int yStart = h + 2, yPos = yStart, count = 0;
  for (auto const& pair : currentCryptoPrices) {
    String t = cryptoTickers[pair.first];
    dma_display->setFont(&FreeSansBold9pt7b);
    dma_display->setTextColor(count == 0 ? cryptoColor1 : cryptoColor2);
    dma_display->getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
    dma_display->setCursor(displayWidth / 2 - w / 2, yPos);
    dma_display->print(t);
    String p = "$" + String(pair.second, 2);
    dma_display->setFont(displayHeight <= 32 ? &TomThumb : &Org_01);
    dma_display->setTextColor(lastCryptoPrices[pair.first] > 0 ? myGREEN : myRED);
    dma_display->getTextBounds(p, 0, 0, &x1, &y1, &w, &h);
    dma_display->setCursor(displayWidth / 2 - w / 2, yPos + h + 2);
    dma_display->print(p);
    String c = String(lastCryptoPrices[pair.first], 2) + "%";
    dma_display->getTextBounds(c, 0, 0, &x1, &y1, &w, &h);
    dma_display->setCursor(displayWidth / 2 - w / 2, yPos + h + 8);
    dma_display->print(c);
    count++;
    yPos = (count == 1 ? yStart + 30 : displayHeight - 10);
    if (count >= 2) break;
  }
  cryptoNeedsUpdate = false;
}

/*
 * Function: displayWeather
 * ------------------------
 * Displays weather information on the matrix.
 * It shows the current temperature (converted to Celsius if selected), a degree symbol with unit,
 * a short weather description, and a high/low estimate.
 */
void displayWeather() {
    dma_display->fillScreen(customBgColor);
    int16_t x1, y1;
    uint16_t w, h;

    // Load unit preference (Celsius or Fahrenheit)
    preferences.begin("config", true);
    bool useCelsius = preferences.getBool("useCelsius", false);
    preferences.end();

    // Convert temperature if needed and format the temperature string
    float displayTemp = useCelsius ? (currentTemperature - 32) * 5.0 / 9.0 : currentTemperature;
    String tempStr = String(displayTemp, 0);

    // Determine size for temperature text using a larger font
    dma_display->setFont(&FreeSans12pt7b);
    dma_display->getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
    uint16_t tempHeight = h;
    uint16_t tempWidth = w;

    // Prepare degree symbol and unit using a smaller font
    dma_display->setFont(&TomThumb);
    String degreeUnit = String((char)176) + (useCelsius ? "C" : "F");
    dma_display->getTextBounds(degreeUnit, 0, 0, &x1, &y1, &w, &h);
    uint16_t degreeHeight = h;
    uint16_t degreeWidth = w;

    // Get weather description dimensions
    dma_display->setFont(displayHeight <= 32 ? &TomThumb : &Org_01);
    dma_display->getTextBounds(currentWeatherDesc, 0, 0, &x1, &y1, &w, &h);
    uint16_t descHeight = h;

    // Calculate high and low strings (using an arbitrary range)
    String hl = "H" + String(displayTemp + 5, 0) + " L" + String(displayTemp - 5, 0);
    dma_display->getTextBounds(hl, 0, 0, &x1, &y1, &w, &h);
    uint16_t hlHeight = h;

    // Calculate vertical spacing and overall height for layout
    const int spacing = max(2, displayHeight / 16);
    int totalHeight = tempHeight + descHeight + hlHeight + 2 * spacing;
    int yStart = max(2, (displayHeight - totalHeight) / 2);

    // Draw temperature value with gradient color based on temperature
    dma_display->setFont(&FreeSans12pt7b);
    dma_display->setTextColor(getTempColor(currentTemperature));
    dma_display->getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
    int tempX = (displayWidth - (tempWidth + degreeWidth + 2)) / 2;
    dma_display->setCursor(tempX, yStart + tempHeight);
    dma_display->print(tempStr);

    // Draw degree symbol and unit
    dma_display->setFont(&TomThumb);
    dma_display->setTextColor(getTempColor(currentTemperature));
    dma_display->setCursor(tempX + tempWidth + 2, yStart + degreeHeight);
    dma_display->print(degreeUnit);

    // Draw weather description
    dma_display->setFont(displayHeight <= 32 ? &TomThumb : &Org_01);
    dma_display->setTextColor(weatherDescColor);
    dma_display->getTextBounds(currentWeatherDesc, 0, 0, &x1, &y1, &w, &h);
    dma_display->setCursor((displayWidth - w) / 2, yStart + tempHeight + spacing + descHeight);
    dma_display->print(currentWeatherDesc);

    // Draw high/low text if space permits
    dma_display->setTextColor(getTempColor(currentTemperature));
    dma_display->getTextBounds(hl, 0, 0, &x1, &y1, &w, &h);
    int hlY = yStart + tempHeight + descHeight + 2 * spacing + hlHeight;
    if (hlY + hlHeight <= displayHeight) {
        dma_display->setCursor((displayWidth - w) / 2, hlY);
        dma_display->print(hl);
    }

    weatherNeedsUpdate = false;
}

/*
 * Function: displayAnimation
 * --------------------------
 * Displays a looping animation from an SD card file.
 * On first entry, it selects a random .anim file from the SD card, reads its header to get frame data,
 * then in each call, it advances the frame based on a fixed frame duration and scales the image to fit the display.
 */
void displayAnimation() {
    static File animFile;
    static uint16_t animWidth = 0, frameHeight = 0, frameCount = 0;
    static uint32_t dataSize = 0; // Total data size for the animation
    static uint8_t frame = 0;

    // On entering animation screen, select a random animation file from the SD card
    if (firstEntry) {
        dma_display->fillScreen(customBgColor);
        if (animFile) {
            animFile.close(); // Close previous file if any
        }

        if (!sdFailed) {
            File root = SD.open("/");
            String animFiles[10];
            int animCount = 0;
            File entry;
            while ((entry = root.openNextFile()) && animCount < 10) {
                String name = entry.name();
                if (name[0] != '.' && name.endsWith(".anim")) {
                    animFiles[animCount++] = name;
                }
                entry.close();
            }
            root.close();

            if (animCount > 0) {
                int randomIdx = random(animCount);
                String animPath = "/" + animFiles[randomIdx];
                animFile = SD.open(animPath, FILE_READ);
                if (animFile) {
                    uint8_t header[16];
                    animFile.read(header, 16);
                    if (header[0] != 'F' || header[1] != 'I' || header[2] != 'V' || header[3] != 'L' ||
                        header[4] != 0x08 || header[5] != 0x88) {
                        animFile.close();
                        return;
                    }
                    animWidth = header[6] | (header[7] << 8);
                    frameHeight = header[8] | (header[9] << 8);
                    frameCount = header[10] | (header[11] << 8);
                    dataSize = animWidth * frameHeight * 4 * frameCount;
                    if (animWidth == 0 || frameHeight == 0 || frameCount == 0) {
                        animFile.close();
                        return;
                    }
                } else {
                    return;
                }
            } else {
                return;
            }
        }
        firstEntry = false;
        frame = 0;
    }

    if (!animFile) return;

    int frameSize = animWidth * frameHeight * 4; // Each frame is in ARGB8888 format

    // Advance the frame if the frame duration has passed
    if (millis() - lastAnimationFrame >= ANIMATION_FRAME_DURATION) {
        frame = (frame + 1) % frameCount;
        lastAnimationFrame = millis();
        animationNeedsUpdate = true;
    }

    // Only redraw if needed
    if (animationNeedsUpdate || lastAnimationFrameDrawn != frame) {
        uint32_t seekPos = 16 + frame * frameSize;
        if (!animFile.seek(seekPos)) {
            animFile.close();
            firstEntry = true;
            return;
        }

        uint8_t* frameBuffer = (uint8_t*)malloc(frameSize);
        if (!frameBuffer) {
            animFile.close();
            firstEntry = true;
            return;
        }

        size_t bytesRead = animFile.read(frameBuffer, frameSize);
        if (bytesRead != frameSize) {
            free(frameBuffer);
            animFile.close();
            firstEntry = true;
            return;
        }

        // Scale the animation to fit the display
        int actualWidth = dma_display->width();
        int actualHeight = dma_display->height();
        float scaleX = (float)actualWidth / animWidth;
        float scaleY = (float)actualHeight / frameHeight;
        int scale = min(scaleX, scaleY);
        if (scale < 1) scale = 1;
        int spriteWidth = animWidth * scale;
        int spriteHeight = frameHeight * scale;
        int xOffset = (actualWidth - spriteWidth) / 2;
        int yOffset = (actualHeight - spriteHeight) / 2;

        dma_display->fillScreen(customBgColor);

        // Draw each pixel of the frame, scaling as necessary
        for (int y = 0; y < frameHeight; y++) {
            for (int x = 0; x < animWidth; x++) {
                uint32_t idx = (y * animWidth + x) * 4;
                uint8_t r = frameBuffer[idx + 1];
                uint8_t g = frameBuffer[idx + 2];
                uint8_t b = frameBuffer[idx + 3];
                uint16_t color = dma_display->color565(r, g, b);
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        int drawX = xOffset + (x * scale) + dx;
                        int drawY = yOffset + (y * scale) + dy;
                        if (drawX >= 0 && drawX < actualWidth && drawY >= 0 && drawY < actualHeight) {
                            dma_display->drawPixel(drawX, drawY, color);
                        }
                    }
                }
            }
        }

        free(frameBuffer);
        lastAnimationFrameDrawn = frame;
        animationNeedsUpdate = false;
    }
}

/*
 * Function: updateCryptoPrices
 * ----------------------------
 * Checks if it’s time to update cryptocurrency prices.
 * If connected to WiFi and the update interval has passed, it makes an HTTP GET request to a crypto API,
 * parses the JSON response, and updates the global maps with the latest prices and percent changes.
 */
void updateCryptoPrices() {
  if (millis() - lastCryptoUpdate < CRYPTO_UPDATE_INTERVAL || WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin("https://api.coincap.io/v2/assets?ids=" + cryptoList);
  if (http.GET() == 200) {
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, http.getString());
    currentCryptoPrices.clear();
    for (JsonVariant v : doc["data"].as<JsonArray>()) {
      String id = v["id"].as<String>();
      currentCryptoPrices[id] = v["priceUsd"].as<float>();
      lastCryptoPrices[id] = v["changePercent24Hr"].as<float>();
    }
    lastCryptoUpdate = millis();
    cryptoNeedsUpdate = true;
  }
  http.end();
}

/*
 * Function: updateWeather
 * -----------------------
 * Checks if it’s time to update weather data.
 * If connected to WiFi and both ZIP and API key are provided, it makes an HTTP GET request to the OpenWeatherMap API,
 * then parses the JSON response to update the global temperature and weather description.
 */
void updateWeather() {
    if (millis() - weatherFetchTimer < WEATHER_UPDATE_INTERVAL || WiFi.status() != WL_CONNECTED || weatherZipCode.length() == 0 || weatherApiKey.length() == 0) return;
    preferences.begin("config", true);
    bool useCelsius = preferences.getBool("useCelsius", false);
    preferences.end();
    String units = useCelsius ? "metric" : "imperial";
    HTTPClient http;
    http.begin("http://api.openweathermap.org/data/2.5/weather?zip=" + weatherZipCode + ",us&appid=" + weatherApiKey + "&units=" + units);
    if (http.GET() == 200) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, http.getString());
        currentTemperature = doc["main"]["temp"];
        currentWeatherDesc = doc["weather"][0]["main"].as<String>();
        weatherFetchTimer = millis();
        weatherNeedsUpdate = true;
    }
    http.end();
}

/*
 * Function: updateScreen
 * ----------------------
 * Determines which display function to call based on the current screen index.
 * It calls the appropriate update function (clock, crypto, weather, or animation) if its update flag is set.
 */
void updateScreen() {
  switch (currentScreen) {
    case 0: if (clockNeedsUpdate) displayClock(); break;
    case 1: if (cryptoNeedsUpdate) displayCrypto(); break;
    case 2: if (weatherNeedsUpdate) displayWeather(); break;
    case 3: displayAnimation(); break;
    default: dma_display->fillScreen(customBgColor); break;
  }
}

/*
 * Function: checkButton
 * ---------------------
 * Monitors the state of a physical button, debouncing the signal and
 * determining if a short press (restart) or long press (factory reset) occurred.
 */
void checkButton() {
    static int lastState = HIGH; // Assume button unpressed (floating HIGH)
    int currentState = digitalRead(BUTTON_PIN);
    unsigned long now = millis();

    // Debounce: only respond to changes if enough time has passed
    if (currentState != lastState && (now - lastButtonEvent) > DEBOUNCE_TIME) {
        if (currentState == LOW) { // Button pressed
            buttonPressTime = now;
            buttonPressed = true;
            lastButtonEvent = now;
        } else if (buttonPressed && currentState == HIGH) { // Button released
            unsigned long pressDuration = now - buttonPressTime;
            buttonPressed = false;

            // Short press triggers a restart
            if (pressDuration >= 500 && pressDuration < FACTORY_RESET_TIME) {
                ESP.restart();
            }
            // Long press triggers factory reset
            else if (pressDuration >= FACTORY_RESET_TIME) {
                preferences.begin("config", false);
                preferences.clear();
                preferences.end();
                ESP.restart();
            }
            lastButtonEvent = now;
        }
        lastState = currentState;
    }

    // Also check if button is held long enough to force factory reset
    if (buttonPressed && (now - buttonPressTime) >= FACTORY_RESET_TIME) {
        preferences.begin("config", false);
        preferences.clear();
        preferences.end();
        ESP.restart();
    }
}

/*
 * Function: hexToColor565
 * -----------------------
 * Converts a hex color string (e.g., "#FF00FF") into a 16-bit color value in RGB565 format
 * using the display object's color conversion method.
 */
uint16_t hexToColor565(String hexColor) {
  if (hexColor.startsWith("#")) hexColor = hexColor.substring(1);
  long number = strtol(hexColor.c_str(), NULL, 16);
  return dma_display->color565((number >> 16) & 0xFF, (number >> 8) & 0xFF, number & 0xFF);
}

/*
 * Function: setTimezone
 * ---------------------
 * Sets the current timezone based on a provided string.
 * It creates a new Timezone instance using pre-defined time change rules.
 */
void setTimezone(String tzName) {
  if (currentTimezone) delete currentTimezone;
  if (tzName == "Eastern") currentTimezone = new Timezone(usEDT, usEST);
  else if (tzName == "Central") currentTimezone = new Timezone(usCDT, usCST);
  else if (tzName == "Mountain") currentTimezone = new Timezone(usMDT, usMST);
  else if (tzName == "Pacific") currentTimezone = new Timezone(usPDT, usPST);
  else if (tzName == "Arizona") currentTimezone = new Timezone(usAZT, usAZT);
  else currentTimezone = new Timezone(usEDT, usEST);
}

/*
 * Function: getTempColor
 * ----------------------
 * Maps a temperature value (in Fahrenheit) to a color.
 * The mapping creates a gradient from blue at 0°F to green at 50°F, then green to red at 100°F.
 */
uint16_t getTempColor(float temp) {
    float t = constrain(temp, 0, 100); // Clamp temperature between 0°F and 100°F
    uint8_t r, g, b;
    if (t <= 50) {
        r = 0;
        g = map(t, 0, 50, 0, 255);
        b = map(t, 0, 50, 255, 0);
    } else {
        r = map(t, 50, 100, 0, 255);
        g = map(t, 50, 100, 255, 0);
        b = 0;
    }
    return dma_display->color565(r, g, b);
}

/*
 * Function: splash
 * ----------------
 * Displays a splash screen on the matrix.
 * It fills the screen with the background color and shows the device's IP address (from WiFi or AP mode).
 */
void splash() {
  dma_display->fillScreen(customBgColor);
  dma_display->setTextColor(myGREEN);
  dma_display->setFont(&Picopixel);
  String ipText = "IP: " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
  int16_t x1, y1; uint16_t w, h;
  dma_display->getTextBounds(ipText, 0, 0, &x1, &y1, &w, &h);
  dma_display->setCursor((displayWidth - w) / 2, (displayHeight - h) / 2 + 3);
  dma_display->print(ipText);
  delay(5000);
}

/*
 * Function: setup
 * ---------------
 * The Arduino setup function.
 * Initializes serial communication, SPI for the SD card, the LED matrix display, and WiFi.
 * It also attempts to initialize the SD card, load preferences, and set up the RTC.
 * If WiFi configuration is missing, it starts in AP mode and serves the configuration page.
 */
void setup() {
    Serial.begin(115200);
    delay(1000);

    // Initialize SPI for SD card
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Initialize I2C for RTC and display
    Wire.begin(21, 22);
    Wire.setClock(50000);
    displaySetup();
    pinMode(BUTTON_PIN, INPUT);

    // Robust SD initialization with retries
    const int maxRetriesSD = 5;
    int retryCountSD = 0;

    sdFailed = true;
    while (retryCountSD < maxRetriesSD && sdFailed) {
        Serial.println("Attempting SD card initialization, try " + String(retryCountSD + 1) + "/" + String(maxRetriesSD) + "...");
        if (SD.begin(SD_CS, SPI, 4000000)) {
            sdFailed = false;
            Serial.println("SD init OK");
            Serial.println("SD card type: " + String(SD.cardType() == CARD_MMC ? "MMC" : SD.cardType() == CARD_SD ? "SD" : "SDHC"));
            Serial.println("SD card size: " + String(SD.cardSize() / (1024 * 1024)) + " MB");
            break;
        } else {
            Serial.println("SD init failed");
            retryCountSD++;
            if (retryCountSD < maxRetriesSD) {
                SPI.end();
                delay(500);
                SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
                pinMode(SD_CS, OUTPUT);
                digitalWrite(SD_CS, HIGH);
            }
        }
    }

    if (sdFailed) {
        Serial.println("SD card initialization failed after " + String(maxRetriesSD) + " attempts! Continuing without SD...");
    }

    // Load WiFi and other preferences
    preferences.begin("config", true);
    String storedSSID = preferences.getString("wifiName", "");
    String storedPassword = preferences.getString("wifiPassword", "");
    String storedCryptoList = preferences.getString("cryptoList", "bitcoin,ethereum");
    weatherZipCode = preferences.getString("weatherZip", "");
    weatherApiKey = preferences.getString("weatherApi", "");
    bool setupComplete = preferences.getBool("setupComplete", false);
    if (storedCryptoList.length() > 0) cryptoList = storedCryptoList;
    preferences.end();

    // If no WiFi config, start in AP mode and serve the configuration page
    if (storedSSID.length() == 0 || !setupComplete) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("MATRIXMATE", "pass1234");
        splash();
        serveConfigPage();
        unsigned long startTime = millis();
        while (millis() - startTime < 60000) {
            preferences.begin("config", true);
            bool configComplete = preferences.getBool("setupComplete", false);
            String newSSID = preferences.getString("wifiName", "");
            preferences.end();
            if (configComplete && newSSID.length() > 0) {
                ESP.restart();
            }
            delay(500);
        }
        Serial.println("AP mode timeout, restarting...");
        ESP.restart();
    } else {
        // Otherwise, connect to WiFi in STA mode
        WiFi.mode(WIFI_STA);
        WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

        // Robust WiFi connection with exponential backoff
        const int maxRetriesWiFi = 10;
        int retryCountWiFi = 0;
        unsigned long retryDelay = 500;

        while (WiFi.status() != WL_CONNECTED && retryCountWiFi < maxRetriesWiFi) {
            Serial.print("Connecting to WiFi, attempt " + String(retryCountWiFi + 1) + "/" + String(maxRetriesWiFi) + "...");
            delay(retryDelay);
            if (WiFi.status() == WL_CONNECTED) break;
            Serial.println(" failed");
            retryCountWiFi++;
            retryDelay = min(retryDelay * 2, 8000UL);
            WiFi.disconnect();
            WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi connected: " + WiFi.localIP().toString());
            splash();
            serveConfigPage();
        } else {
            Serial.println("WiFi connection failed after " + String(maxRetriesWiFi) + " attempts, proceeding offline...");
        }
    }

    // Initialize the RTC and sync time if necessary
    if (!rtc.begin()) {
        Serial.println("RTC initialization failed!");
        rtcFailed = true;
    } else {
        DateTime now = rtc.now();
        if (now.unixtime() < 1700000000) syncTime();
        else setTime(now.unixtime());
    }
    syncTime();
    lastRtcSync = millis();
}

/*
 * Function: loop
 * --------------
 * The main Arduino loop.
 * It periodically:
 *  - Syncs time (every 7 days)
 *  - Switches the display screen based on a timer
 *  - Updates the clock every second (if on clock screen)
 *  - Attempts WiFi reconnection if disconnected
 *  - Calls update functions for crypto and weather data
 *  - Refreshes the current screen if needed
 */
void loop() {
    static unsigned long lastWiFiCheck = 0;
    const unsigned long WIFI_CHECK_INTERVAL = 30000; // 30 seconds

    if (millis() - lastRtcSync >= 604800UL) {
        syncTime();
        lastRtcSync = millis();
    }

    if (millis() - lastScreenSwitch >= SCREEN_DURATION) {
        currentScreen = (currentScreen + 1) % 4;
        lastScreenSwitch = millis();
        clockNeedsUpdate = cryptoNeedsUpdate = weatherNeedsUpdate = animationNeedsUpdate = true;
        if (currentScreen == 3) firstEntry = true;
    }

    if (currentScreen == 0 && millis() - lastClockUpdate >= CLOCK_UPDATE_INTERVAL) {
        clockNeedsUpdate = true;
        lastClockUpdate = millis();
    }

    // Check WiFi connection and attempt to reconnect if needed
    if (millis() - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected, attempting to reconnect...");
            WiFi.disconnect();
            preferences.begin("config", true);
            String storedSSID = preferences.getString("wifiName", "");
            String storedPassword = preferences.getString("wifiPassword", "");
            preferences.end();
            WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

            unsigned long startAttemptTime = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
                delay(500);
                Serial.print(".");
            }
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("WiFi reconnected: " + WiFi.localIP().toString());
                splash();
            } else {
                Serial.println("WiFi reconnection failed");
            }
        }
        lastWiFiCheck = millis();
    }

    updateCryptoPrices();
    updateWeather();
    // Optionally call checkButton() to monitor physical button events
    // checkButton();
    if (clockNeedsUpdate || cryptoNeedsUpdate || weatherNeedsUpdate || animationNeedsUpdate) updateScreen();
    delay(10);
}
