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
#include "OTA_Handler.h"

/***** LED MATRIX 設定 *****/
int GLOBAL_BRIGHTNESS = 20;
uint16_t TEXT_FRAME_DELAY_MS = 70;

/***** ボタン設定 *****/
#ifndef BUTTON_PIN
#define BUTTON_PIN 39
#endif

static OneButton g_btn;
static OneButton g_btnBoot(0, true); // Bootボタン (GPIO 0)
static bool DisplayMode = false;

/***** 受信制御 *****/
String lastRxData = "";
unsigned long lastRxTime = 0;
const unsigned long IGNORE_MS = 4000;
const unsigned long RECEIVE_DISPLAY_HOLD_MS = 5000;
const unsigned long RECEIVE_DISPLAY_GUARD_MS = 4500;

/***** 無線設定 *****/
static const int WIFI_CH = 6;
static const char* JSON_PATH = "/data.json";
static int RSSI_THRESHOLD_DBM = -75;

/***** ランタイム状態 *****/
String myJson;
static size_t currentInboxIndex = 0;  // ★追加: 現在表示中のインデックス

/***** 受信コールバック *****/
static void OnMessageReceived(const uint8_t* data, size_t len) {
  String incoming((const char*)data, len);

    // 直近のデータと同じなら無視（デバウンス）
  if (incoming.equals(lastRxData) && (millis() - lastRxTime < IGNORE_MS)) {
    return;
  }

  lastRxData = incoming;
  lastRxTime = millis();

  saveIncomingJson(data, len);
  
  DisplayManager::Clear(); 

  DisplayManager::BlockFor(RECEIVE_DISPLAY_GUARD_MS);
  Ripple_PlayOnce();

  if (!loadDisplayFromJsonString(incoming)) {
    debugPrintln("JSONパース失敗");
  } else if (!performDisplay(true, RECEIVE_DISPLAY_HOLD_MS, false)) {
    debugPrintln("表示失敗");
  } else {
    debugPrintln("受信データを表示中");
  }
  debugPrintln(incoming);
}

/***** setup *****/
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n=== ESP-NOW JSON Broadcast ===");

  DisplayManager::Init(GLOBAL_BRIGHTNESS);
  DisplayManager::TextInit();
  Ripple_PlayOnce();

  // ボタン初期化
  g_btn.setup(BUTTON_PIN, INPUT_PULLUP, true);
  g_btn.setClickMs(300);

  // Bootボタン: クリックでデバッグモードへ移行
  g_btnBoot.attachClick([]() {
    debugPrintln("[BOOT] Starting Debug Mode...");
    startDebugMode();
  });

  // ダブルクリック: 表示モード切替
  g_btn.attachDoubleClick([]() {
    DisplayMode = !DisplayMode;
    DiagonalWave_PlayOnce();
    debugPrintf("[MODE] 受信データ表示モード: %s\n", DisplayMode ? "ON" : "OFF");

    if (DisplayMode) {
      size_t n = inboxSize();
      if (n > 0) {
        // currentInboxIndex = (currentInboxIndex + 1) % n;
        currentInboxIndex = 0; // ★変更: 古い順（最初から）
        
        InboxItem item;
        if (inboxGet(currentInboxIndex, item)) {
          debugPrintln("[DEBUG] Inbox item found. Loading JSON...");
          if (loadDisplayFromJsonString(item.json)) {
            debugPrintf("[INBOX] 表示中: %d / %d\n", currentInboxIndex + 1, n);
            debugPrintln("[DEBUG] Calling performDisplay(true, ULONG_MAX, true)...");
            bool res = performDisplay(true, ULONG_MAX, true);  // アニメON, 無期限, ループON
            debugPrintf("[DEBUG] performDisplay result: %s\n", res ? "TRUE" : "FALSE");
          } else {
            debugPrintln("[ERROR] loadDisplayFromJsonString failed!");
          }
        } else {
          debugPrintln("[ERROR] inboxGet failed!");
        }
      } else {
        debugPrintln("[INBOX] データなし");
      }
    } else {
      DisplayManager::Clear();
      if (!myJson.isEmpty()) {
        loadDisplayFromJsonString(myJson);
        performDisplay();
      }
    }
  });

  // シングルクリック: 次のデータを表示（ループ）
  g_btn.attachClick([]() {
    if (!DisplayMode) return;

    size_t n = inboxSize();
    if (n == 0) {
      debugPrintln("[INBOX] データなし");
      return;
    }

    // 次のインデックスへ（最後まで行ったら最初に戻る）
    currentInboxIndex = (currentInboxIndex + 1) % n;

    InboxItem item;
    if (inboxGet(currentInboxIndex, item)) {
      if (loadDisplayFromJsonString(item.json)) {
        debugPrintf("[INBOX] 表示中: %d / %d\n", currentInboxIndex + 1, n);
        performDisplay(true, ULONG_MAX, true);  // アニメON, 無期限, ループON
      }
    }
  });

  // 保存されたJSONを読み込んで表示
  myJson = loadJsonFromPath(JSON_PATH, 2048);
  debugPrintf("生データ:\n%s\n", myJson.c_str());
  debugPrintf("%s (%uB)\n", JSON_PATH, (unsigned)myJson.length());
  if (!myJson.isEmpty()) {
    loadDisplayFromLittleFS();
    performDisplay();
  }

  // ESP-NOW初期化
  Comm_SetOnMessage(OnMessageReceived);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
  debugPrintf("強制的に CH %d を使用\n", WIFI_CH);
  Comm_Init(WIFI_CH);

  Comm_SetMinRssiToAccept(RSSI_THRESHOLD_DBM);

  BLE_Init();
}

