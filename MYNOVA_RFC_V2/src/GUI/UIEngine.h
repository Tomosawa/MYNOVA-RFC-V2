/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#ifndef UIEngine_h
#define UIEngine_h

#include "UIPage.h"
#include <Arduino.h>
#include <stack>
#include <vector>
#include <U8g2lib.h>
#include "Animation/Animation.h"

// UIEngine —— 导航栈引擎(仅 UIKernel 任务调用, 单线程化后无跨任务并发)
// 导航语义:
//   navigateTo: 当前页压栈 → 进入新页(新页由调用方new, PSRAM分配)
//   navigateBack: 弹栈回退, 被回退的页面延迟删除
//   页面删除一律走 markForDeletion → 帧末 processPendingDeletions
class UIEngine {
public:
  UIEngine();
  ~UIEngine();
  void render(U8G2* u8g2);
  void update();
  void navigateTo(UIPage* page, AnimationType aniType = ANIME_NONE);
  UIPage* navigateBack(AnimationType aniType = ANIME_NONE);
  void navigateBack(UIPage* targetPage, AnimationType aniType = ANIME_NONE);
  UIPage* navigateBackSteps(int steps, AnimationType aniType = ANIME_NONE);  // 返回到堆栈中的第N个页面
  UIPage* getCurrentPage();
  void setCurrentPage(UIPage* page);

  // 延迟删除：在渲染帧结束后安全删除
  void markForDeletion(UIPage* page);
  void processPendingDeletions();

private:
  UIPage* currentPage;
  UIPage* nextPage;
  std::stack<UIPage*> pages;
  std::vector<UIPage*> pagesToDelete; // 待删除的页面队列

  // 统一切换内核(P5.1去重): 三个navigateBack与navigateTo共用
  // deleteCurrent=true时旧页(动画完成后或立即)进入延迟删除队列
  void transitionTo(UIPage* targetPage, AnimationType aniType, bool deleteCurrent);
};

#endif
