/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "UIEditableNumber.h"

UIEditableNumber::UIEditableNumber() {
    mode = EDITABLE_DECIMAL;
    value = 0;
    maxDigits = 3;
    cursorPosition = 0;
    selected = false;
    editing = false;
    minValue = 0;
    maxValue = 0xFFFFFFFFFFFFFFFFULL;  // 默认最大值(uint64)
    x = 0;
    y = 0;
    width = 50;
    height = 12;
    bVisible = true;
}

void UIEditableNumber::render(U8G2* u8g2, int offsetX, int offsetY) {
    if (!bVisible) return;
    
    int actualX = offsetX + x;
    int actualY = offsetY + y + 1;
    
    u8g2->setFont(textFont);
    
    // 获取显示字符串
    String displayStr = getDisplayString();
    
    // 计算文字Y坐标（参考UILabel的实现）
    int fontHeight = u8g2->getMaxCharHeight();
    int textY = actualY + (height / 2) + (fontHeight / 2) - 2;
    
    // 字段选中但未进入编辑：实心填充（与 UISelectValue 选中态一致）
    if (selected && !editing) {
        u8g2->setDrawColor(1);
        u8g2->drawBox(actualX - 1, actualY, width, height);
        u8g2->setDrawColor(0);
        u8g2->drawStr(actualX, textY, displayStr.c_str());
        u8g2->setDrawColor(1);
        return;
    }

    // 逐位编辑模式：空心框 + 当前位高亮
    if (selected && editing) {
        // 绘制一个浅色的边框表示选中
        u8g2->drawFrame(actualX - 1, actualY, width, height);
    }

    // 绘制文本
    u8g2->drawStr(actualX, textY, displayStr.c_str());

    // 绘制当前编辑位的高亮
    if (selected && editing) {
        // 计算当前编辑位的字符位置
        int charIndex = 0;
        if (mode == EDITABLE_HEX) {
            // 十六进制模式：格式如 "7A 3B DC"
            // cursorPosition 0 = 最右边的C (DC的C)
            // cursorPosition 1 = 最右边的D (DC的D)
            // cursorPosition 2 = 3B的B
            // cursorPosition 3 = 3B的3
            // 每个字节2个字符，字节间有空格
            int byteIndex = cursorPosition / 2;  // 第几个字节
            int digitInByte = cursorPosition % 2; // 字节内的位置（0=高位，1=低位）
            
            // 从右往左数字节
            int totalBytes = (maxDigits + 1) / 2;
            int byteFromLeft = totalBytes - 1 - byteIndex;
            
            // 计算字符索引：每个字节3个字符（2个hex + 1个空格），但最后一个字节没有空格
            charIndex = byteFromLeft * 3 + (1 - digitInByte);
            if (byteFromLeft == totalBytes - 1) {
                charIndex = byteFromLeft * 3 + (1 - digitInByte);
            }
        } else {
            // 十进制模式：直接从右往左数
            charIndex = displayStr.length() - 1 - cursorPosition;
        }
        
        if (charIndex >= 0 && charIndex < displayStr.length()) {
            // 计算该字符的位置
            String leftPart = displayStr.substring(0, charIndex);
            String highlightChar = displayStr.substring(charIndex, charIndex + 1);
            
            int leftWidth = u8g2->getStrWidth(leftPart.c_str());
            int charWidth = u8g2->getStrWidth(highlightChar.c_str());
            
            // 反色显示当前字符
            u8g2->setDrawColor(2); // XOR模式
            u8g2->drawBox(actualX + leftWidth, actualY + 1, charWidth + 2, height - 2);
            u8g2->setDrawColor(1); // 恢复正常模式
        }
    }
}

void UIEditableNumber::setMode(EditableNumberMode mode) {
    this->mode = mode;
}

void UIEditableNumber::setValue(uint64_t value) {
    // 确保值在范围内
    if (value < minValue) {
        this->value = minValue;
    } else if (value > maxValue) {
        this->value = maxValue;
    } else {
        this->value = value;
    }
}

uint64_t UIEditableNumber::getValue() {
    return value;
}

void UIEditableNumber::setMaxDigits(int maxDigits) {
    this->maxDigits = maxDigits;
}

void UIEditableNumber::setMinValue(uint64_t minValue) {
    this->minValue = minValue;
    // 确保当前值在范围内
    if (value < minValue) {
        value = minValue;
    }
}

void UIEditableNumber::setMaxValue(uint64_t maxValue) {
    this->maxValue = maxValue;
    // 确保当前值在范围内
    if (value > maxValue) {
        value = maxValue;
    }
}

