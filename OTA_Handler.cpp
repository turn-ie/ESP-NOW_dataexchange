#include <WiFi.h>
#include <ArduinoOTA.h>
#include <esp_now.h>
#include "OTA_Handler.h"
#include "Display_Manager.h"

// WiFi設定
static const char* WIFI_SSID = "IA4-411";
static const char* WIFI_PASS = "gEdCx5Rdm9J9WNAJ7xN7";

// Telnetサーバー
static WiFiServer telnetServer(23);
static WiFiClient telnetClient;

// 状態フラグ
static bool s_debugMode = false;
static bool s_otaReady = false;

// ========== デバッグ出力関数 ==========
void debugPrint(const char* msg) {
  Serial.print(msg);
  if (s_debugMode && telnetClient && telnetClient.connected()) {
    telnetClient.print(msg);
  }
}

void debugPrintln(const char* msg) {
  Serial.println(msg);
  if (s_debugMode && telnetClient && telnetClient.connected()) {
    telnetClient.println(msg);
  }
}

void debugPrintln(const String& msg) {
  debugPrintln(msg.c_str());
}

void debugPrintf(const char* format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  debugPrint(buf);
}

// ========== デバッグモード関連 ==========
bool isDebugMode() {
  return s_debugMode;
}

void startDebugMode() {
  if (s_debugMode) return; // 既にデバッグモードなら何もしない

  s_debugMode = true;
  Serial.println("\n=== DEBUG MODE (OTA + Telnet) ===");

  // ESP-NOWを停止してWiFi接続に備える
  esp_now_deinit();
  WiFi.disconnect(true);
  delay(100);

  DisplayManager::AllOn(0, 255, 0); // 赤色: WiFi接続中

  // WiFi接続
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.println("[DEBUG] Connecting to WiFi...");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 1000) {
    delay(50);
    retry++;
    if (retry % 20 == 0) Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    DisplayManager::AllOn(255, 0, 0); // 緑色: 接続成功
    // Telnetサーバー開始
    telnetServer.begin();
    telnetServer.setNoDelay(true);

    // OTAセットアップ
    ArduinoOTA.setPassword("0000");
    ArduinoOTA.setHostname("turnie_debug");
    ArduinoOTA.begin();
    s_otaReady = true;

    Serial.println("✅ Debug Mode Ready");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.println("Telnet: nc <IP> 23");
    Serial.println("OTA: Arduino IDE or PlatformIO");

  
  } else {
    Serial.println("⚠️ WiFi connection failed");
    DisplayManager::Clear();
    s_debugMode = false;
    s_otaReady = false;
    // 接続失敗時は再起動して通常モードへ
    
  }
}

void handleDebugMode() {
  if (!s_debugMode) return;

  // Telnetクライアント接続確認
  if (telnetServer.hasClient()) {
    if (telnetClient && telnetClient.connected()) {
      telnetClient.stop();
    }
    telnetClient = telnetServer.available();
    telnetClient.println("=== Telnet Debug Connected ===");
    telnetClient.print("IP: ");
    telnetClient.println(WiFi.localIP());
    telnetClient.println("Type 'help' for commands.");
  }

  // Telnetからの入力を処理
  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    String cmd = telnetClient.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "help") {
      telnetClient.println("=== Available Commands ===");
      telnetClient.println("help    - Show this help");
      telnetClient.println("status  - Show device status");
      telnetClient.println("reboot  - Restart device");
      telnetClient.println("heap    - Show free heap memory");
      telnetClient.println("wifi    - Show WiFi info");
    } else if (cmd == "status") {
      telnetClient.println("=== Device Status ===");
      telnetClient.printf("Uptime: %lu ms\n", millis());
      telnetClient.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
      telnetClient.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
    } else if (cmd == "reboot") {
      telnetClient.println("Rebooting...");
      delay(100);
      ESP.restart();
    } else if (cmd == "heap") {
      telnetClient.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
      telnetClient.printf("Min Free Heap: %u bytes\n", ESP.getMinFreeHeap());
    } else if (cmd == "wifi") {
      telnetClient.println("=== WiFi Info ===");
      telnetClient.printf("SSID: %s\n", WiFi.SSID().c_str());
      telnetClient.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      telnetClient.printf("RSSI: %d dBm\n", WiFi.RSSI());
      telnetClient.printf("Channel: %d\n", WiFi.channel());
    } else if (cmd.length() > 0) {
      telnetClient.printf("Unknown command: %s\n", cmd.c_str());
    }
  }

  // 定期ステータス表示 (30秒ごと)
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    lastStatus = millis();
    if (telnetClient && telnetClient.connected()) {
      // 時間
      unsigned long uptimeSec = millis() / 1000;
      unsigned long mins = uptimeSec / 60;
      unsigned long secs = uptimeSec % 60;
      
      telnetClient.println("\n--- DEBUG STATUS ---");
      telnetClient.printf("Uptime:    %lu:%02lu\n", mins, secs);
      telnetClient.printf("Heap:      %u / %u bytes\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
      telnetClient.printf("WiFi CH:   %d (Target: 6)\n", WiFi.channel());
      telnetClient.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
      telnetClient.printf("IP:        %s\n", WiFi.localIP().toString().c_str());
      telnetClient.printf("MAC:       %s\n", WiFi.macAddress().c_str());
      telnetClient.printf("OTA:       %s\n", s_otaReady ? "Ready" : "Not Ready");
      telnetClient.println("--------------------\n");
    }
  }

  // OTA処理
  if (s_otaReady) {
    ArduinoOTA.handle();
  }

  delay(10);
}

// ========== 従来のOTA関数（互換性のため） ==========
void setupOTA() {
  DisplayManager::AllOn(255, 0, 0);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.println("[OTA] Connecting to WiFi...");

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 200) {
    delay(50);
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setPassword("0000");
    ArduinoOTA.setHostname("WifiOTA_NWstudio");
    ArduinoOTA.begin();
    s_otaReady = true;
    Serial.println("✅ OTA Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    DisplayManager::AllOn(0, 255, 0);
  } else {
    Serial.println("⚠️ WiFi failed, OTA aborted.");
    s_otaReady = false;
    DisplayManager::Clear();
  }
}

void handleOTA() {
  if (s_otaReady) {
    ArduinoOTA.handle();
  }
}
