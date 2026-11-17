/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include "UIPage.h"
#include "../Util/MemAlloc.h"

// ── PSRAM new/delete 实现: 失败降级内部RAM(全失败时new抛abort, 由系统兜底) ──
void* UIPage::operator new(size_t sz) {
    void* p = psramMalloc(sz);
    if (p == nullptr) p = ramMalloc(sz);
    return p;  // 两者都失败时返回nullptr→new表达式内部abort(与全局new行为一致)
}
void UIPage::operator delete(void* p) { memFree(p); }
void* UIPage::operator new[](size_t sz) {
    void* p = psramMalloc(sz);
    if (p == nullptr) p = ramMalloc(sz);
    return p;
}
void UIPage::operator delete[](void* p) { memFree(p); }

UIPage::UIPage(int x, int y, int width, int height) {
  // 构造函数
  pageX = x;
  pageY = y;
  pageWidth = width;
  pageHeight = height;
}

UIPage::~UIPage() {
  // 只释放页面拥有的widget(owned=true), 借引用的(owned=false)不动
  for (size_t i = 0; i < widgets.size(); i++) {
    auto widget = widgets[i];
    bool owned = (i < ownedWidgets.size()) ? ownedWidgets[i] : true;
    if (widget != nullptr && owned) {
      delete widget;
      widgets[i] = nullptr;
    }
    // 每隔几个对象喂一次看门狗
    if (i % 5 == 0) {
      yield();
    }
  }
  widgets.clear();
  ownedWidgets.clear();
}

void UIPage::addWidget(UIWidget* widget, bool owned) {
  this->widgets.push_back(widget);
  this->ownedWidgets.push_back(owned);
}

void UIPage::render(U8G2* u8g2) {
  // 渲染所有Widget
  for (auto widget : widgets) {
    widget->render(u8g2, pageX, pageY);
  }
}

// 实现showPage函数，默认不执行任何操作
void UIPage::showPage() {
  // 默认实现为空，子类可以重写此方法以执行页面显示时的初始化操作
}

// 实现update函数，默认不执行任何操作
void UIPage::update() {
  // 默认实现为空，子类可以重写此方法以执行页面更新逻辑
}

// 按钮事件处理函数(子类重写实现业务; 原C函数指针回调机制已删除)
void UIPage::onButtonBack(void* context) { (void)context; }
void UIPage::onButtonMenu(void* context) { (void)context; }
void UIPage::onButtonEnter(void* context) { (void)context; }
void UIPage::onButton1(void* context) { (void)context; }
void UIPage::onButton2(void* context) { (void)context; }
void UIPage::onButton3(void* context) { (void)context; }
void UIPage::onButton4(void* context) { (void)context; }
void UIPage::onButton5(void* context) { (void)context; }
void UIPage::onButton6(void* context) { (void)context; }
void UIPage::onButton7(void* context) { (void)context; }
void UIPage::onButton8(void* context) { (void)context; }
void UIPage::onButton9(void* context) { (void)context; }

// 5键按下/释放沿(PTT): 基类空实现, 对讲页重写
void UIPage::onButton5Down(void* context) { (void)context; }
void UIPage::onButton5Up(void* context) { (void)context; }
