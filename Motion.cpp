#include "Motion.h"
#include <math.h>

// === モーション用の明るさと色相 ===
uint8_t gMotionBrightness = 20;
uint8_t gMotionHue        = 74;
// Hue 0-255 早見表 (HSV H→代表色 / 角度近似)
//turnie1 90 赤ピンク
//turnie2 212 青
//turnie3 4 緑
//turnie4 74 黄



// ユーティリティ関数
static inline uint8_t gamma8(float v01){ 
  if(v01<0) v01=0; 
  if(v01>1) v01=1; 
  float g=powf(v01,1.0f/2.2f); 
  return (uint8_t)(g*255.f+0.5f); 
}

static uint16_t ColorHSV8(uint8_t h,uint8_t s,uint8_t v){
  uint8_t region=h/43; 
  uint8_t rem=(h-region*43)*6; 
  uint8_t p=(uint16_t)v*(255-s)/255;
  uint8_t q=(uint16_t)v*(255-((uint16_t)s*rem)/255)/255; 
  uint8_t t=(uint16_t)v*(255-((uint16_t)s*(255-rem))/255)/255;
  uint8_t r,g,b; 
  switch(region){ 
    default: 
    case 0:r=v; g=t; b=p; break; 
    case 1:r=q; g=v; b=p; break; 
    case 2:r=p; g=v; b=t; break; 
    case 3:r=p; g=q; b=v; break; 
    case 4:r=t; g=p; b=v; break; 
    case 5:r=v; g=p; b=q; break; 
  }
  return Matrix.Color(r,g,b);
}



void Ripple_PlayOnce(){
  
  const uint8_t LEVELS=12; 
  const float SPEED=0.14f; 
  const float SPACING=0.85f; 
  const float SIGMA=0.55f; 
  const int RINGS=4; 
  const float MAX_DIST=4.95f;
  const float PERIOD=MAX_DIST+(RINGS-1)*SPACING+2.f*SIGMA;
  
  uint8_t originalBrightness = Matrix.getBrightness();
  Matrix.setBrightness(gMotionBrightness);
  uint8_t localHue = gMotionHue;
  const float cx=(Matrix.width()-1)*0.5f; 
  const float cy=(Matrix.height()-1)*0.5f; 
  float t=0.f;
  
  while(t<=PERIOD){
    for(int y=0;y<Matrix.height();++y){ 
      for(int x=0;x<Matrix.width();++x){ 
        float dx=x-cx, dy=y-cy; 
        float dist=sqrtf(dx*dx+dy*dy); 
        float amp=0.f; 
        for(int k=0;k<RINGS;k++){ 
          float r=t-k*SPACING; 
          float d=dist-r; 
          amp+=expf(-(d*d)/(2.f*SIGMA*SIGMA)); 
        } 
        if(amp>1.f) amp=1.f; 
        float stepped=floorf(amp*LEVELS)/LEVELS; 
        float satf=0.90f-0.25f*(dist/4.8f); 
        if(satf<0) satf=0; 
        if(satf>1) satf=1; 
        uint8_t V=gamma8(stepped*0.9f); 
        V=(uint8_t)((V*250+127)/255); 
        uint8_t S=(uint8_t)(satf*255.f+0.5f); 
        uint16_t c=ColorHSV8(localHue,S,V); 
        Matrix.drawPixel(x,y,c);
      } 
    }
    Matrix.show(); 
    delay(20); 
    t+=SPEED; 
  }

  for(int b=gMotionBrightness; b>=0; b-=2){ 
    Matrix.setBrightness(b); 
    Matrix.show(); 
    delay(18);
  } 
  Matrix.fillScreen(0); 
  Matrix.show();
  Matrix.setBrightness(originalBrightness);
}