void UIEditableNumber::setRange(uint64_t minValue, uint64_t maxValue) {
    this->minValue = minValue;
    this->maxValue = maxValue;
    // 确保当前值在范围内
    if (value < minValue) {
        value = minValue;
    } else if (value > maxValue) {
        value = maxValue;
    }
}

void UIEditableNumber::moveCursorLeft() {
    cursorPosition++;
    if (cursorPosition >= maxDigits) {
        cursorPosition = maxDigits - 1;
    }
}

void UIEditableNumber::moveCursorRight() {
    cursorPosition--;
    if (cursorPosition < 0) {
        cursorPosition = 0;
    }
}

void UIEditableNumber::incrementDigit() {
    int digit = getDigitAt(cursorPosition);
    int maxDigitValue = (mode == EDITABLE_HEX) ? 15 : 9;

    digit++;
    if (digit > maxDigitValue) {
        digit = 0;
    }

    // 临时保存旧值
    uint64_t oldValue = value;
    setDigitAt(cursorPosition, digit);
    
    // 检查是否超出范围
    if (value > maxValue) {
        // 超出最大值，恢复旧值
        value = oldValue;
    }
}

void UIEditableNumber::decrementDigit() {
    int digit = getDigitAt(cursorPosition);
    int maxDigitValue = (mode == EDITABLE_HEX) ? 15 : 9;

    digit--;
    if (digit < 0) {
        digit = maxDigitValue;
    }

    // 临时保存旧值
    uint64_t oldValue = value;
    setDigitAt(cursorPosition, digit);
    
    // 检查是否超出范围
    if (value < minValue) {
        // 低于最小值，恢复旧值
        value = oldValue;
    }
}

void UIEditableNumber::setSelected(bool selected) {
    this->selected = selected;
    if (!selected) this->editing = false;
}

void UIEditableNumber::setEditing(bool editing) {
    this->editing = editing;
}

bool UIEditableNumber::isEditing() {
    return editing;
}

bool UIEditableNumber::isSelected() {
    return selected;
}

bool UIEditableNumber::isAtLeftBoundary() {
    return cursorPosition >= (maxDigits - 1);
}

bool UIEditableNumber::isAtRightBoundary() {
    return cursorPosition <= 0;
}

int UIEditableNumber::getDigitAt(int position) {
    if (mode == EDITABLE_HEX) {
        // 十六进制，每位是16进制
        if (position >= 16) return 0;
        return (value >> (position * 4)) & 0xF;
    } else {
        // 十进制
        uint64_t divisor = 1;
        for (int i = 0; i < position; i++) {
            divisor *= 10;
        }
        return (value / divisor) % 10;
    }
}

void UIEditableNumber::setDigitAt(int position, int digit) {
    int oldDigit = getDigitAt(position);

    if (mode == EDITABLE_HEX) {
        if (position >= 16) return;
        uint64_t mask = (uint64_t)0xF << (position * 4);
        value = (value & ~mask) | ((uint64_t)(digit & 0xF) << (position * 4));
    } else {
        uint64_t divisor = 1;
        for (int i = 0; i < position; i++) {
            divisor *= 10;
        }
        value = value - (uint64_t)oldDigit * divisor + (uint64_t)digit * divisor;
    }
}

String UIEditableNumber::formatDisplay() {
    return getDisplayString();
}

String UIEditableNumber::getDisplayString() {
    if (mode == EDITABLE_HEX) {
        // 十六进制模式，显示为 "7A 3B DC" 格式（高位在左，不足补0）
        static const char HEXCH[] = "0123456789ABCDEF";
        String hex = "";
        for (int i = maxDigits - 1; i >= 0; i--) {
            hex += HEXCH[(value >> (i * 4)) & 0xF];
        }

        // 每两个字符加一个空格
        String formatted = "";
        for (int i = 0; i < hex.length(); i += 2) {
            if (i > 0) formatted += " ";
            formatted += hex.substring(i, i + 2);
        }

        return formatted;
    } else {
        // 十进制模式（不足补0）
        char buf[24];
        int len = 0;
        uint64_t v = value;
        if (v == 0) {
            buf[len++] = '0';
        } else {
            char rev[24];
            int rlen = 0;
            while (v > 0 && rlen < (int)sizeof(rev)) {
                rev[rlen++] = '0' + (v % 10);
                v /= 10;
            }
            while (rlen > 0) buf[len++] = rev[--rlen];
        }
        String dec = "";
        for (int i = len; i < maxDigits; i++) dec += "0";
        for (int i = 0; i < len; i++) dec += buf[i];
        return dec;
    }
}

