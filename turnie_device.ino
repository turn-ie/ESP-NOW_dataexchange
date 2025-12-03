#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>

#include <LittleFS.h>
#include <FS.h>

#include <ArduinoJson.h>
#include <OneButton.h>

#include "Motion.h"
#include "Display_Manager.h"
#include "Json_Handler.h"
#include "BLE_Manager.h"
#include "Comm_EspNow.h"
// #include "OTA_Handler.h" // OTA無効化

/***** LED MATRIX 設定 *****/
int GLOBAL_BRIGHTNESS = 20;
uint16_t TEXT_FRAME_DELAY_MS = 60;

/***** ボタン設定 *****/
#ifndef BUTTON_PIN
#define BUTTON_PIN 39
#endif

static OneButton g_btn;
static bool DisplayMode = false;

/***** 受信制御 *****/
String lastRxData = "";
unsigned long lastRxTime = 0;
const unsigned long IGNORE_MS = 10000;
const unsigned long RECEIVE_DISPLAY_HOLD_MS = 1600;
const unsigned long RECEIVE_DISPLAY_GUARD_MS = 4500;

/***** 無線設定 *****/
static const int WIFI_CH = 6;
static const char* JSON_PATH = "/data.json";
static int RSSI_THRESHOLD_DBM = -90;

/***** ランタイム状態 *****/
String myJson;

/***** 受信コールバック *****/
static void OnMessageReceived(const uint8_t* data, size_t len) {
  String incoming((const char*)data, len);

  if (incoming.equals(lastRxData) && (millis() - lastRxTime < IGNORE_MS)) {
    return;
  }

  lastRxData = incoming;
  lastRxTime = millis();

  saveIncomingJson(data, len);
  DisplayManager::BlockFor(RECEIVE_DISPLAY_GUARD_MS);
  Ripple_PlayOnce();

  if (!loadDisplayFromJsonString(incoming)) {
    Serial.println("JSONパース失敗");
  } else if (!performDisplay(true, RECEIVE_DISPLAY_HOLD_MS, false)) {
    Serial.println("表示失敗");
  } else {
    Serial.println("受信データを表示中");
  }
  Serial.println(incoming);
}

/***** setup *****/
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP-NOW JSON Broadcast ===");

  // setupOTA(); // OTA無効化

  DisplayManager::Init(GLOBAL_BRIGHTNESS);
  DisplayManager::TextInit();
  Ripple_PlayOnce();

  // ボタン初期化
  g_btn.setup(BUTTON_PIN, INPUT_PULLUP, true);
  g_btn.setClickMs(300);

  // ダブルクリック: 表示モード切替
  g_btn.attachDoubleClick([]() {
    DisplayMode = !DisplayMode;
    DiagonalWave_PlayOnce();
    Serial.printf("[MODE] 受信データ表示モード: %s\n", DisplayMode ? "ON" : "OFF");

    if (DisplayMode) {
      size_t n = inboxSize();
      if (n > 0) {
        InboxItem item;
        if (inboxGet(n - 1, item)) {
          if (loadDisplayFromJsonString(item.json)) {
            performDisplay(false, 3000, false);
          }
        }
      } else {
        Serial.println("[INBOX] データなし");
      }
    } else {
      DisplayManager::Clear();
      if (!myJson.isEmpty()) {
        loadDisplayFromJsonString(myJson);
        performDisplay();
      }
    }
  });

  // シングルクリック: 2番目のデータ表示
  g_btn.attachClick([]() {
    if (!DisplayMode) return;

    size_t n = inboxSize();
    if (n >= 2) {
      InboxItem item;
      if (inboxGet(n - 2, item)) {
        if (loadDisplayFromJsonString(item.json)) {
          performDisplay(false, 3000, false);
        }
      }
    } else {
      Serial.println("[INBOX] 2番目のデータなし");
    }
  });

  // 保存されたJSONを読み込んで表示
  myJson = loadJsonFromPath(JSON_PATH, 2048);
  Serial.printf("生データ:\n%s\n", myJson.c_str());
  Serial.printf("%s (%uB)\n", JSON_PATH, (unsigned)myJson.length());
  if (!myJson.isEmpty()) {
    loadDisplayFromLittleFS();
    performDisplay();
  }

  // ESP-NOW初期化
  Comm_SetOnMessage(OnMessageReceived);

  // WiFiチャンネルを強制的に固定
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
  Serial.printf("強制的に CH %d を使用\n", WIFI_CH);
  Comm_Init(WIFI_CH);

  /* 以前の自動判定ロジックを無効化
  int currentChannel = WiFi.channel();
  if (currentChannel > 0) {
    Serial.printf("WiFi CH %d を使用\n", currentChannel);
    Comm_Init(currentChannel);
  } else {
    Serial.printf("デフォルト CH %d を使用\n", WIFI_CH);
    Comm_Init(WIFI_CH);
  }
  */
  Comm_SetMinRssiToAccept(RSSI_THRESHOLD_DBM);

  BLE_Init();
}

/***** loop *****/
void loop() {
  // handleOTA(); // OTA無効化

  static unsigned long nextSend = 0;
  unsigned long now = millis();

  // ★追加: 受信体制の定期チェックとログ出力 (5秒ごと)
  static unsigned long lastStatusCheck = 0;
  if (now - lastStatusCheck > 5000) {
    lastStatusCheck = now;
    
    uint8_t pCh;
    wifi_second_chan_t sCh;
    esp_wifi_get_channel(&pCh, &sCh);
    
    Serial.println("--- [RX STATUS CHECK] ---");
    Serial.printf("Time: %lu ms\n", now);
    Serial.printf("WiFi Channel: %d (Target: %d)\n", pCh, WIFI_CH);
    Serial.printf("RSSI Threshold: %d dBm\n", RSSI_THRESHOLD_DBM);
    Serial.println("State: Listening for ESP-NOW packets...");
    
    // チャンネルずれの自動修正
    if (pCh != WIFI_CH) {
        Serial.println("[WARN] Channel drifted! Resetting...");
        esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
    }
    Serial.println("-------------------------");
  }

  // 表示期限切れ時に自分のデータを再表示
  if (DisplayManager::EndIfExpired()) {
    if (!myJson.isEmpty() && !DisplayMode) {
      loadDisplayFromJsonString(myJson);
      performDisplay();
    }
  }

  g_btn.tick();

  if (DisplayManager::TextScroll_IsActive()) {
    DisplayManager::TextScroll_Update();
  }
  delay(16);

  BLE_Tick();

  // 定期ブロードキャスト
  if (!myJson.isEmpty() && now >= nextSend) {
    Comm_SendJsonBroadcast(myJson);
    // 送信間隔をランダム化 (2000ms 〜 3000ms)
    // これにより、デバイス間の送信タイミングの同期（衝突）を防ぐ
    nextSend = now + 2000 + (esp_random() % 1000); 
  }

  // シリアル経由でJSON保存
  if (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("save:")) {
      String js = line.substring(5);
      if (!js.isEmpty()) {
        saveJsonToPath("/data.json", js);
        myJson = js;
        loadDisplayFromJsonString(myJson);
        performDisplay();
        Serial.println("Saved JSON to /data.json and displayed it");
      }
    }
  }
}