void DiagonalWave_PlayOnce(){
  
  const uint8_t LEVELS = 12;
  const float SIGMA = 0.8f;
  const float SPEED = 0.18f;
  
  const float CENTER = (Matrix.width() - 1) * 0.5f;
  const float MARGIN = 2.5f;
  const float START_LEFT = -MARGIN;
  const float START_RIGHT = (Matrix.width() - 1) + MARGIN;
  const float HALF_DIST = CENTER + MARGIN;
  const float TOTAL_DIST = HALF_DIST * 2.0f;
  
  uint8_t originalBrightness = Matrix.getBrightness();
  Matrix.setBrightness(gMotionBrightness);
  uint8_t localHue = gMotionHue;

  float t = 0.f;

  while(t <= TOTAL_DIST){
    float posLeft = START_LEFT + t;
    float posRight = START_RIGHT - t;

    for(int y=0; y<Matrix.height(); ++y){ 
      for(int x=0; x<Matrix.width(); ++x){ 
        float fx = (float)x;
        
        float distL = fx - posLeft;
        float ampL = expf(-(distL*distL) / (2.f * SIGMA * SIGMA));
        
        float distR = fx - posRight;
        float ampR = expf(-(distR*distR) / (2.f * SIGMA * SIGMA));
        
        float amp = ampL + ampR;
        if(amp > 1.f) amp = 1.f;
        
        float stepped = floorf(amp * LEVELS) / LEVELS;
        
        float satf = 0.90f - 0.25f * amp;
        if(satf < 0.f) satf = 0.f;
        if(satf > 1.f) satf = 1.f;
        
        uint8_t V = gamma8(stepped * 0.9f);
        V = (uint8_t)((V * 250 + 127) / 255);
        
        uint8_t S = (uint8_t)(satf * 255.f + 0.5f);
        
        uint16_t c = ColorHSV8(localHue, S, V); 
        Matrix.drawPixel(x, y, c);
      } 
    }
    Matrix.show(); 
    delay(20); 
    
    t += SPEED; 
  }
  
  for(int b=gMotionBrightness; b>=0; b-=2){ 
    Matrix.setBrightness(b); 
    Matrix.show(); 
    delay(18);
  } 
  Matrix.fillScreen(0); 
  Matrix.show();
  Matrix.setBrightness(originalBrightness);
  Matrix.show();
}

// 非ブロッキング レーダー（色相固定）
static float sRadarAngleDeg=0.f; 
static bool sRadarActive=false; 
const float RADAR_SPEED=2.5f; 
const float BW_F_IDLE=0.8f; 
const float BW_B_IDLE=0.05f; 
const uint8_t FADE_IDLE=10;

void Radar_InitIdle(){ 
  sRadarAngleDeg=0.f; 
  Matrix.fillScreen(0); 
  Matrix.setBrightness(gMotionBrightness); 
  Matrix.show(); 
  sRadarActive=true; 
}

void Radar_IdleStep(bool doShow){ 
  if(!sRadarActive) return; 
  
  for(int i=0;i<Matrix.width()*Matrix.height();++i){ 
    uint32_t col=Matrix.getPixelColor(i); 
    if(col){ 
      uint8_t r=(col>>16)&0xFF,g=(col>>8)&0xFF,b=col&0xFF; 
      r=(r<=FADE_IDLE)?0:r-FADE_IDLE; 
      g=(g<=FADE_IDLE)?0:g-FADE_IDLE; 
      b=(b<=FADE_IDLE)?0:b-FADE_IDLE; 
      Matrix.setPixelColor(i,Matrix.Color(r,g,b)); 
    }
  } 
  
  const float cx=(Matrix.width()-1)*0.5f, cy=(Matrix.height()-1)*0.5f; 
  float rad=sRadarAngleDeg*(float)M_PI/180.f; 
  
  for(uint8_t y=0;y<Matrix.height();++y){ 
    for(uint8_t x=0;x<Matrix.width();++x){ 
      float dx=x-cx,dy=y-cy; 
      float pr=atan2f(dy,dx); 
      float diff=rad-pr; 
      while(diff>M_PI) diff-=2.f*M_PI; 
      while(diff<-M_PI) diff+=2.f*M_PI; 
      float bw=(diff>0)?BW_F_IDLE:BW_B_IDLE; 
      float br=expf(-(diff*diff)/(2.f*bw*bw)); 
      if(br>0.05f){ 
        uint8_t V=gamma8(br); 
        uint16_t c=ColorHSV8(gMotionHue,255,V); 
        Matrix.drawPixel(x,y,c);
      } 
    }
  } 
  
  if(doShow) Matrix.show(); 
  sRadarAngleDeg+=RADAR_SPEED; 
  if(sRadarAngleDeg>=360.f){ 
    sRadarAngleDeg-=360.f; 
  }
}
