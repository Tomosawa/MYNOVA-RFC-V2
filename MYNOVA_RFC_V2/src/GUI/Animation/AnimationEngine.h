/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef AnimationEngine_h
#define AnimationEngine_h

#include <Arduino.h>
#include <U8g2lib.h>
#include "Animation.h"
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// 动画引擎类
// 注意: addAnimation可能被按键任务调用(onButtonXxx里showBlink),
// update/clearAnimations在渲染任务调用 -- 必须互斥保护, 否则vector并发操作损坏堆
class AnimationEngine {
public:
  AnimationEngine();
  ~AnimationEngine();
  
  // 更新所有动画(渲染任务调用; 动画回调内的addAnimation同任务, 递归锁保护)
  void update();
  // 添加动画(任意任务可调用)
  void addAnimation(Animation* animation);
  // 移除动画(任意任务可调用)
  void removeAnimation(Animation* animation);
  // 清除所有动画(任意任务可调用)
  void clearAnimations();
  // 是否有动画正在运行
  bool isAnimating();
  
private:
  std::vector<Animation*> animations; // 当前动画
  SemaphoreHandle_t mutex = nullptr;  // 递归互斥锁(update回调内可能再addAnimation)
};

extern AnimationEngine animationEngine;

#endif