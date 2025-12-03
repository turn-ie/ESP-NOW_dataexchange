#include <WiFi.h>
#include <ArduinoOTA.h>
#include "OTA_Handler.h"

const char* ssid = "IA4-411";
const char* password = "gEdCx5Rdm9J9WNAJ7xN7";

static bool otaReady = false;
static unsigned long lastRetryTime = 0;
const unsigned long RETRY_INTERVAL_MS = 60000; // 1分ごとに再試行

// WiFi接続を試みる（最大1秒間）
static bool tryConnect() {
  if (WiFi.status() == WL_CONNECTED) return true;
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(50);
    retry++;
  }
  
  return (WiFi.status() == WL_CONNECTED);
}

void setupOTA() {
  if (tryConnect()) {
    ArduinoOTA.setPassword("0000");
    ArduinoOTA.setHostname("WifiOTA_NWstudio");
    ArduinoOTA.begin();
    otaReady = true;
    Serial.println("✅ OTA Ready");
  } else {
    Serial.println("⚠️ WiFi failed, will retry in 1 min");
    otaReady = false;
  }
  lastRetryTime = millis();
}

void handleOTA() {
  if (otaReady) {
    ArduinoOTA.handle();
  } else {
    // 1分ごとに再接続を試みる
    if (millis() - lastRetryTime >= RETRY_INTERVAL_MS) {
      lastRetryTime = millis();
      Serial.println("🔄 Retrying WiFi connection...");
      
      if (tryConnect()) {
        ArduinoOTA.setPassword("0000");
        ArduinoOTA.setHostname("WifiOTA_NWstudio");
        ArduinoOTA.begin();
        otaReady = true;
        Serial.println("✅ OTA Ready (reconnected)");
      } else {
        Serial.println("⚠️ WiFi still failed, will retry in 1 min");
      }
    }
  }
}
