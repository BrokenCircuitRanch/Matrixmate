/*******************************************************************
*                                                                  *
*        Matrixmate Audio Spectrum Analyzer                        *                                                        
*                                                           
*        Reset pin re-assigned to mode pin
*        PINS 34 and 35 Set for Audio input though simple resistor 
*        network for Line input
*       Audio Input ---- C1 ---- R3 ---- ADC Pin (34 or 35)
*                      |
*                      C2
*                      |
*                     GND
*                      |
*                      R1
*                      |
*                     3.3V
*                      |
*                     R2
*                      |
*                     GND
*    
*        C1: 1µF (DC blocking)
*        R3: 10kΩ (series resistor)
*        C2: 1nF (low-pass filter)
*        R1, R2: 10kΩ each (bias to 1.65V)
*        You can fildle with cap values to change cutoff frequencies of filter.
*
*        Code Written By Kent Andersen / Broken Circuit Ranch                                                         
*        Copyright 2025. Released under MIT Licence. 
*        
*                                                                  
*                                                                  
 *******************************************************************/



#include <Arduino.h>
#include <arduinoFFT.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <driver/adc.h>

#define MATRIX_WIDTH 128
#define MATRIX_HEIGHT 32 // Physical display height
#define VIRTUAL_MATRIX_HEIGHT 40 // Virtual height for calculations
#define PANEL_WIDTH 64
#define VU_WIDTH 4
#define BINS_PER_CHANNEL 64
#define BAR_WIDTH 2
#define SAMPLE_RATE 32000
#define FFT_SIZE 256
float MIC_GAIN = 12.0; // Dynamic as in original
#define ADC_PIN_LEFT 34
#define ADC_PIN_RIGHT 35
#define BUTTON_PIN 33
#define ADC_VREF 3.3
#define ADC_BIAS (4095.0 / 2.0)

ArduinoFFT<float> FFT_L, FFT_R;
float vRealL[FFT_SIZE], vImagL[FFT_SIZE], vRealR[FFT_SIZE], vImagR[FFT_SIZE];
float noise_floorL[FFT_SIZE / 2] = {0.02}, noise_floorR[FFT_SIZE / 2] = {0.02};
uint16_t waterfallBuffer[MATRIX_HEIGHT][BINS_PER_CHANNEL * 2] = {0};

HUB75_I2S_CFG::i2s_pins _pins = {25, 26, 27, 14, 12, 13, 19, 23, 18, 5, 15, 16, 4};
HUB75_I2S_CFG mxconfig(64, 32, 2);
MatrixPanel_I2S_DMA matrix(mxconfig);

enum DisplayMode { OPTION1, OPTION2, OPTION3, OPTION4, OPTION5, OPTION6, OPTION7 };
DisplayMode currentMode = OPTION1;
int buttonState = HIGH, lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 500;

const float freqBins[] = {
  10, 20, 40, 80, 120, 160, 200, 250, 300, 350, 400, 450, 500, 550, 600, 650,
  700, 750, 800, 850, 900, 950, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700,
  1800, 1900, 2000, 2200, 2400, 2600, 2800, 3000, 3200, 3400, 3600, 3800, 4000,
  4200, 4400, 4600, 4800, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000,
  9500, 10000, 11000, 12000, 13000, 14000, 15000, 16000
};

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setup() {
  Serial.begin(115200);
  mxconfig.double_buff = true;
  if (!matrix.begin()) {
    Serial.println("Matrix DMA init failed!");
    while (1);
  }
  matrix.setBrightness8(80);
  matrix.clearScreen();

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);

  pinMode(BUTTON_PIN, INPUT);
}

uint16_t hsvToRgb(float h, float s, float v) {
  h = fmod(h, 360.0);
  int i = (int)(h / 60.0);
  float f = h / 60.0 - i;
  float p = v * (1 - s);
  float q = v * (1 - s * f);
  float t = v * (1 - s * (1 - f));
  float r, g, b;
  switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return matrix.color444((int)(r * 15), (int)(g * 15), (int)(b * 15));
}

