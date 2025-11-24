#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>

#include <LittleFS.h>
#include <FS.h>

#include <ArduinoJson.h>
#include <OneButton.h>

#include "Motion.h"           // Radar/Ripple 用
#include "Display_Manager.h"  // LED表示統合モジュール
#include "Json_Handler.h"     // JSON読み込み・保存管理
#include "BLE_Manager.h"      // BLEを使ったJSON受信
#include "Comm_EspNow.h"      // 通信シーケンス

/***** ========== LED MATRIX ========== *****/
int GLOBAL_BRIGHTNESS = 20;
uint16_t TEXT_FRAME_DELAY_MS = 60;  // スクロール速度(1ステップの遅延)

/***** ========== ボタン ========== *****/
#ifndef BUTTON_PIN
#define BUTTON_PIN 39
#endif


static OneButton g_btn;            
static bool DisplayMode = false;   

/***** ========== 無線・ファイル設定 ========== *****/
static const int WIFI_CH = 6;
static const char* JSON_PATH = "/data.json";

static int RSSI_THRESHOLD_DBM = -20; 

/***** ========== ランタイム状態 ========== *****/
String myJson;

/***** ========== 受信フロー（保存→表示） ========== *****/
static void OnMessageReceived(const uint8_t* data, size_t len) {
  saveIncomingJson(data, len);  
  DisplayManager::BlockFor(1600);
  Ripple_PlayOnce();

  String js((const char*)data, len);  
  if (!loadDisplayFromJsonString(js)) {
    Serial.println("[PARSE] 受信JSON解析失敗");
  } else if (!performDisplay()) {
    Serial.println("[DISPLAY] 表示できるデータがありません");
  }
  Serial.println(js);  
}

/***** ========== Arduino 標準 ========== *****/
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP-NOW JSON Broadcast ===");

  DisplayManager::Init(GLOBAL_BRIGHTNESS);
  DisplayManager::TextInit();
  Ripple_PlayOnce();

/*** ========== ボタン ========== *****/
  g_btn.setup(BUTTON_PIN, INPUT_PULLUP, true);  // 
  g_btn.setClickMs(300);                        // 
  g_btn.attachDoubleClick([]() {
    DisplayMode = !DisplayMode;

    DiagonalWave_PlayOnce();
    
    Serial.printf("[MODE] 受信データ表示モード: %s\n", DisplayMode ? "ON" : "OFF");

    if (DisplayMode) {
      // 表示モード再生 (Play latest)
      size_t n = inboxSize();
      if (n > 0) {
        InboxItem item;
        if (inboxGet(n - 1, item)) {
           if (loadDisplayFromJsonString(item.json)) {
             performDisplay();
           }
        }
      } else {
        Serial.println("[INBOX] データなし");
      }
    } else {
      // 表示モード終了 (End)
      DisplayManager::Clear();
      Radar_InitIdle();
    }
  });

  g_btn.attachClick([]() {
    if (!DisplayMode) return;
    
    // 受信データの二番目へ (To the 2nd item)
    size_t n = inboxSize();
    if (n >= 2) {
      InboxItem item;
      if (inboxGet(n - 2, item)) {
         if (loadDisplayFromJsonString(item.json)) {
           performDisplay();
         }
      }
    } else {
      Serial.println("[INBOX] 2番目のデータなし");
    }
  });

  myJson = loadJsonFromPath(JSON_PATH, 2048);
  Serial.printf("📄 生データ:\n%s\n", myJson.c_str());
  Serial.printf("📄 %s (%uB)\n", JSON_PATH, (unsigned)myJson.length());
  if (!myJson.isEmpty()) {
    loadDisplayFromLittleFS();
    performDisplay();
  }

  Comm_SetOnMessage(OnMessageReceived);
  Comm_Init(WIFI_CH);
  Comm_SetMinRssiToAccept(RSSI_THRESHOLD_DBM);

  if (!DisplayManager::IsActive()) {
    Radar_InitIdle();
  } else {
    Serial.println("🔍 起動時に表示中のため、レーダーは有効期限後に開始");
  }

  BLE_Init();
}

void loop() {
  static unsigned long nextSend = 0;
  unsigned long now = millis();

  if (DisplayManager::EndIfExpired()) {
    Radar_InitIdle();
  }

  g_btn.tick();

  if (!DisplayManager::IsActive() && !DisplayMode) {
    Radar_IdleStep(true);
  }
  delay(16);

  BLE_Tick();

  if (!myJson.isEmpty() && now >= nextSend) {
    Comm_SendJsonBroadcast(myJson);
    nextSend = now + 100 + (esp_random() % 50) - 25;  
    
  }
  if (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("save:")) {
      String js = line.substring(5);
      if (!js.isEmpty()) {
        saveJsonToPath("/mydata.json", js);
        saveJsonToPath("/data.json", js);
        loadDisplayFromLittleFS();
        performDisplay();
        Serial.println("Saved JSON to /mydata.json and /data.json and displayed it");
      }
    }
  }
}
