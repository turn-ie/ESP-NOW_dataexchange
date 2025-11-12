#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>

#include <LittleFS.h>
#include <FS.h>

#include <ArduinoJson.h>

#include "Motion.h"        // Radar/Ripple 用
#include "Display_image.h" // LED表示モジュール
#include "Display_text.h"  // テキストスクロール表示

/***** ========== LED MATRIX ========== *****/
#define GLOBAL_BRIGHTNESS 10

// テキストスクロール設定
static const uint16_t TEXT_FRAME_DELAY_MS = 50;  // スクロール速度(1ステップの遅延)
static const uint8_t  TEXT_BRIGHTNESS     = 20;  // テキスト時の明るさ

/***** ========== 無線・ファイル設定 ========== *****/
static const int WIFI_CH = 6;
static const uint8_t MAC_BC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static const char* JSON_PATH = "/my_data_text.json";

// === Broadcast chunk header ===
static const uint16_t CHUNK_MAX      = 200;     // 1パケットのデータ最大
static const uint16_t MAX_MSG_BYTES  = 2048;    // 再構成の最大サイズ
static const uint16_t MAX_CHUNKS     = (MAX_MSG_BYTES + CHUNK_MAX - 1) / CHUNK_MAX;
static const unsigned long RX_TIMEOUT_MS = 2500; // 受信途中の期限

#pragma pack(push,1)
struct ChunkHdr {
  uint8_t  tag;    // 'C'
  uint16_t msgId;  // 送信ごとに++
  uint16_t total;  // 総チャンク数
  uint16_t idx;    // 0..total-1
  uint16_t len;    // このチャンクのデータ長
};
#pragma pack(pop)

/***** ========== ランタイム状態 ========== *****/
String myJson;
uint8_t selfMac[6] = {0};
static uint16_t g_msgId = 1;

/***** ========== ファイル ========== *****/
String loadJsonFromLittleFS(const char* path, size_t maxBytes){
  if (!LittleFS.begin(false)) { LittleFS.begin(true); }
  if (!LittleFS.exists(path)) return String();
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory()) return String();
  String s; s.reserve(min((size_t)f.size(), maxBytes));
  while (f.available() && s.length() < (int)maxBytes) s += (char)f.read();
  f.close();
  return s;
}

/***** ========== Flagルータ（画像/テキスト振り分け） ========== *****/
// 受信バッファ(JSON)を見て image/text を出し分けて表示する
bool ShowByFlag_Route(const uint8_t* buf, size_t len) {
  if (!buf || len == 0) return false;

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, buf, len)) {
    Serial.println("❌ JSON parse (router)");
    return false;
  }

  // flagの取得（"image" / "text" を想定。"photo" を互換として同扱い）
  String flag = doc["flag"] | "";
  flag.toLowerCase();

  if (flag == "image" || flag == "photo") {
    return Display_ShowFromJson(buf, len, 3000);
  }

  if (flag == "text") {
    // テキスト本文の取り出し
    const char* text = nullptr;
    if (doc["text"].is<const char*>()) {
      text = doc["text"].as<const char*>();
    } else if (doc["records"].is<JsonArray>() && doc["records"][0]["text"].is<const char*>()) {
      text = doc["records"][0]["text"].as<const char*>();
    }

    if (!text || !*text) {
      Serial.println("❌ no text field for flag=text");
      return false;
    }

    // 任意の明るさ（JSONにbrightnessがあれば優先）
    uint8_t tb = TEXT_BRIGHTNESS;
    if (doc.containsKey("brightness")) {
      tb = constrain(doc["brightness"].as<int>(), 0, 255);
    }
    Matrix_SetTextBrightness(tb);

    // スクロール所要時間を見積 → その間は受信抑止ガードを張る
    const unsigned long dur = Text_EstimateDurationMs(text, TEXT_FRAME_DELAY_MS);
    if (dur) Display_BlockFor(dur);

    // スクロール実行（ブロッキング）
    Text_PlayOnce(text, TEXT_FRAME_DELAY_MS);
    return true;
  }

  // 未知のflag → 画像扱いにフォールバック
  Serial.printf("⚠️ unknown flag='%s' → image fallback\n", flag.c_str());
  return Display_ShowFromJson(buf, len, 3000);
}

