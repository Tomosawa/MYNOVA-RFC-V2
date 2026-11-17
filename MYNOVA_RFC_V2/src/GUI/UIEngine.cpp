/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include "UIEngine.h"
#include "Animation/AnimationEngine.h"
#include "Animation/PageTransition.h"

UIEngine::UIEngine() {
  this->currentPage = nullptr;
  this->nextPage = nullptr;
}

UIEngine::~UIEngine() {
  // 清理堆栈中的所有页面
  while (!this->pages.empty()) {
    UIPage* page = this->pages.top();
    this->pages.pop();
    delete page;
  }

  // 清理待删除队列
  for (auto page : pagesToDelete) {
    delete page;
  }
  pagesToDelete.clear();

  // 清理当前页面
  if (this->currentPage != nullptr) {
    delete this->currentPage;
    this->currentPage = nullptr;
  }

  // 清理下一个页面
  if (this->nextPage != nullptr) {
    delete this->nextPage;
    this->nextPage = nullptr;
  }
}

// 标记页面为待删除(帧末统一释放, 防止渲染中途悬挂)
void UIEngine::markForDeletion(UIPage* page) {
  if (page != nullptr) {
    pagesToDelete.push_back(page);
  }
}

// 处理待删除的页面（渲染帧结束后调用, 仅UIKernel上下文）
void UIEngine::processPendingDeletions() {
  if (pagesToDelete.empty()) {
    return;
  }

  // 先清除所有动画，防止动画引用待删除的对象
  animationEngine.clearAnimations();

  for (auto page : pagesToDelete) {
    yield(); // 喂看门狗
    delete page;
  }

  pagesToDelete.clear();
}

void UIEngine::render(U8G2* u8g2) {

  if (this->currentPage != nullptr) {
    // 渲染当前页面
    this->currentPage->render(u8g2);
  }
  if (this->nextPage != nullptr) {
    // 渲染下一个页面内容(过渡动画期间)
    this->nextPage->render(u8g2);
  }

  // 渲染完成后，安全删除待删除的页面
  processPendingDeletions();
}

void UIEngine::update() {
  // 更新动画引擎
  animationEngine.update();

  // 更新当前页面
  if (this->currentPage != nullptr) {
    this->currentPage->update();
  }
}

// ── 统一切换内核(仅UIKernel调用) ─────────────────────────────
// 有动画: nextPage=target, PageTransition完成后由动画回调完成指针交接
// 无动画: 立即交接 + showPage + 旧页延迟删除
void UIEngine::transitionTo(UIPage* targetPage, AnimationType aniType, bool deleteCurrent) {
  if (targetPage == nullptr || targetPage == this->currentPage) {
    return;
  }

  if (aniType != ANIME_NONE && this->currentPage != nullptr) {
    this->nextPage = targetPage;
    // deleteCurrent=true: 动画完成后旧页进入延迟删除队列
    PageTransition* pageTransition = new PageTransition(
        this->currentPage, this->nextPage, aniType, 300, deleteCurrent, this);
    animationEngine.addAnimation(pageTransition);
    return;
  }

  // 无动画: 直接切换
  UIPage* oldPage = this->currentPage;
  this->currentPage = targetPage;
  this->nextPage = nullptr;
  this->currentPage->showPage();
  if (deleteCurrent && oldPage != nullptr && oldPage != targetPage) {
    markForDeletion(oldPage);
  }
}

void UIEngine::navigateTo(UIPage* page, AnimationType aniType) {
  if (page == nullptr || page == this->currentPage) {
    return;
  }

  // 保存当前页面到堆栈
  if (this->currentPage != nullptr) {
    this->pages.push(this->currentPage);
  }

  // 进入新页面(新页接管显示, 旧页保留在栈中不删除)
  transitionTo(page, aniType, false);
}

UIPage* UIEngine::navigateBack(AnimationType aniType) {
  if (this->pages.empty()) {
    return this->currentPage;
  }

  // 弹出上一页并回退(旧页延迟删除)
  UIPage* prevPage = this->pages.top();
  this->pages.pop();
  transitionTo(prevPage, aniType, true);

  return this->currentPage;
}

void UIEngine::navigateBack(UIPage* targetPage, AnimationType aniType) {
  if (targetPage == nullptr || targetPage == this->currentPage) {
    return;
  }

  // 弹栈直到找到目标页(栈顶→目标页之间的页面全部丢弃)
  std::stack<UIPage*> tempStack;
  bool found = false;
  while (!this->pages.empty()) {
    UIPage* page = this->pages.top();
    this->pages.pop();
    if (page == targetPage) {
      found = true;
      break;
    }
    tempStack.push(page);
  }

  if (!found) {
    // 未找到目标: 恢复堆栈
    while (!tempStack.empty()) {
      this->pages.push(tempStack.top());
      tempStack.pop();
    }
    return;
  }

  // 跳过的页面全部延迟删除, 回退到目标页
  while (!tempStack.empty()) {
    UIPage* pageToDelete = tempStack.top();
    tempStack.pop();
    markForDeletion(pageToDelete);
  }
  transitionTo(targetPage, aniType, true);
}

UIPage* UIEngine::navigateBackSteps(int steps, AnimationType aniType) {
  if (steps <= 0 || this->pages.empty()) {
    return this->currentPage;
  }

  // 步数超过堆栈深度则回退到栈底
  int stackSize = (int)this->pages.size();
  if (steps > stackSize) {
    steps = stackSize;
  }

  // 弹出steps个页面: 最后弹出的为目标页, 其余延迟删除
  std::vector<UIPage*> dropped;
  UIPage* targetPage = nullptr;
  for (int i = 0; i < steps; i++) {
    if (this->pages.empty()) {
      break;
    }
    UIPage* page = this->pages.top();
    this->pages.pop();
    if (i == steps - 1) {
      targetPage = page;
    } else {
      dropped.push_back(page);
    }
  }

  if (targetPage == nullptr) {
    // 异常保护: 恢复堆栈
    for (auto it = dropped.rbegin(); it != dropped.rend(); ++it) {
      this->pages.push(*it);
    }
    return this->currentPage;
  }

  for (auto page : dropped) {
    markForDeletion(page);
  }
  transitionTo(targetPage, aniType, true);

  return this->currentPage;
}

UIPage* UIEngine::getCurrentPage() {
  return this->currentPage;
}

void UIEngine::setCurrentPage(UIPage* page) {
  this->currentPage = page;
  if (this->currentPage != nullptr) {
    this->currentPage->showPage();
  }
}