/***** loop *****/
void loop() {
  // デバッグモード中はOTA/Telnet処理のみ
  if (isDebugMode()) {
    handleDebugMode();
    return;
  }

  // ========== 通常モードの処理 ==========
  g_btn.tick();
  g_btnBoot.tick();

  static unsigned long nextSend = 0;
  unsigned long now = millis();

  // 受信体制の定期チェック (5秒ごと)
  static unsigned long lastStatusCheck = 0;
  if (now - lastStatusCheck > 5000) {
    lastStatusCheck = now;

    uint8_t pCh;
    wifi_second_chan_t sCh;
    esp_wifi_get_channel(&pCh, &sCh);

    debugPrintln("--- [RX STATUS CHECK] ---");
    debugPrintf("Time: %lu ms\n", now);
    debugPrintf("WiFi Channel: %d (Target: %d)\n", pCh, WIFI_CH);
    debugPrintf("RSSI Threshold: %d dBm\n", RSSI_THRESHOLD_DBM);
    debugPrintln("State: Listening for ESP-NOW packets...");

    if (pCh != WIFI_CH) {
      debugPrintln("[WARN] Channel drifted! Resetting...");
      esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
    }
    debugPrintln("-------------------------");
  }

  // 表示期限切れ時に自分のデータを再表示
  if (DisplayManager::EndIfExpired()) {
    if (!myJson.isEmpty() && !DisplayMode) {
      loadDisplayFromJsonString(myJson);
      performDisplay();
    }
  }

  if (DisplayManager::TextScroll_IsActive()) {
    DisplayManager::TextScroll_Update();
    
    // ★追加: スクロールが終わった瞬間に自分の表示に戻す
    if (!DisplayManager::TextScroll_IsActive()) {
      if (!DisplayMode && !myJson.isEmpty()) {
        loadDisplayFromJsonString(myJson);
        performDisplay();
        debugPrintln("[INFO] Text scroll finished, reverting to myJson");
      }
    }
  }
  delay(16);

  BLE_Tick();

  // 定期ブロードキャスト
  if (!myJson.isEmpty() && now >= nextSend) {
    Comm_SendJsonBroadcast(myJson);
    nextSend = now + 1000 + (esp_random() % 500);
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
        debugPrintln("Saved JSON to /data.json and displayed it");
      }
    }
  }
}