/***** ========== 送信関連 ========== *****/
void sendJsonBroadcast(const String& json) {
  const size_t L = json.length();
  if (L == 0) return;

  // 250B以下は1パケットでそのまま
  if (L <= 250) {
    esp_now_send(MAC_BC, (const uint8_t*)json.c_str(), L);
    Serial.println("📢 broadcast JSON (single)");
    return;
  }

  // 250B超は分割
  const uint16_t total = (L + CHUNK_MAX - 1) / CHUNK_MAX;
  if (total > MAX_CHUNKS) {
    Serial.printf("⚠ JSON大きすぎ: 最大%uB, 今%uB\n", MAX_MSG_BYTES, (unsigned)L);
    return;
  }

  const uint16_t myId = g_msgId++;
  if (g_msgId == 0) g_msgId = 1;

  uint8_t packet[sizeof(ChunkHdr) + CHUNK_MAX];
  for (uint16_t i = 0; i < total; i++) {
    size_t off = (size_t)i * CHUNK_MAX;
    uint16_t n = (uint16_t)min((size_t)CHUNK_MAX, L - off);

    ChunkHdr* h = (ChunkHdr*)packet;
    h->tag   = 'C';
    h->msgId = myId;
    h->total = total;
    h->idx   = i;
    h->len   = n;

    memcpy(packet + sizeof(ChunkHdr), json.c_str() + off, n);
    esp_now_send(MAC_BC, packet, sizeof(ChunkHdr) + n);
    delay(3); // 連続送信の隙間
  }
  Serial.printf("📢 broadcast JSON (chunked): %u chunks, %uB\n", total, (unsigned)L);
}

