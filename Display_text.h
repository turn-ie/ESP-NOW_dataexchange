#ifndef _DISPLAY_TEXT_H_
#define _DISPLAY_TEXT_H_

#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>

// テキスト表示は画像表示と同じ物理Matrixを共有する
// Display_image.cpp で生成された Matrix の参照を流用する
extern Adafruit_NeoMatrix& TextMatrix;

// テキスト表示用の明るさ
extern uint8_t gTextBrightness;

// 明るさ設定
void Matrix_SetTextBrightness(uint8_t b);

// 初期化
void Matrix_Init();

// フロー表示（非ブロッキング、1フレーム分だけ進行）
void Text_Flow(char* Text);

// ワンショットスクロール（ブロッキング）
void Text_PlayOnce(const char* text, uint16_t frame_delay_ms);

// スクロール所要時間の見積（ms）
unsigned long Text_EstimateDurationMs(const char* text, uint16_t frame_delay_ms);

#endif
