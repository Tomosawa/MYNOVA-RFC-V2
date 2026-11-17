/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#ifndef UIPage_h
#define UIPage_h

#include <Arduino.h>
#include <vector>
#include <U8g2lib.h>
#include "Widget/UIWidget.h"

class UIPage {
public:
  UIPage(int x, int y, int width, int height);
  virtual ~UIPage();

  // 添加Widget: owned=true 页面析构时负责delete(new出来的组件);
  // owned=false 页面只持引用不负责释放(成员对象/外部管理的组件)
  void addWidget(UIWidget* widget, bool owned = true);
  virtual void render(U8G2* u8g2);

  // 页面显示(进入)时回调
  virtual void showPage();

  // 页面更新函数, 由 UIKernel 每帧调用
  virtual void update();

  // 按钮事件虚函数(子类重写)
  virtual void onButtonBack(void* context = nullptr);
  virtual void onButtonMenu(void* context = nullptr);
  virtual void onButtonEnter(void* context = nullptr);
  virtual void onButton1(void* context = nullptr);
  virtual void onButton2(void* context = nullptr);
  virtual void onButton3(void* context = nullptr);
  virtual void onButton4(void* context = nullptr);
  virtual void onButton5(void* context = nullptr);
  virtual void onButton6(void* context = nullptr);
  virtual void onButton7(void* context = nullptr);
  virtual void onButton8(void* context = nullptr);
  virtual void onButton9(void* context = nullptr);
  // 5键按下/释放沿(PTT按住说话用): 基类空实现, 对讲页重写
  virtual void onButton5Down(void* context = nullptr);
  virtual void onButton5Up(void* context = nullptr);
  // 页面整体坐标偏移
  int pageX, pageY;
  int pageWidth, pageHeight;

  // ── PSRAM 对象池支持(规划§5.2): 动态创建的页面落 PSRAM,
  //    把内部RAM留给任务栈/DMA/网络栈。页面仅 UIKernel 单任务访问,
  //    无跨核/跨任务并发, PSRAM 上的访问是安全的。──
  static void* operator new(size_t sz);
  static void operator delete(void* p);
  static void* operator new[](size_t sz);
  static void operator delete[](void* p);

private:
  std::vector<UIWidget*> widgets;
  std::vector<bool> ownedWidgets;   // 与 widgets 一一对应的所有权标记
};

#endif
