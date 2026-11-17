/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "UIMenu.h"
#include <U8g2lib.h>
#include "../Animation/MenuCursorAnimation.h"
#include "../Animation/AnimationEngine.h"

UIMenu::UIMenu(int x, int y, int width, int height,int menuLines)
{
    this->x = x;
    this->y = y;
    this->height = height;
    this->width = width;
    this->menuLines = menuLines;
    
    menuPos = 0;
    menuSel = 0;
    menuStart = 0;
    menuSelY = 0;
    marginLeft = 3;
    marginRight = 3;
    menuLineHeight = round(height * 1.0 / menuLines);

    int navBarHeight = menuLineHeight;
    navBar = new UINavBar(x, y + height - navBarHeight, width, navBarHeight);
    navBar->setMargin(marginLeft, marginRight);
    bShowRadio = false;
    radioSelected = -1;
}

UIMenu::~UIMenu()
{
    delete navBar;
}

int UIMenu::selectIndex()
{
    return menuSel;
}

void UIMenu::addMenuItem(String item)
{
    items.push_back(item);
}

void UIMenu::removeMenuItem(int iIndex)
{
    if (iIndex < 0 || iIndex >= (int)items.size()) return;
    items.erase(items.begin() + iIndex);
}

void UIMenu::clear() {
    items.clear();
    menuPos = 0;
    menuSel = 0;
    menuStart = 0;
    menuSelY = 0;
    radioSelected = -1;
    // keep bShowRadio flag — caller controls it
}

size_t UIMenu::itemCount() const { return items.size(); }
String UIMenu::getMenuItem(int idx) const { if (idx < 0 || idx >= (int)items.size()) return String(); return items[idx]; }

void UIMenu::setRadioSelected(int idx) { radioSelected = idx; }
void UIMenu::setShowRadio(bool show) { bShowRadio = show; }

void UIMenu::moveUp()
{
    int itemLines = menuLines;
    if (bShowTitle) itemLines--;
    if (bShowBtnTips) itemLines--;
    if (menuPos > 0)
    {
        menuPos--;
    }
    else if(menuStart > 0)
    {
        menuStart--;
    }
    else
    {
        int n = (int)items.size();
        if (n <= itemLines) { menuPos = n > 0 ? n - 1 : 0; }
        else { menuStart = n - itemLines; menuPos = itemLines - 1; }
    }
    MenuCursorAnimation* anim = new MenuCursorAnimation(this, menuSelY, menuPos * menuLineHeight, 100);
    animationEngine.addAnimation(anim);
}

void UIMenu::moveDown()
{
    int itemLines = menuLines;
    if (bShowTitle) itemLines--;
    if (bShowBtnTips) itemLines--;
    if(menuPos + 1 < itemLines)
    {
        if (menuPos + 1 < (int)items.size() - menuStart)
            menuPos++;
        else {
            menuStart = 0;
            menuPos = 0;
        }
    }
    else if((int)menuStart + itemLines < (int)items.size())
    {
        menuStart++;
    }
    else
    {
        menuStart = 0;
        menuPos = 0;
    }

    MenuCursorAnimation* anim = new MenuCursorAnimation(this, menuSelY, menuPos * menuLineHeight, 100);
    animationEngine.addAnimation(anim);
}

UINavBar* UIMenu::getNavBar() {
    return navBar;
}