/***** ========== コールバック ========== *****/
void onSent(const wifi_tx_info_t* /*info*/, esp_now_send_status_t status) {
  Serial.printf("[SEND] %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!data || len <= 0) return;
  if (info && memcmp(info->src_addr, selfMac, 6) == 0) return; // 自送信は無視

  // 1) 単発JSON（先頭が'{'）
  if (data[0] == '{') {
    Serial.print("📥 single: ");
    Serial.write(data, len);
    Serial.println();

    // レーダーと重ならないよう受信～表示の間は占有ガード
    Display_BlockFor(1600); // Rippleおよび直後処理の目安 (~1.5s)

    // データ受信完了 → Rippleエフェクト実行
    Ripple_PlayOnce();

    // 表示（flag=text/image 振り分け）
    if (!ShowByFlag_Route(data, len)) {
      Display_ShowFromJson(data, len, /*ms*/3000);
    }
    
  // 表示終了後、再びRadar待機モードへ
    Serial.println("🔍 待機中: Radar再開");
    Radar_InitIdle();
    
    return;
  }

  // 2) チャンク（先頭が 'C'）
  if ((uint8_t)data[0] == 'C' && len >= (int)sizeof(ChunkHdr)) {
    static struct {
      bool active = false;
      uint16_t msgId = 0, total = 0, gotCount = 0, lastLen = 0;
      uint8_t fromMac[6];
      unsigned long startAt = 0;
      bool got[(MAX_MSG_BYTES + CHUNK_MAX - 1) / CHUNK_MAX];
      uint8_t buf[MAX_MSG_BYTES];
    } rx;

    const ChunkHdr* h = (const ChunkHdr*)data;
    if (h->len > CHUNK_MAX || h->total == 0 || h->total > MAX_CHUNKS || h->idx >= h->total) return;

    const uint8_t* src = info ? info->src_addr : selfMac;
    bool needInit = (!rx.active)
                 || (memcmp(rx.fromMac, src, 6) != 0)
                 || (rx.msgId != h->msgId)
                 || (millis() - rx.startAt > RX_TIMEOUT_MS);

    if (needInit) {
      rx.active = true;
      rx.msgId = h->msgId;
      rx.total = h->total;
      rx.gotCount = 0;
      rx.lastLen = 0;
      memset(rx.got, 0, sizeof(rx.got));
      memset(rx.buf, 0, sizeof(rx.buf));
      memcpy(rx.fromMac, src, 6);
    }
    rx.startAt = millis();

    if ((int)(sizeof(ChunkHdr) + h->len) != len) return;
    size_t off = (size_t)h->idx * CHUNK_MAX;
    if (off + h->len > sizeof(rx.buf)) return;

    if (!rx.got[h->idx]) {
      memcpy(rx.buf + off, data + sizeof(ChunkHdr), h->len);
      rx.got[h->idx] = true;
      rx.gotCount++;
      if (h->idx == h->total - 1) rx.lastLen = h->len;
    }

    // 全部そろったら表示
    if (rx.gotCount == rx.total && rx.lastLen > 0) {
      size_t fullLen = (size_t)(rx.total - 1) * CHUNK_MAX + rx.lastLen;
      Serial.printf("📥 chunked complete (%u chunks, %uB)\n", rx.total, (unsigned)fullLen);

      // レーダーと重ならないよう占有ガード（Ripple所要時間ぶん）
      Display_BlockFor(1600);

      // データ受信完了 → Rippleエフェクト実行
      Ripple_PlayOnce();

      if (!ShowByFlag_Route(rx.buf, fullLen)) {
        Display_ShowFromJson(rx.buf, fullLen, /*ms*/3000);
      }
      rx.active = false;

      // 表示終了後、再びRadar待機モードへ
      Serial.println("🔍 待機中: Radar再開");
      Radar_InitIdle();
    }
  }
}

/***** ========== Arduino 標準 ========== *****/
void setup(){
  Serial.begin(115200); delay(200);
  Serial.println("\n=== ESP-NOW JSON Broadcast (no HELLO/ACK) ===");

  // LED
  Display_Init(GLOBAL_BRIGHTNESS);
  Matrix_Init();
  Ripple_PlayOnce();

  // LittleFS -> myJson 読み出し＆シリアル表示
  myJson = loadJsonFromLittleFS(JSON_PATH, MAX_MSG_BYTES);
  Serial.printf("📄 %s (%uB)\n", JSON_PATH, (unsigned)myJson.length());
  if (!myJson.isEmpty()) {
    // 起動時にも表示試行（flag で text/image 自動振り分け）
    if (!ShowByFlag_Route((const uint8_t*)myJson.c_str(), myJson.length())) {
      Display_ShowFromJson((const uint8_t*)myJson.c_str(), myJson.length(), 3000);
    }
  }

  // WiFi/ESP-NOW
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);

  esp_wifi_get_mac(WIFI_IF_STA, selfMac);
  Serial.printf("MAC:%s CH:%d\n", WiFi.macAddress().c_str(), WIFI_CH);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    while(true) delay(1000);
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);

  // （環境によって）BC peer登録で安定することがある
  if (!esp_now_is_peer_exist(MAC_BC)) {
    esp_now_peer_info_t p{};
    memcpy(p.peer_addr, MAC_BC, 6);
    p.ifidx = WIFI_IF_STA;
    p.channel = WIFI_CH;
    p.encrypt = false;
    esp_now_add_peer(&p);
  }

  // データ待機モード開始 → Radar起動
  Serial.println("🔍 待機中: Radar開始");
  Radar_InitIdle();
}

void loop(){
  static unsigned long nextSend = 0;
  unsigned long now = millis();

  // データ表示/エフェクト中はレーダーを停止
  if (!Display_IsActive()) {
    // 待機中のみレーダーを回す
    Radar_IdleStep(true);
  }
  delay(16);

  if (!myJson.isEmpty() && now >= nextSend) {
    sendJsonBroadcast(myJson); // 250B以下→単発, 超→チャンク
    nextSend = now + 2000 + (esp_random() % 200) - 100; // ±100ms ジッター
  }
}
