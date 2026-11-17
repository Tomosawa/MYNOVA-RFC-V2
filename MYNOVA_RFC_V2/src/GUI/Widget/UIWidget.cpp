/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "UIWidget.h"
#include "../../Util/MemAlloc.h"

// ── PSRAM new/delete: 失败降级内部RAM(同 UIPage 策略) ──
void* UIWidget::operator new(size_t sz) {
    void* p = psramMalloc(sz);
    if (p == nullptr) p = ramMalloc(sz);
    return p;
}
void UIWidget::operator delete(void* p) { memFree(p); }
void* UIWidget::operator new[](size_t sz) {
    void* p = psramMalloc(sz);
    if (p == nullptr) p = ramMalloc(sz);
    return p;
}
void UIWidget::operator delete[](void* p) { memFree(p); }

UIWidget::UIWidget() {
  // 初始化默认位置和大小
  x = 0;
  y = 0;
  width = 0;
  height = 0;
  margin_left = 0;
  margin_top = 0; 
  margin_right = 0;
  margin_bottom = 0;
  padding_left = 0;
  padding_top = 0;
  padding_right = 0; 
  padding_bottom = 0;
  bVisible = true;  // 默认可见
}

UIWidget::~UIWidget() {
  // 析构函数实现
}

void UIWidget::render(U8G2* u8g2,int offsetX, int offsetY) {

}