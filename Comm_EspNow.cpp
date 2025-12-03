#include "Comm_EspNow.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_idf_version.h>

// === 設定 ===
static const uint8_t MAC_BC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
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

static uint8_t s_selfMac[6] = {0};
static uint16_t s_msgId = 1;
static CommOnMessageCB s_onMessage = nullptr;
static volatile int s_lastRssi = -128; // 未取得/非対応時は -128 を保持
// 受信許可最小RSSI。既定はフィルタ無効（-128）。.ino から Comm_SetMinRssiToAccept() で設定してください。
static volatile int s_minRssiAccept = -128;

// 受信再構成バッファ
static struct RxState {
  bool active = false;
  uint16_t msgId = 0, total = 0, gotCount = 0, lastLen = 0;
  uint8_t fromMac[6] = {0};
  unsigned long startAt = 0;
  bool got[(MAX_MSG_BYTES + CHUNK_MAX - 1) / CHUNK_MAX]{};
  uint8_t buf[MAX_MSG_BYTES]{};
} s_rx;

// 共通の受信処理本体（mac アドレスは任意）
static void handleRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
  if (!data || len <= 0) return;
  if (mac_addr && memcmp(mac_addr, s_selfMac, 6) == 0) return; // 自送信は無視

  // ★変更: 受信データの中身を少し表示する
  Serial.printf("[%lu] [RX] Recv packet len=%d | ", millis(), len);
  for(int i=0; i<min(len, 20); i++) { // 先頭20バイトを表示
      Serial.printf("%02X ", data[i]);
  }
  Serial.println("..."); // 改行

  // 1) 単発JSON（先頭'{'）
  if (data[0] == '{') {
    Serial.printf("RX: Single JSON (%d bytes)\n", len); // 受信デバッグ
    if (s_onMessage) s_onMessage(data, (size_t)len);
    return;
  }

  // 2) チャンク（先頭 'C'）
  if ((uint8_t)data[0] != 'C' || len < (int)sizeof(ChunkHdr)) return;
  const ChunkHdr* h = (const ChunkHdr*)data;
  if (h->len > CHUNK_MAX || h->total == 0 || h->total > MAX_CHUNKS || h->idx >= h->total) return;

  bool needInit = (!s_rx.active)
               || (mac_addr && memcmp(s_rx.fromMac, mac_addr, 6) != 0)
               || (s_rx.msgId != h->msgId)
               || (millis() - s_rx.startAt > RX_TIMEOUT_MS);

  if (needInit) {
    Serial.printf("RX: Start Chunked Msg ID=%d Total=%d\n", h->msgId, h->total); // 受信デバッグ
    s_rx = RxState{}; // reset struct
    s_rx.active = true;
    s_rx.msgId = h->msgId;
    s_rx.total = h->total;
    if (mac_addr) memcpy(s_rx.fromMac, mac_addr, 6);
  }
  s_rx.startAt = millis();

  if ((int)(sizeof(ChunkHdr) + h->len) != len) return;
  size_t off = (size_t)h->idx * CHUNK_MAX;
  if (off + h->len > sizeof(s_rx.buf)) return;

  if (!s_rx.got[h->idx]) {
    memcpy(s_rx.buf + off, data + sizeof(ChunkHdr), h->len);
    s_rx.got[h->idx] = true;
    s_rx.gotCount++;
    if (h->idx == h->total - 1) s_rx.lastLen = h->len;
  }

  if (s_rx.gotCount == s_rx.total && s_rx.lastLen > 0) {
    Serial.println("RX: All Chunks Received"); // 受信デバッグ
    size_t fullLen = (size_t)(s_rx.total - 1) * CHUNK_MAX + s_rx.lastLen;
    if (s_onMessage) s_onMessage(s_rx.buf, fullLen);
    s_rx.active = false;
  }
}