void UIMenu::render(U8G2* u8g2,int offsetX, int offsetY) {
    if (!bVisible) return;
    
    uint8_t oldColor = u8g2->getDrawColor();
    u8g2->setDrawColor(0);
    u8g2->drawBox(offsetX + x, offsetY + y, width, height);
    u8g2->setDrawColor(oldColor);

    if(bShowBorder)
    {
        u8g2->drawFrame(offsetX + x, offsetY + y, width, height);
        if(bShowTitle)
            u8g2->drawLine(offsetX + x, offsetY + y + menuLineHeight, x + width, y + menuLineHeight);
        if(bShowBtnTips)
            u8g2->drawLine(offsetX + x, offsetY + y + height - menuLineHeight, x + width, y + height - menuLineHeight);
    }
    if(bShowTitle)
    {
        u8g2->setFont(titleFont);
        int8_t fontHeight = u8g2->getMaxCharHeight();
        uint16_t strWidth = u8g2->getUTF8Width(titleText.c_str());
        u8g2->drawUTF8(offsetX + x + (width / 2 - strWidth / 2) , offsetY + y + menuLineHeight / 2 + fontHeight / 2, titleText.c_str());
    }
    
    int itemStartY = y;
    if(bShowTitle)
        itemStartY += menuLineHeight;

    u8g2->setFontMode(1);
    u8g2->setDrawColor(1);
    u8g2->drawBox(offsetX + x, offsetY + itemStartY + 1 + menuSelY, width, menuLineHeight - 1);
    u8g2->setDrawColor(2);
    u8g2->setFont(menuFont);
    int strLineOffset = 0;
    int8_t maxCharHeight = u8g2->getMaxCharHeight();
    strLineOffset = menuLineHeight / 2 + maxCharHeight / 2 - 1;
    menuItemLines = menuLines;
    if(bShowTitle)
        menuItemLines--;
    if(bShowBtnTips)
        menuItemLines--;
    for(int i = 0; i < menuItemLines; i++)
    {
        int idx = menuStart + i;
        if (idx < 0 || idx >= (int)items.size()) continue;
        // Radiobox: 标准单选样式 — 未选中=空心圆, 选中=实心圆
        // 注意: u8g2官方明确 drawCircle/drawDisc 不支持 drawColor=2(XOR),
        // XOR下disc内部重叠的VLine会在对角线方向互相抵消, 画出"四叶草"。
        // 必须按该行是否被光标白条覆盖显式选色: 黑底白圆 / 白底黑圆。
        if (bShowRadio) {
            int r = 4;  // 直径 2r+1 = 9px
            if (r * 2 + 2 > menuLineHeight) r = menuLineHeight / 2 - 2;
            if (r < 3) r = 3;
            int cx = offsetX + x + marginLeft + 1 + r;
            int cy = offsetY + y + itemStartY + menuLineHeight * i + menuLineHeight / 2;
            // 行区间与光标白条区间求交(含100ms移动动画过渡态, 交叠时按白底色处理)
            int rowTop = offsetY + y + itemStartY + menuLineHeight * i;
            int rowBot = rowTop + menuLineHeight - 1;
            int boxTop = offsetY + y + itemStartY + 1 + menuSelY;
            int boxBot = boxTop + menuLineHeight - 2;
            bool onHighlight = (boxTop <= rowBot && boxBot >= rowTop);
            u8g2->setDrawColor(onHighlight ? 0 : 1);  // 白条上黑色, 黑底上白色
            u8g2->drawCircle(cx, cy, r); // 未选中: 空心圆
            if (idx == radioSelected) {
                u8g2->drawDisc(cx, cy, r - 2);   // 选中: 实心圆
            } 
            // 文字仍用XOR(字形像素不重叠, 反色正常)
            u8g2->setDrawColor(2);
            int textX = offsetX + x + marginLeft + r * 2 + 4;
            u8g2->drawUTF8(textX, offsetY + y + itemStartY + menuLineHeight * i + strLineOffset, items[idx].c_str());
        } else {
            u8g2->drawUTF8(offsetX + x + marginLeft, offsetY + y + itemStartY + menuLineHeight * i + strLineOffset, items[idx].c_str());
        }
    }
    
    u8g2->setFontMode(0);
    u8g2->setDrawColor(1);
    if(bShowBtnTips)
    {
       navBar->render(u8g2, offsetX, offsetY);
    }

    menuSel = menuStart + menuPos;
}

