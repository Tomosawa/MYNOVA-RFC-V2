/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef UIMenu_h
#define UIMenu_h

#include "UIWidget.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include <vector>
#include "UINavBar.h"

class UIMenu : public UIWidget {
public:
  UIMenu(int x, int y, int width, int height, int menuLines);
  ~UIMenu() override;
  void render(U8G2* u8g2,int offsetX = 0, int offsetY = 0) override;
  int selectIndex();
  void addMenuItem(String item);
  void removeMenuItem(int iIndex);
  void clear();
  size_t itemCount() const;
  String getMenuItem(int idx) const;
  void moveUp();
  void moveDown();
  UINavBar* getNavBar();
  void setRadioSelected(int idx); // -1 = no radio
  void setShowRadio(bool show);
public:
    bool bShowBtnTips = true;
    bool bShowTitle = true;
    bool bShowBorder = false;
    bool bShowScrollBar = true;
    const uint8_t* titleFont = u8g2_font_wqy12_t_gb2312;
    const uint8_t* menuFont = u8g2_font_wqy12_t_gb2312;
    int menuLines = 5;
    int menuItemLines = 3;
    int menuPos = 0;
    int menuSel = 0;
    int menuSelY = 0;
    int menuStart = 0;
    int marginLeft = 3;
    int marginRight = 3;
    int menuLineHeight;
    String titleText;
    // Radiobox support: hollow circle = unselected, solid disc = selected
    bool bShowRadio = false;
    int radioSelected = -1; // item index that is checked
private:
  std::vector<String> items;
  UINavBar* navBar;
};

#endif
