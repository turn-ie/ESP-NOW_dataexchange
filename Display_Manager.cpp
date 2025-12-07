#include "Display_Manager.h"
#include <climits>

namespace DisplayManager {

void TextScroll_Stop();

// ---- 内部状態 ----
static unsigned long s_until_ms = 0;

// NeoMatrix 本体（ここで1つだけ生成）
static Adafruit_NeoMatrix s_matrix(
  DISP_W, DISP_H, DISP_LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_ROWS + NEO_MATRIX_PROGRESSIVE,
  DISP_PIXEL_TYPE
);

// Motion等が参照するMatrix（互換維持）
Adafruit_NeoMatrix& Matrix = s_matrix;

// テキスト表示用の明るさと状態
static uint8_t gTextBrightness = 20;  
static int MatrixWidth = 0;

// テキストカラーパレット
static const uint16_t colors[] = {
  s_matrix.Color(255,255,255),
  s_matrix.Color(255,0,0),
  s_matrix.Color(0,255,0),
  s_matrix.Color(0,0,255)
};


// ---- 内部ユーティリティ（テキスト表示用） ----
static int getStringWidth(const char* text) {
  if (!text) return 0;
  return strlen(text) * 6;
}

// ========== 公開API：初期化 ==========
void Init(uint8_t global_brightness) {
  s_matrix.begin();
  s_matrix.setBrightness(global_brightness);
  s_matrix.fillScreen(0);
  s_matrix.show();
  s_until_ms = 0;
}

// ========== 公開API：画像表示 ==========
void Clear() {
  TextScroll_Stop();
  s_matrix.fillScreen(0);
  s_matrix.show();
  s_until_ms = 0;
}

void AllOn(uint8_t r, uint8_t g, uint8_t b) {
  TextScroll_Stop();
  s_matrix.setBrightness(GLOBAL_BRIGHTNESS);
  s_matrix.fillScreen(s_matrix.Color(r, g, b));
  s_matrix.show();
  s_until_ms = 0;
}


bool ShowRGB(const uint8_t* rgb, size_t n, unsigned long display_ms) {
  TextScroll_Stop();
  if (!rgb) return false;
  if (n < (size_t)(DISP_W * DISP_H * 3)) return false;

  s_matrix.setRotation(3);
  s_matrix.setBrightness(GLOBAL_BRIGHTNESS);

  s_matrix.fillScreen(0);
  for (int sy = 0; sy < DISP_H; ++sy) {
    for (int sx = 0; sx < DISP_W; ++sx) {
      size_t i = (size_t)(sy * DISP_W + sx) * 3;
      s_matrix.drawPixel(sx, sy, s_matrix.Color(rgb[i + 1], rgb[i], rgb[i + 2]));
    }
  }
  s_matrix.show();

  if (display_ms == ULONG_MAX) {
    s_until_ms = 0;
  } else {
    s_until_ms = millis() + display_ms;
  }
  return true;
}

bool ShowRGB_Animated(const uint8_t* rgb, size_t n, unsigned long display_ms) {
  TextScroll_Stop();
  if (!rgb) return false;
  if (n < (size_t)(DISP_W * DISP_H * 3)) return false;

  s_matrix.setRotation(3);
  s_matrix.setBrightness(GLOBAL_BRIGHTNESS);

  s_matrix.fillScreen(0);
  s_matrix.show();
  delay(50);

  for (int sy = 0; sy < DISP_H; ++sy) {
    for (int sx = 0; sx < DISP_W; ++sx) {
      size_t i = (size_t)(sy * DISP_W + sx) * 3;
      s_matrix.drawPixel(sx, sy, s_matrix.Color(rgb[i + 1], rgb[i], rgb[i + 2]));
      s_matrix.show();
      delay(10);
    }
  }
  
  if (display_ms == ULONG_MAX) {
    s_until_ms = 0;
  } else {
    s_until_ms = millis() + display_ms;
  }
  return true;
}

// ========== 公開API：表示状態管理 ==========
bool IsActive() {
  return (s_until_ms != 0) && (millis() < s_until_ms);
}

bool EndIfExpired() {
  if (s_until_ms && millis() >= s_until_ms) {
    Clear();
    return true;
  }
  return false;
}

void BlockFor(unsigned long ms) {
  if (ms == 0) return;
  s_until_ms = millis() + ms;
}

// ========== 公開API：テキスト表示 ==========
void SetTextBrightness(uint8_t b) {
  gTextBrightness = b;
}

void TextInit() {
  s_matrix.setTextWrap(false);
  s_matrix.setBrightness(GLOBAL_BRIGHTNESS);
  s_matrix.setTextColor(colors[0]);
  MatrixWidth = s_matrix.width();
}

void TextPlayOnce(const char* text, uint16_t frame_delay_ms) {
  TextScroll_Stop();
  s_matrix.setBrightness(GLOBAL_BRIGHTNESS);
  int textWidth = getStringWidth(text);
  MatrixWidth = s_matrix.width();
  while (MatrixWidth >= -textWidth) {
    s_matrix.fillScreen(0);
    s_matrix.setCursor(MatrixWidth, 0);
    s_matrix.print(text);
    s_matrix.show();
    MatrixWidth--;
    delay(frame_delay_ms);
  }
}

// --- Non-blocking Text Scroll State ---
static String s_scrollText = "";
static uint16_t s_scrollDelay = 60;
static int s_scrollX = 0;
static int s_textWidth = 0;
static unsigned long s_lastScrollTime = 0;
static bool s_isScrolling = false;
static bool s_scrollLoop = true;

void TextScroll_Start(const char* text, uint16_t frame_delay_ms, bool loop) {
  TextScroll_Stop();
  s_until_ms = 0;
  if (!text) return;
  s_scrollText = String(text);
  s_scrollDelay = frame_delay_ms;
  s_textWidth = getStringWidth(s_scrollText.c_str());
  s_scrollLoop = loop;
  
  s_matrix.setBrightness(GLOBAL_BRIGHTNESS);
  s_matrix.setRotation(3);
  s_matrix.setTextWrap(false);
  s_scrollX = s_matrix.height();
  s_isScrolling = true;
  s_lastScrollTime = millis();

  s_matrix.fillScreen(0);
  s_matrix.setCursor(s_scrollX, 0);
  s_matrix.print(s_scrollText);
  s_matrix.show();
}

void TextScroll_Update() {
  if (!s_isScrolling) return;

  if (millis() - s_lastScrollTime >= s_scrollDelay) {
    s_lastScrollTime = millis();
    
    s_matrix.fillScreen(0);
    s_matrix.setCursor(s_scrollX, 0);
    s_matrix.print(s_scrollText);
    s_matrix.show();
    
    s_scrollX--;
    if (s_scrollX < -s_textWidth) {
      if (s_scrollLoop) {
        s_scrollX = s_matrix.height();
      } else {
        s_isScrolling = false;
        s_matrix.fillScreen(0);
        s_matrix.show();
      }
    }
  }
}

void TextScroll_Stop() {
  s_isScrolling = false;
  s_matrix.setRotation(0);
}

bool TextScroll_IsActive() {
  return s_isScrolling;
}

unsigned long TextEstimateDurationMs(const char* text, uint16_t frame_delay_ms) {
  if (!text) return 0;
  const int textWidth = getStringWidth(text);
  const int steps = s_matrix.width() + textWidth;
  return (unsigned long)steps * (unsigned long)frame_delay_ms;
}

} // namespace DisplayManager