// ===== Arduino-ESP32 のバージョン差異に対応したコールバック定義 =====
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
// 新API (IDF v5 系): 型は wifi_tx_info_t / esp_now_recv_info
static void onSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  (void)info; (void)status;
}
static void onRecv(const esp_now_recv_info* info, const uint8_t* data, int len) {
  const uint8_t* mac = (info && info->src_addr) ? info->src_addr : nullptr;
  // RSSIを取得（利用可能な場合）
  if (info && info->rx_ctrl) {
    s_lastRssi = (int)info->rx_ctrl->rssi; // dBm
    if (s_lastRssi < s_minRssiAccept) {
      return;
    }
  } else {
    s_lastRssi = -128;
  }
  handleRecv(mac, data, len);
}
#else
// 旧API: 型は MAC アドレスポインタ
static void onSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
  (void)mac_addr; (void)status;
}
static void onRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
  // 旧APIではRSSIが渡されないため不明扱い
  s_lastRssi = -128;
  handleRecv(mac_addr, data, len);
}
#endif

void Comm_Init(int wifiChannel) {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_get_mac(WIFI_IF_STA, s_selfMac);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed"); // 初期化失敗ログ
    return;
  }
  Serial.printf("ESP-NOW Init Success. MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                s_selfMac[0], s_selfMac[1], s_selfMac[2], s_selfMac[3], s_selfMac[4], s_selfMac[5]); // 初期化成功ログ

  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);

  if (!esp_now_is_peer_exist(MAC_BC)) {
    esp_now_peer_info_t p{};
    memcpy(p.peer_addr, MAC_BC, 6);
    p.ifidx = WIFI_IF_STA;
    p.channel = wifiChannel;
    p.encrypt = false;
    esp_now_add_peer(&p);
  }
}

void Comm_SetMinRssiToAccept(int dbm) {
  s_minRssiAccept = dbm;
}

void Comm_SetOnMessage(CommOnMessageCB cb) {
  s_onMessage = cb;
}

void Comm_SendJsonBroadcast(const String& json) {
  const size_t L = json.length();
  if (L == 0) return;

  // デバッグ: 送信チャンネルとデータ長を表示
  uint8_t primaryChan;
  wifi_second_chan_t secondChan;
  esp_wifi_get_channel(&primaryChan, &secondChan);
  
  // ★変更: タイムスタンプ付きで送信開始ログ
  Serial.printf("[%lu] [TX] Start Broadcast %u bytes on CH %u\n", millis(), (unsigned)L, primaryChan);

  if (L <= 250) {// 単発送信
    esp_now_send(MAC_BC, (const uint8_t*)json.c_str(), L);
    Serial.printf("[%lu] [TX] End Broadcast (Single)\n", millis());
    return;
  }
  // 分割送信
  const uint16_t total = (L + CHUNK_MAX - 1) / CHUNK_MAX;
  if (total > MAX_CHUNKS) return;

  const uint16_t myId = s_msgId++;
  if (s_msgId == 0) s_msgId = 1;

  uint8_t packet[sizeof(ChunkHdr) + CHUNK_MAX];
  for (uint16_t i = 0; i < total; i++) {
    // Serial.printf("Sending chunk %u/%u\n", i + 1, total); // ログ抑制
    size_t off = (size_t)i * CHUNK_MAX;
    uint16_t n = (uint16_t)min((size_t)CHUNK_MAX, L - off);

    ChunkHdr* h = (ChunkHdr*)packet;
    h->tag   = 'C';//分割ですよフラグ
    h->msgId = myId;
    h->total = total;
    h->idx   = i;
    h->len   = n;

    memcpy(packet + sizeof(ChunkHdr), json.c_str() + off, n);
    esp_now_send(MAC_BC, packet, sizeof(ChunkHdr) + n);
    delay(3);
  }
  // ★追加: 送信完了ログ
  Serial.printf("[%lu] [TX] End Broadcast (Chunked)\n", millis());
}

