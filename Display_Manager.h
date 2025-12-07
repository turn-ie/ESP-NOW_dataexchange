#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>

// 表示設定定数の外部宣言（.ino で定義）
extern uint16_t TEXT_FRAME_DELAY_MS;
extern int GLOBAL_BRIGHTNESS;

// 8x8/ピン/ピクセルタイプはここで定義（必要なら変更）
#ifndef DISP_W
#define DISP_W 8
#endif

#ifndef DISP_H
#define DISP_H 8
#endif

#ifndef DISP_LED_PIN
#define DISP_LED_PIN 14
#endif

#ifndef DISP_PIXEL_TYPE
#define DISP_PIXEL_TYPE (NEO_GRB + NEO_KHZ800)
#endif

namespace DisplayManager {
  // === 初期化 ===
  void Init(uint8_t global_brightness);

  // === 画像表示 ===
  void Clear();
  void AllOn(uint8_t r, uint8_t g, uint8_t b);  // ★追加
  bool ShowRGB(const uint8_t* rgb, size_t n, unsigned long display_ms);
  bool ShowRGB_Animated(const uint8_t* rgb, size_t n, unsigned long display_ms); // アニメーション付き

  // 全点灯
  void AllOnGreen(uint8_t brightness);
  void AllOnRed(uint8_t brightness);
  void AllOnWhite(uint8_t brightness);  // ★追加

  // === 表示状態管理（ガード） ===
  bool IsActive();              // 表示中ガードが張られているか
  bool EndIfExpired();          // 期限切れなら消灯しtrue
  void BlockFor(unsigned long ms); // 何も描画せず占有だけ張る

  // === テキスト表示 ===
  void SetTextBrightness(uint8_t b);
  void TextInit();  // テキスト用初期設定（setTextWrap等）
  void TextPlayOnce(const char* text, uint16_t frame_delay_ms);
  
  // Non-blocking Text Scroll
  void TextScroll_Start(const char* text, uint16_t frame_delay_ms, bool loop = true);
  void TextScroll_Update();
  void TextScroll_Stop();
  bool TextScroll_IsActive();

  unsigned long TextEstimateDurationMs(const char* text, uint16_t frame_delay_ms);

  // === Motion等が参照するMatrix（互換維持） ===
  extern Adafruit_NeoMatrix& Matrix;
}
