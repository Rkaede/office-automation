#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "config.h"
#include "time.h"

// load TFT libraries
TFT_eSPI tft = TFT_eSPI(135, 240);
TFT_eSprite img = TFT_eSprite(&tft);

// STATE
unsigned long last_motion;  // Keep track of last time motion was detected

// The first timestamp when motion is detected (for the CONFIG_LCD_SLEEP)
unsigned long first_detection_time;

// 0 Initialization
// 1 Motion Detected (lights on)
// 2 Motion Detected
// 3 No Motion
// 4 TFT Screensaver On
int state = 0;

volatile int motion_state;
boolean detect_motion = true;  // Motion is detected

void draw_border(uint32_t color) {
    img.fillRect(0, 0, 240, 4, color);
    img.fillRect(0, 0, 4, 135, color);
    img.fillRect(0, 130, 240, 4, color);
    img.fillRect(235, 0, 4, 135, color);
}

void draw_screen() {
    img.fillRect(0, 0, 240, 135, TFT_BLACK);
    img.setCursor(0, 0, 2);
    img.setTextColor(TFT_WHITE);
    img.setTextSize(2);

    switch (state) {
        case 0:
            break;
        case 1:
            draw_border(TFT_GREEN);
            img.setTextDatum(MC_DATUM);
            img.drawString("Motion Detected", 240 / 2, 135 / 2);
            break;
        case 2:
            struct tm timeinfo;
            if (!getLocalTime(&timeinfo)) {
                Serial.println("Failed to obtain time");
                img.println("Issue trying to get time..");
                break;
            }
            img.setTextColor(TFT_DARKGREY);
            img.setCursor(15, 10);
            img.println(&timeinfo, "%A");
            img.setCursor(15, 40);
            img.println(&timeinfo, "%B %d");
            img.setTextSize(3);
            img.setCursor(15, 75);
            img.println(&timeinfo, "%I:%M %p");
            break;
        case 3:
            draw_border(TFT_YELLOW);
            img.setCursor(15, 10);
            img.println("No Motion");
            img.setCursor(15, 40);
            img.println("Screensaver in:");
            img.setTextSize(3);
            img.setCursor(15, 75);
            img.println((last_motion + CONFIG_SLEEP_DELAY - millis()) / 1000 + 1);
            break;
        case 4:
            draw_border(TFT_RED);
            img.setCursor(15, 10);
            img.println("Screensaver On");
            img.setCursor(15, 40);
            break;
    }

    img.pushSprite(0, 0);
}

// action | 0 lights ON | 1 lights OFF
void rest_api_action(int action) {
    WiFiClient client;
    HTTPClient http;
    int httpResponseCode;
    String url;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi Disconnected");
        tft.fillScreen(TFT_RED);
        img.setTextColor(TFT_BLACK);
        tft.println("ERROR: WiFi Disconnected");
        delay(5000);
        ESP.restart();
    }

    switch (action) {
        case 0:  // lights on
            Serial.println("Sending Lights ON Request");
            url = CONFIG_HOME_ASSISTANT_URL + "/api/webhook/" + CONFIG_HOME_ASSISTANT_HOOK_ON;
            http.begin(client, url);
            http.addHeader("Authorization", "Bearer " + CONFIG_HOME_ASSISTANT_TOKEN);
            http.addHeader("Content-Type", "application/json");
            httpResponseCode = http.POST("{}");
            break;
        case 1:  // lights off
            Serial.println("Sending Lights OFF Request");
            url = CONFIG_HOME_ASSISTANT_URL + "/api/webhook/" + CONFIG_HOME_ASSISTANT_HOOK_OFF;
            http.begin(client, url);
            http.addHeader("Authorization", "Bearer " + CONFIG_HOME_ASSISTANT_TOKEN);
            http.addHeader("Content-Type", "application/json");
            httpResponseCode = http.POST("{}");
            break;
    }

    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    http.end();
}

void IRAM_ATTR motionISR() { motion_state = digitalRead(CONFIG_PIN_MOTION); }

void setup() {
    Serial.begin(115200);
    Serial.println("Start");

    tft.init();
    img.createSprite(240, 135);

    tft.setRotation(1);
    tft.fillScreen(TFT_WHITE);
    tft.setCursor(0, 0, 2);
    tft.setTextSize(1);

    tft.println("Booting...");

    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
    tft.println("Connecting to WiFi");
    int connectTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(200);
        tft.print(".");
        if (millis() - connectTime >= 5000) {  // On the rare occasion it gets stuck trying to
                                               // connect, a reboot fixes it.
            ESP.restart();
        }
    }

    tft.println("IP Address: ");
    IPAddress ip = WiFi.localIP();
    tft.println(ip);

    // init and get the time
    tft.println("Setting up Time Server");
    configTime(CONFIG_GMT_OFFSET_SEC, CONFIG_DAYLIGHT_OFFSET_SEC, CONFIG_NTP_SERVER);

    tft.println("Boot up complete!");

    delay(2000);

    pinMode(CONFIG_PIN_MOTION, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(CONFIG_PIN_MOTION), motionISR, CHANGE);
    motionISR();  // Initialize motionState value
    last_motion = millis();
}

void loop() {
    detect_motion = motion_state == HIGH;

    if (detect_motion) {
        last_motion = millis();
    }

    if (!detect_motion && (millis() - last_motion >= CONFIG_SLEEP_DELAY)) {
        if (state != 4) {
            Serial.println("Turning Screensaver On");
            state = 4;
            rest_api_action(1);
        }
        draw_screen();
    } else {
        if (detect_motion) {
            // Not already in a motion detected state
            if (state != 1 && state != 2) {
                Serial.println("Motion Detected");
                state = 1;
                first_detection_time = millis();
                rest_api_action(0);
            }
            if (state == 1 && (millis() - first_detection_time >= CONFIG_LCD_SLEEP)) {
                state = 2;
            }
            draw_screen();
        } else {
            // No longer detecting motion but it hasn't been long
            // enough to go to TFT Screensaver.
            state = 3;
            draw_screen();
        }
    }
}
