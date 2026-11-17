/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include "AnimationEngine.h"

// 创建全局动画引擎实例
AnimationEngine animationEngine;

// AnimationEngine 类实现
AnimationEngine::AnimationEngine() {
  // 递归互斥锁: update()的动画完成回调里可能再addAnimation(同任务重入)
  mutex = xSemaphoreCreateRecursiveMutex();
}

AnimationEngine::~AnimationEngine() {
  this->clearAnimations();
  if (mutex != nullptr) {
    vSemaphoreDelete(mutex);
    mutex = nullptr;
  }
}

// 修改 update 方法，使其返回 bool 类型
void AnimationEngine::update() {
  if (mutex == nullptr) return;
  if (xSemaphoreTakeRecursive(mutex, portMAX_DELAY) != pdTRUE) return;

  if (this->animations.empty()) {
    xSemaphoreGiveRecursive(mutex);
    return;
  }

  // 创建当前动画列表的副本（避免在遍历时因回调添加新动画而导致迭代器失效）
  std::vector<Animation*> currentAnimations = this->animations;
  std::vector<Animation*> toDelete;

  // 遍历并更新所有动画（使用副本）
  for (auto anim : currentAnimations) {
    // 检查动画是否仍在列表中（可能已被其他操作删除）
    if (std::find(this->animations.begin(), this->animations.end(), anim) != this->animations.end()) {
      anim->update();

      // 如果动画已完成，标记为待删除
      if (anim->getState() == ANIME_STATE_FINISHED) {
        toDelete.push_back(anim);
      }
    }
  }

  // 删除已完成的动画
  for (auto anim : toDelete) {
    auto it = std::find(this->animations.begin(), this->animations.end(), anim);
    if (it != this->animations.end()) {
      this->animations.erase(it);
    }
    delete anim;
  }

  xSemaphoreGiveRecursive(mutex);
}

void AnimationEngine::addAnimation(Animation* animation) {
  if (mutex == nullptr) {
    // 极早期调用(构造前): 直接加入, 此时无并发
    if (animation != nullptr) {
      this->animations.push_back(animation);
      animation->start();
    }
    return;
  }
  if (xSemaphoreTakeRecursive(mutex, portMAX_DELAY) != pdTRUE) return;
  if (animation != nullptr) {
    this->animations.push_back(animation);
    animation->start();
  }
  xSemaphoreGiveRecursive(mutex);
}

void AnimationEngine::removeAnimation(Animation* animation) {
  if (mutex == nullptr) return;
  if (xSemaphoreTakeRecursive(mutex, portMAX_DELAY) != pdTRUE) return;
  auto it = std::find(this->animations.begin(), this->animations.end(), animation);
  if (it != this->animations.end()) {
    delete *it; // 删除动画对象
    this->animations.erase(it); // 从列表中移除
  }
  xSemaphoreGiveRecursive(mutex);
}

void AnimationEngine::clearAnimations() {
  if (mutex == nullptr) {
    animations.clear();
    return;
  }
  if (xSemaphoreTakeRecursive(mutex, portMAX_DELAY) != pdTRUE) return;
  for (auto anim : animations) {
    delete anim;
  }
  animations.clear();
  xSemaphoreGiveRecursive(mutex);
}

bool AnimationEngine::isAnimating() {
  if (mutex == nullptr) return !animations.empty();
  if (xSemaphoreTakeRecursive(mutex, portMAX_DELAY) != pdTRUE) return false;
  bool animating = !this->animations.empty();
  xSemaphoreGiveRecursive(mutex);
  return animating;
}
