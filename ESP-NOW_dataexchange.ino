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
#include "Comm_EspNow.h"      // 通信シーケンス

/***** ========== LED MATRIX ========== *****/
#define GLOBAL_BRIGHTNESS 10

// テキストスクロール設定
uint16_t TEXT_FRAME_DELAY_MS = 60;  // スクロール速度(1ステップの遅延)
uint8_t TEXT_BRIGHTNESS = 20;       // テキスト時の明るさ

/***** ========== ボタン ========== *****/
// ダブルクリックで「受信データ表示モード」をトグル
// 必要に応じて環境に合わせて変更してください
#ifndef BUTTON_PIN
#define BUTTON_PIN 39
#endif

// OneButtonでクリック/ダブルクリックを扱う
static OneButton g_btn;           // 後でsetup()で初期化
static bool DisplayMode = false;  // 受信データ表示モード（ダブルクリックでトグル）

/***** ========== 無線・ファイル設定 ========== *****/
static const int WIFI_CH = 6;
static const char* JSON_PATH = "/data.json";

static int RSSI_THRESHOLD_DBM = -40;  // 必要に応じて変更可

/***** ========== ランタイム状態 ========== *****/
String myJson;

/***** ========== 受信フロー（保存→表示） ========== *****/
static void OnMessageReceived(const uint8_t* data, size_t len) {
  // 保存→ガード→演出→解析→表示→待機
  saveIncomingJson(data, len);  // RAMリングバッファへ保存（直近N件）
  DisplayManager::BlockFor(1600);
  Ripple_PlayOnce();
  loadDisplayDataFromJson();
  performDisplay();
  Radar_InitIdle();
}

/***** ========== Arduino 標準 ========== *****/
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP-NOW JSON Broadcast ===");

  // LED
  DisplayManager::Init(GLOBAL_BRIGHTNESS);
  DisplayManager::TextInit();
  Ripple_PlayOnce();

  // ボタン（OneButtonを使用して初期化）
  g_btn.setup(BUTTON_PIN, INPUT_PULLUP, true);  // アクティブLOW、内部プルアップ
  g_btn.setClickMs(300);                        // ダブルクリックの間隔
  // ダブルクリック：受信データ表示モードをトグル
  g_btn.attachDoubleClick([]() {
    DisplayMode = !DisplayMode;
    DisplayManager::AllOn(TEXT_BRIGHTNESS);  // モード切替時に全点灯
    Serial.printf("[MODE] 受信データ表示モード: %s\n", DisplayMode ? "ON" : "OFF");
  });
  // シングルクリック：モードONのとき最新受信データを再生
  g_btn.attachClick([]() {
    if (!DisplayMode) return;
    size_t n = inboxSize();
    if (n == 0) {
      Serial.println("[INBOX] 受信データなし");
      return;
    }
    InboxItem item;
    if (!inboxGet(n - 1, item)) {
      Serial.println("[INBOX] 取得失敗");
      return;
    }
    if (!loadDisplayDataFromJsonString(item.json)) {
      Serial.println("[PARSE] JSON解析失敗");
      return;
    }
    // 表示
    if (!performDisplay()) {
      Serial.println("[DISPLAY] 表示できるデータがありません");
    }
  });

  // LittleFS -> myJson 読み出し＆シリアル表示
  myJson = loadJsonFromPath(JSON_PATH, 2048);
  Serial.printf("📄 生データ:\n%s\n", myJson.c_str());
  Serial.printf("📄 %s (%uB)\n", JSON_PATH, (unsigned)myJson.length());
  if (!myJson.isEmpty()) {
    // 起動時にも表示試行
    loadDisplayDataFromJson();
    performDisplay();
  }

  // ESP-NOWコールバック登録
  Comm_SetOnMessage(OnMessageReceived);
  Comm_Init(WIFI_CH);
  // 受信RSSIしきい値の設定（-40dBmより弱い受信は破棄）
  Comm_SetMinRssiToAccept(RSSI_THRESHOLD_DBM);

  // データ待機モード開始 → Radar起動
  Serial.println("🔍 待機中: Radar開始");
  Radar_InitIdle();
}

void loop() {
  static unsigned long nextSend = 0;
  unsigned long now = millis();

  // --- ボタン（OneButton） ---
  // OneButtonの状態更新（イベント発火）
  g_btn.tick();

  // データ表示/エフェクト中はレーダーを停止
  if (!DisplayManager::IsActive()) {
    Radar_IdleStep(true);
  }
  delay(16);

  if (!myJson.isEmpty() && now >= nextSend) {
    Comm_SendJsonBroadcast(myJson);
    nextSend = now + 500 + (esp_random() % 200) - 50;  // ±100ms ジッター
  }
}
