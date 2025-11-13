#include "Json_Handler.h"
#include "Display_Manager.h"

String displayFlag;
String displayText;
std::vector<uint8_t> rgbData;

// ===== インボックス（RAMリングバッファ）実装 =====
namespace {
    static constexpr size_t kInboxCapacity = 20; // 直近20件
    struct InboxSlot { unsigned long at; String json; };
    static InboxSlot sInbox[kInboxCapacity];
    static size_t sHead = 0;   // 先頭（最古）のインデックス
    static size_t sCount = 0;  // 現在件数

    static size_t advance(size_t i) { return (i + 1) % kInboxCapacity; }
}

void saveIncomingJson(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    String js;
    js.reserve(len + 1);
    js.concat((const char*)data, len);

    if (sCount < kInboxCapacity) {
        // 空きがある: 末尾に追加
        size_t tail = (sHead + sCount) % kInboxCapacity;
        sInbox[tail].at = millis();
        sInbox[tail].json = js;
        sCount++;
    } else {
        // 一杯: 最古を上書き（ヘッドを進める）
        sInbox[sHead].at = millis();
        sInbox[sHead].json = js;
        sHead = advance(sHead);
    }
}

size_t inboxSize() { return sCount; }

bool inboxGet(size_t index, InboxItem& out) {
    if (index >= sCount) return false;
    size_t pos = (sHead + index) % kInboxCapacity;
    out.atMillis = sInbox[pos].at;
    out.json = sInbox[pos].json; // コピー（必要なら参照化も可能）
    return true;
}

void inboxClear() {
    for (size_t i = 0; i < sCount; ++i) {
        size_t pos = (sHead + i) % kInboxCapacity;
        sInbox[pos].json = String();
        sInbox[pos].at = 0;
    }
    sHead = 0;
    sCount = 0;
}

static JsonObject parseJsonFile(const char* path) {
    if (!LittleFS.begin(false)) LittleFS.begin(true);
    File file = LittleFS.open(path, "r");
    if (!file) return JsonObject();
    
    static StaticJsonDocument<2048> doc;
    deserializeJson(doc, file);
    file.close();
    return doc.as<JsonObject>();
}

void loadDisplayDataFromJson() {
    JsonObject obj = parseJsonFile("/data.json");
    if (!obj) return;
    
    displayFlag = obj["flag"] | "";
    
    if (displayFlag == "text") {
        displayText = obj["text"] | "";
        rgbData.clear();
    } else if (displayFlag == "image" || displayFlag == "emoji") {
        displayText.clear();
        rgbData.clear();
        JsonArray arr = obj["rgb"];
        Serial.println(arr.size());
        for (auto v : arr) rgbData.push_back((uint8_t)v.as<int>());
    } else {
        displayText.clear();
        rgbData.clear();
    }
}

bool loadDisplayDataFromJsonString(const String& jsonString) {
    if (jsonString.isEmpty()) return false;
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, jsonString);
    if (err) return false;
    JsonObject obj = doc.as<JsonObject>();
    if (!obj) return false;

    displayFlag = obj["flag"] | "";

    if (displayFlag == "text") {
        displayText = obj["text"] | "";
        rgbData.clear();
        return true;
    } else if (displayFlag == "image" || displayFlag == "emoji") {
        displayText.clear();
        rgbData.clear();
        JsonArray arr = obj["rgb"];
        if (!arr.isNull()) {
            for (auto v : arr) rgbData.push_back((uint8_t)v.as<int>());
        }
        return true;
    } else {
        // 未知のフラグ
        displayText.clear();
        rgbData.clear();
        return false;
    }
}

String loadJsonFromPath(const char* path, size_t maxBytes) {
    if (!LittleFS.begin(false)) LittleFS.begin(true);
    if (!LittleFS.exists(path)) return String();
    File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) return String();
    String s;
    s.reserve(min((size_t)f.size(), maxBytes));
    while (f.available() && s.length() < (int)maxBytes) s += (char)f.read();
    f.close();
    return s;
}

bool performDisplay() {
    String flag = displayFlag;
    if (flag.isEmpty()) return false;
    flag.toLowerCase();
    
    if (flag == "text") {
        if (displayText.isEmpty()) return false;
        DisplayManager::SetTextBrightness(TEXT_BRIGHTNESS);
        unsigned long dur = DisplayManager::TextEstimateDurationMs(displayText.c_str(), TEXT_FRAME_DELAY_MS);
        if (dur) DisplayManager::BlockFor(dur);
        DisplayManager::TextPlayOnce(displayText.c_str(), TEXT_FRAME_DELAY_MS);
        return true;
    }
    
    if (flag == "image" || flag == "photo" || flag == "emoji") {
        if (rgbData.empty()) return false;
        // 任意のサイズに対応するが、データはRGB(3要素)の倍数であるべき
        if ((rgbData.size() % 3) != 0) {
            Serial.printf("[DISPLAY] rgbData size (%u) is not multiple of 3\n", (unsigned)rgbData.size());
        }
        return DisplayManager::ShowRGBRotCCW(rgbData.data(), rgbData.size(), 3000);
    }
    
    return false;
}



const char* getDisplayFlag() {
    return displayFlag.c_str();
}

const char* getDisplayText() {
    return displayText.c_str();
}

const std::vector<uint8_t>& getRgbData() {
    return rgbData;
}
