#ifndef JSON_HANDLER_H_
#define JSON_HANDLER_H_

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

// ========== 受信インボックス（RAMリングバッファ） ==========
// 直近N件だけRAMに保持（フラッシュ書き込み寿命に影響しない）
// - i=0 は最古、i=size-1 は最新
// - 保存内容: 受信時刻(millis)、JSON文字列
struct InboxItem {
	unsigned long atMillis;
	String json;
};

void saveIncomingJson(const uint8_t* data, size_t len);
size_t inboxSize();
bool inboxGet(size_t index, InboxItem& out);



// ========== 表示設定定数（.ino で定義） ==========
extern uint16_t TEXT_FRAME_DELAY_MS;  // テキストスクロール速度 [ms]
extern uint8_t TEXT_BRIGHTNESS;       // テキスト表示時の明るさ

// ========== JSON表示データ管理 ==========
extern String displayFlag;
extern String displayText;
extern std::vector<uint8_t> rgbData;

// ========== インターフェース ==========


bool loadDisplayFromLittleFS(const char* path = "/data.json");
bool saveJsonToPath(const char* path, const String& jsonString);
bool loadDisplayFromJsonString(const String& jsonString);
String loadJsonFromPath(const char* path, size_t maxBytes = 2048);
bool performDisplay();

#endif // JSON_HANDLER_H_
