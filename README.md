# ESP-NOW データ交換 & LED表示

ESP32間でESP-NOWプロトコルを使用してJSONデータを交換し、8×8 LEDマトリクスに画像を表示するプロジェクトです。

## 🌟 主な機能

- **ESP-NOWによる無線通信**: 2台のESP32間でデータを自動交換
- **RSSI距離判定**: 近距離（-20dBm以内）でのみ通信を開始
- **LEDマトリクス表示**: 8×8 NeoPixelマトリクスに画像データを表示
- **視覚エフェクト**: レーダー＆リップルアニメーション
- **自動再接続**: 表示終了後に自動的に相手を再探索

## 📋 必要な部品

### ハードウェア
- ESP32開発ボード × 2
- 8×8 NeoPixel LEDマトリクス（WS2812B）× 2
- USB電源ケーブル × 2

### ソフトウェア（Arduinoライブラリ）
- `Adafruit_GFX`
- `Adafruit_NeoMatrix`
- `Adafruit_NeoPixel`
- `ArduinoJson`
- `LittleFS`（ESP32標準）
- `WiFi`（ESP32標準）
- `esp_now`（ESP32標準）

## 🔌 配線

| ESP32 Pin | LEDマトリクス |
|-----------|--------------|
| GPIO 14   | DIN          |
| 5V        | VCC          |
| GND       | GND          |

## 📁 ファイル構成

```
ESP-NOW_dataexchange/
├── ESP-NOW_dataexchange.ino  # メインプログラム
├── Motion.cpp                 # レーダー/リップルエフェクト実装
├── Motion.h                   # モーション関数ヘッダー
└── README.md                  # このファイル
```

## 🚀 使い方

### 1. 初期設定

#### JSONデータファイルの準備
LittleFSに `/my_data_text.json` ファイルを配置します。

**JSONフォーマット例:**
```json
{
  "id": "393235",
  "flag": "photo",
  "brightness": 10,
  "rgb": [
    255, 0, 0,    /* ピクセル0: 赤 */
    0, 255, 0,    /* ピクセル1: 緑 */
    0, 0, 255,    /* ピクセル2: 青 */
    ...           /* 合計64ピクセル分（192個の値） */
  ]
}
```

または、`records`配列形式も対応:
```json
{
  "records": [
    {
      "rgb": [/* 192個の値 */]
    }
  ]
}
```

### 2. アップロード

1. Arduino IDEで `ESP-NOW_dataexchange.ino` を開く
2. ボード設定: `ESP32 Dev Module`
3. 2台のESP32にそれぞれアップロード

### 3. 動作確認

1. 2台のESP32に電源を投入
2. 起動時にリップルエフェクトが表示される
3. レーダーアニメーションが開始（相手探索中）
4. 相手発見時にシリアルモニタに表示:
   ```
   🔍 相手発見: XX:XX:XX:XX:XX:XX | RSSI: -XX dBm (非常に近い)
   ```
5. データ交換が自動的に開始
6. 受信完了後、3秒間画像を表示
7. 表示終了後、再びレーダーモードに戻る

## ⚙️ パラメータ調整

### 通信設定（ESP-NOW_dataexchange.ino内）

```cpp
// RSSI閾値（開始判定）
static const int RSSI_START_THRESHOLD = -20;  // -20dBm以内で開始

// WiFiチャンネル
static const int WIFI_CH = 6;

// 表示時間
static const unsigned long DISPLAY_MS = 3000;  // 3秒

// HELLOバースト設定
static const unsigned long HELLO_BURST_MS  = 1000;  // 1秒間連打
static const unsigned long HELLO_BURST_INT = 150;   // 150ms間隔
```

### LED設定

```cpp
#define LED_PIN   14              // GPIO番号
#define W 8                       // 幅
#define H 8                       // 高さ
#define GLOBAL_BRIGHTNESS 10      // 輝度（0-255）
```

### モーション設定（Motion.cpp内）

```cpp
uint8_t gMotionBrightness = 20;   // エフェクト輝度
uint8_t gMotionHue = 90;          // 色相（0-255）
```

**色相値の参考:**
- 0: 赤
- 90: 赤ピンク
- 100: ピンク
- 210: 水色
- 235: 青緑

## 🔧 動作フロー

```
[起動]
  ↓
リップルエフェクト再生
  ↓
自分のJSON画像を表示
  ↓
レーダーモード（探索中）
  ↓
相手発見（RSSI > -20dBm）
  ↓
データ交換開始
  ↓
受信完了（CRC検証）
  ↓
リップルエフェクト再生
  ↓
受信した画像を3秒表示
  ↓
LEDオフ → レーダー再開 + HELLO連打（1秒間）
  ↓
（繰り返し）
```

## 📡 通信プロトコル

### メッセージタイプ
- `HELLO (1)`: ピア探索
- `META (2)`: 転送メタデータ（サイズ、CRC等）
- `CHUNK (3)`: データチャンク（160バイト/個）
- `NACK (4)`: 再送要求
- `ACK (5)`: 受信完了確認

### 特徴
- **チャンク分割**: 大きなデータを160バイトに分割して送信
- **CRC32検証**: データ整合性を確認
- **自動再送**: NACKにより欠損チャンクを再送
- **進捗表示**: 10%単位で受信進捗を表示

## 🐛 トラブルシューティング

### 相手を発見しない
- RSSI閾値を緩和: `RSSI_START_THRESHOLD` を `-40` などに変更
- WiFiチャンネルが一致しているか確認
- 2台が近距離にあるか確認

### LEDが点灯しない
- GPIO14の配線を確認
- 電源容量を確認（64個のLEDには十分な電流が必要）
- `GLOBAL_BRIGHTNESS` を上げてみる

### データ転送が失敗する
- シリアルモニタでエラーメッセージを確認
- CRCエラーの場合はJSON形式を確認
- `rgb`配列が192個（64ピクセル×3色）あるか確認

### 表示が回転している
- `drawRGBArrayRotCCW()` で90度反時計回りに回転
- 必要に応じて回転ロジックを調整

## 📝 ライセンス

MIT License

## 👤 作者

kaishiraishi

## 🙏 謝辞

- Adafruit社のLEDライブラリ
- ArduinoJsonライブラリ
- ESP32コミュニティ
