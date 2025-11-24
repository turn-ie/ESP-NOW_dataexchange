#ifndef _MOTION_H_
#define _MOTION_H_

#pragma once
#include <Arduino.h>
#include <Adafruit_NeoMatrix.h>
#include "Display_Manager.h"

// Motion.cppで参照するMatrixはDisplayManager経由で提供
using DisplayManager::Matrix;

// === モーション用の明るさと色相 ===
extern uint8_t gMotionBrightness;    // レーダー/リップル共通
extern uint8_t gMotionHue;           // レーダー/リップル共有色相



// モーション関連の関数
void Ripple_PlayOnce();
void DiagonalWave_PlayOnce();

// レーダー待機(非ブロッキング)
void Radar_InitIdle();
void Radar_IdleStep(bool doShow = true);

#endif