uint16_t getColor(int height, int x, bool isPlasma) {
  if (isPlasma) {
    float h = fmod((float)(height * 10 + x * 5 + millis() / 100.0), 360.0);
    return hsvToRgb(h, 1.0, 0.8);
  } else {
    float h = map(height, 0, MATRIX_HEIGHT, 240.0, 0.0);
    return hsvToRgb(h, 1.0, height > 0.9 * MATRIX_HEIGHT ? 1.0 : 0.8);
  }
}

void readAdcData(float *vRealL, float *vImagL, float *vRealR, float *vImagR) {
  static uint32_t lastSampleTime = 0;
  uint32_t sampleInterval = 1000000 / SAMPLE_RATE;
  for (int i = 0; i < FFT_SIZE; i++) {
    while (micros() - lastSampleTime < sampleInterval) {}
    lastSampleTime = micros();
    uint16_t adcLeft = adc1_get_raw(ADC1_CHANNEL_6);
    uint16_t adcRight = adc1_get_raw(ADC1_CHANNEL_7);
    float voltageL = (adcLeft / 4095.0) * ADC_VREF;
    float voltageR = (adcRight / 4095.0) * ADC_VREF;
    vRealL[i] = (voltageL - ADC_VREF / 2.0) * MIC_GAIN;
    vRealR[i] = (voltageR - ADC_VREF / 2.0) * MIC_GAIN;
    vImagL[i] = vImagR[i] = 0;
    if (i == 0) {
      Serial.printf("ADC Left: %d, Right: %d, vRealL[0]: %.4f, vRealR[0]: %.4f\n",
                    adcLeft, adcRight, vRealL[0], vRealR[0]);
    }
  }
  FFT_L.windowing(vRealL, FFT_SIZE, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT_L.compute(vRealL, vImagL, FFT_SIZE, FFT_FORWARD);
  FFT_L.complexToMagnitude(vRealL, vImagL, FFT_SIZE);
  FFT_R.windowing(vRealR, FFT_SIZE, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT_R.compute(vRealR, vImagR, FFT_SIZE, FFT_FORWARD);
  FFT_R.complexToMagnitude(vRealR, vImagR, FFT_SIZE);
  for (int i = 0; i < FFT_SIZE / 2; i++) {
    noise_floorL[i] = 0.91f * noise_floorL[i] + 0.1f * vRealL[i];
    noise_floorR[i] = 0.91f * noise_floorR[i] + 0.1f * vRealR[i];
    if (i == 0) {
      Serial.printf("Noise Floor Left[0]: %.4f, Right[0]: %.4f\n", noise_floorL[0], noise_floorR[0]);
    }
  }
}

void handleButton() {
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) {
      currentMode = (DisplayMode)((currentMode + 1) % 7);
      matrix.clearScreen();
    }
  }
  lastButtonState = reading;
}

void loop() {
  static float peakHeights[MATRIX_WIDTH] = {0};
  static float avgHeights[MATRIX_WIDTH] = {0};
  static float peakHold[MATRIX_WIDTH] = {0};
  static unsigned long peakTimes[MATRIX_WIDTH] = {0};
  static float vuLevelL = 0, vuLevelR = 0;
  int xStart, xEnd;
  handleButton();
  readAdcData(vRealL, vImagL, vRealR, vImagR);
  matrix.fillScreen(0);

  float maxMagL = 0, maxMagR = 0;
  for (int i = 0; i < FFT_SIZE / 2; i++) {
    maxMagL = max(maxMagL, vRealL[i]);
    maxMagR = max(maxMagR, vRealR[i]);
  }
  float maxMag = max(maxMagL, maxMagR);
  if (maxMag > 0.001f) {
    MIC_GAIN = constrain(MIC_GAIN * (0.5f / maxMag), 0.1f, 15.0f);
  } else {
    MIC_GAIN = 12.0f;
  }

  float vuL = sqrt(maxMagL), vuR = sqrt(maxMagR);
  vuLevelL = 0.8f * vuLevelL + 0.2f * vuL;
  vuLevelR = 0.8f * vuLevelR + 0.2f * vuR;
  int vuHeightL = (int)(vuLevelL * MATRIX_HEIGHT / 5);
  int vuHeightR = (int)(vuLevelR * MATRIX_HEIGHT / 5);
  vuHeightL = constrain(vuHeightL, 0, MATRIX_HEIGHT - 1);
  vuHeightR = constrain(vuHeightR, 0, MATRIX_HEIGHT - 1);

  if (currentMode == OPTION6) {
    memmove(waterfallBuffer[1], waterfallBuffer[0], sizeof(uint16_t) * (BINS_PER_CHANNEL * 2) * (MATRIX_HEIGHT - 1));
    memset(waterfallBuffer[0], 0, sizeof(uint16_t) * (BINS_PER_CHANNEL * 2));
  }

  // Left channel
  xStart = (currentMode == OPTION5) ? VU_WIDTH : 0;
  xEnd = (currentMode == OPTION5) ? PANEL_WIDTH - VU_WIDTH : PANEL_WIDTH;
  for (int x = xStart; x < xEnd; x++) {
    int binIdx = map(x, xStart, xEnd - 1, 0, BINS_PER_CHANNEL - 1);
    float freqStart = binIdx > 0 ? freqBins[binIdx - 1] : 0;
    float freqEnd = freqBins[binIdx];
    int binStart = freqStart * FFT_SIZE / SAMPLE_RATE;
    int binEnd = freqEnd * FFT_SIZE / SAMPLE_RATE;
    if (binEnd == binStart) binEnd = binStart + 1;

    float sum = 0;
    for (int i = binStart; i < binEnd; i++) {
      float magnitude = vRealL[i] - noise_floorL[i];
      if (magnitude < 0) magnitude = 0;
      sum += magnitude;
    }

    float avg = sum / (binEnd - binStart);
    avg = sqrt(avg);
    float virtualHeight = avg * 50 * (VIRTUAL_MATRIX_HEIGHT - 1) / (MATRIX_HEIGHT - 1); // Scale for 0–47
    int height = constrain((int)virtualHeight, 0, MATRIX_HEIGHT - 1); // Clip to 0–31

    if (x == 0) {
      Serial.printf("Left Avg: %.4f, Virtual Height: %.1f, Display Height: %d\n", avg, virtualHeight, height);
    }

    float decay = (currentMode == OPTION1 || currentMode == OPTION3 || currentMode == OPTION5 || currentMode == OPTION6 || currentMode == OPTION7) ? 0.9f : 0.99f;
    peakHeights[x] = max(peakHeights[x] * decay, (float)height);
    avgHeights[x] = 0.7f * avgHeights[x] + 0.3f * peakHeights[x];

    if (currentMode == OPTION2 || currentMode == OPTION4) {
      if (height > peakHold[x]) {
        peakHold[x] = height;
        peakTimes[x] = millis();
      } else if (millis() - peakTimes[x] > 500) {
        peakHold[x] = max(peakHold[x] * 0.9f, (float)height);
      }
    } else {
      peakHold[x] = height;
    }

    if (avgHeights[x] > 0) {
      bool isPlasma = (currentMode == OPTION3 || currentMode == OPTION4);
      uint16_t color = (virtualHeight > 32) ? matrix.color444(15, 0, 0) : getColor((int)avgHeights[x], x, isPlasma); // Red if virtual > 32
      if (currentMode == OPTION6) {
        waterfallBuffer[0][binIdx] = color;
      } else if (currentMode == OPTION7) {
        int h = (int)avgHeights[x];
        int mid = MATRIX_HEIGHT / 2;
        for (int y = mid - h / 2; y <= mid + h / 2; y++) {
          if (y >= 0 && y < MATRIX_HEIGHT) {
            matrix.drawPixel(x, y, color);
          }
        }
      } else {
        matrix.drawLine(x, MATRIX_HEIGHT - 1, x, MATRIX_HEIGHT - (int)avgHeights[x], color);
        if ((currentMode == OPTION2 || currentMode == OPTION4) && peakHold[x] > avgHeights[x]) {
          matrix.drawPixel(x, MATRIX_HEIGHT - (int)peakHold[x], matrix.color444(15, 15, 15));
        }
      }
    }
  }

  // Right channel
  xStart = (currentMode == OPTION5) ? PANEL_WIDTH + VU_WIDTH : PANEL_WIDTH;
  xEnd = (currentMode == OPTION5) ? MATRIX_WIDTH - VU_WIDTH : MATRIX_WIDTH;
  for (int x = xStart; x < xEnd; x++) {
    int binIdx = map(x, xStart, xEnd - 1, 0, BINS_PER_CHANNEL - 1);
    float freqStart = binIdx > 0 ? freqBins[binIdx - 1] : 0;
    float freqEnd = freqBins[binIdx];
    int binStart = freqStart * FFT_SIZE / SAMPLE_RATE;
    int binEnd = freqEnd * FFT_SIZE / SAMPLE_RATE;
    if (binEnd == binStart) binEnd = binStart + 1;

    float sum = 0;
    for (int i = binStart; i < binEnd; i++) {
      float magnitude = vRealR[i] - noise_floorR[i];
      if (magnitude < 0) magnitude = 0;
      sum += magnitude;
    }

    float avg = sum / (binEnd - binStart);
    avg = sqrt(avg);
    float virtualHeight = avg * 50 * (VIRTUAL_MATRIX_HEIGHT - 1) / (MATRIX_HEIGHT - 1); // Scale for 0–47
    int height = constrain((int)virtualHeight, 0, MATRIX_HEIGHT - 1); // Clip to 0–31

    if (x == PANEL_WIDTH) {
      Serial.printf("Right Avg: %.4f, Virtual Height: %.1f, Display Height: %d\n", avg, virtualHeight, height);
    }

    float decay = (currentMode == OPTION1 || currentMode == OPTION3 || currentMode == OPTION5 || currentMode == OPTION6 || currentMode == OPTION7) ? 0.9f : 0.99f;
    peakHeights[x] = max(peakHeights[x] * decay, (float)height);
    avgHeights[x] = 0.7f * avgHeights[x] + 0.3f * peakHeights[x];

    if (currentMode == OPTION2 || currentMode == OPTION4) {
      if (height > peakHold[x]) {
        peakHold[x] = height;
        peakTimes[x] = millis();
      } else if (millis() - peakTimes[x] > 500) {
        peakHold[x] = max(peakHold[x] * 0.9f, (float)height);
      }
    } else {
      peakHold[x] = height;
    }

    if (avgHeights[x] > 0) {
      bool isPlasma = (currentMode == OPTION3 || currentMode == OPTION4);
      uint16_t color = (virtualHeight > 32) ? matrix.color444(15, 0, 0) : getColor((int)avgHeights[x], x, isPlasma); // Red if virtual > 32
      if (currentMode == OPTION6) {
        waterfallBuffer[0][binIdx + BINS_PER_CHANNEL] = color;
      } else if (currentMode == OPTION7) {
        int h = (int)avgHeights[x];
        int mid = MATRIX_HEIGHT / 2;
        for (int y = mid - h / 2; y <= mid + h / 2; y++) {
          if (y >= 0 && y < MATRIX_HEIGHT) {
            matrix.drawPixel(x, y, color);
          }
        }
      } else {
        matrix.drawLine(x, MATRIX_HEIGHT - 1, x, MATRIX_HEIGHT - (int)avgHeights[x], color);
        if ((currentMode == OPTION2 || currentMode == OPTION4) && peakHold[x] > avgHeights[x]) {
          matrix.drawPixel(x, MATRIX_HEIGHT - (int)peakHold[x], matrix.color444(15, 15, 15));
        }
      }
    }
  }

  if (currentMode == OPTION5) {
    for (int x = 0; x < VU_WIDTH; x++) {
      matrix.drawLine(x, MATRIX_HEIGHT - 1, x, MATRIX_HEIGHT - vuHeightL, getColor(vuHeightL, x, false));
    }
    for (int x = MATRIX_WIDTH - VU_WIDTH; x < MATRIX_WIDTH; x++) {
      matrix.drawLine(x, MATRIX_HEIGHT - 1, x, MATRIX_HEIGHT - vuHeightR, getColor(vuHeightR, x, false));
    }
  } else if (currentMode == OPTION6) {
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
      for (int binIdx = 0; binIdx < BINS_PER_CHANNEL * 2; binIdx++) {
        if (waterfallBuffer[y][binIdx]) {
          int xPos = (binIdx < BINS_PER_CHANNEL) ? binIdx * BAR_WIDTH : PANEL_WIDTH + (binIdx - BINS_PER_CHANNEL) * BAR_WIDTH;
          matrix.fillRect(xPos, y, BAR_WIDTH, 1, waterfallBuffer[y][binIdx]);
        }
      }
    }
  }

  matrix.flipDMABuffer();
}
