/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "EditDataPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Services/DataStore.h"
#include "../../Drivers/RadioHelper.h"
#include "../../GUI/GUIRender.h"

extern UIEngine uiEngine;
extern DataStore dataStore;

EditDataPage::EditDataPage(int dataIdx, bool isNew) : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    dataIndex(dataIdx),
    isNewData(isNew),
    currentState(EDIT_FIELDS),
    currentFieldIndex(FIELD_FREQ),
    digitEditing(false),
    currentFreqIndex(RF_FREQ_COUNT_ALL - 1),  // 默认433.92MHz（最常用）
    currentBitLengthIndex(5) {
    
    // 加载或初始化数据
    if (!isNew) {
        currentData = dataStore.ReadSignal(dataIndex);
        
        // 设置当前选项索引
        currentFreqIndex = findFreqIndex(currentData.rf.frequency);
        
        // 查找位长索引
        currentBitLengthIndex = 5; // 默认24位
        for (int i = 0; i < BITLENGTH_COUNT; i++) {
            if (bitLengthOptions[i] == currentData.rf.bitCount) {
                currentBitLengthIndex = i;
                break;
            }
        }
    } else {
        // 新建数据，初始化为默认值（PT2262典型参数）
        currentData.name = generateDefaultName();
        currentData.kind = SIG_RF;
        currentData.rf.frequency = 433.92f;
        currentData.rf.bitCount = 24;
        currentData.rf.shortPulse = 300;
        currentData.rf.longPulse = 900;
        currentData.rf.syncPulse = 9000;
        memset(currentData.rf.data, 0, sizeof(currentData.rf.data));
        currentData.rf.data[0] = 0xFA; // 示例数据 FA0000
        memset(currentData.rf.protocolName, 0, sizeof(currentData.rf.protocolName));
        strcpy(currentData.rf.protocolName, "CUSTOM");
        currentBitLengthIndex = 5; // 24位
    }
    
    initLayout();
}

EditDataPage::~EditDataPage() {
}

int EditDataPage::findFreqIndex(float freqMHz) {
    // 在全部频点表中查找最接近的频点
    int best = 0;
    float bestDiff = 1e9;
    for (int i = 0; i < RF_FREQ_COUNT_ALL; i++) {
        float diff = fabsf(RF_FREQ_TABLE_ALL[i] - freqMHz);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = i;
        }
    }
    return best;
}

int EditDataPage::editHexDigits() {
    int bits = bitLengthOptions[currentBitLengthIndex];
    return (bits + 3) / 4;
}

uint64_t EditDataPage::dataHeadToValue() {
    // 全部 bits 右对齐取出（MSB first）
    int bits = bitLengthOptions[currentBitLengthIndex];
    int bytes = (bits + 7) / 8;
    uint64_t v = 0;
    for (int i = 0; i < bytes; i++) {
        v = (v << 8) | currentData.rf.data[i];
    }
    v >>= (bytes * 8 - bits);
    return v;
}

void EditDataPage::valueToDataHead(uint64_t value) {
    // 右对齐的值左对齐写回全部字节
    int bits = bitLengthOptions[currentBitLengthIndex];
    int bytes = (bits + 7) / 8;
    uint64_t v = value << (bytes * 8 - bits);
    for (int i = bytes - 1; i >= 0; i--) {
        currentData.rf.data[i] = v & 0xFF;
        v >>= 8;
    }
    // 位长缩短后多余尾字节清零，避免残留旧数据被发射
    for (size_t i = bytes; i < sizeof(currentData.rf.data); i++) {
        currentData.rf.data[i] = 0;
    }
}

void EditDataPage::initLayout() {
    // ========== 字段编辑状态的UI ==========
    // 无标题行：第一行直接放频率，腾出一行给数据独占显示
    statusLabel = nullptr;

    // 第一行(y=0)：频率独占
    freqLabel = new UILabel();
    freqLabel->x = 0;
    freqLabel->y = 0;
    freqLabel->width = 28;
    freqLabel->height = 12;
    freqLabel->label = "频率:";
    freqLabel->textFont = u8g2_font_wqy12_t_gb2312;
    freqLabel->textAlign = LEFT;
    freqLabel->verticalAlign = MIDDLE;
    freqLabel->bVisible = true;
    addWidget(freqLabel);
    
    freqSelect = new UISelectValue();
    freqSelect->x = 28;
    freqSelect->y = 0;
    freqSelect->width = 46;
    freqSelect->height = 12;
    freqSelect->bSelected = true;
    freqSelect->bShowBorder = false;
    freqSelect->paddingH = 0;
    freqSelect->paddingV = 0;
    freqSelect->textFont = u8g2_font_wqy12_t_gb2312;
    freqSelect->bVisible = true;
    addWidget(freqSelect);
    
    bitLengthLabel = new UILabel();
    bitLengthLabel->x = 64;
    bitLengthLabel->y = 26;
    bitLengthLabel->width = 28;
    bitLengthLabel->height = 12;
    bitLengthLabel->label = "位长:";
    bitLengthLabel->textFont = u8g2_font_wqy12_t_gb2312;
    bitLengthLabel->textAlign = LEFT;
    bitLengthLabel->verticalAlign = MIDDLE;
    bitLengthLabel->bVisible = true;
    addWidget(bitLengthLabel);
    
    bitLengthSelect = new UISelectValue();
    bitLengthSelect->x = 92;
    bitLengthSelect->y = 26;
    bitLengthSelect->width = 36;
    bitLengthSelect->height = 12;
    bitLengthSelect->bSelected = false;
    bitLengthSelect->bShowBorder = false;
    bitLengthSelect->paddingH = 0;
    bitLengthSelect->paddingV = 0;
    bitLengthSelect->textFont = u8g2_font_wqy12_t_gb2312;
    bitLengthSelect->bVisible = true;
    addWidget(bitLengthSelect);
    
    // 第二行(y=13)：短宽 + 长宽
    shortPulseLabel = new UILabel();
    shortPulseLabel->x = 0;
    shortPulseLabel->y = 13;
    shortPulseLabel->width = 28;
    shortPulseLabel->height = 12;
    shortPulseLabel->label = "短宽:";
    shortPulseLabel->textFont = u8g2_font_wqy12_t_gb2312;
    shortPulseLabel->textAlign = LEFT;
    shortPulseLabel->verticalAlign = MIDDLE;
    shortPulseLabel->bVisible = true;
    addWidget(shortPulseLabel);
    
    shortPulseEdit = new UIEditableNumber();
    shortPulseEdit->x = 28;
    shortPulseEdit->y = 13;
    shortPulseEdit->width = 36;
    shortPulseEdit->height = 12;
    shortPulseEdit->setMode(EDITABLE_DECIMAL);
    shortPulseEdit->setMaxDigits(4);
    shortPulseEdit->setRange(50, 5000);
    shortPulseEdit->setValue(currentData.rf.shortPulse);
    shortPulseEdit->setSelected(false);
    shortPulseEdit->textFont = u8g2_font_wqy12_t_gb2312;
    shortPulseEdit->bVisible = true;
    addWidget(shortPulseEdit);
    
    longPulseLabel = new UILabel();
    longPulseLabel->x = 64;
    longPulseLabel->y = 13;
    longPulseLabel->width = 28;
    longPulseLabel->height = 12;
    longPulseLabel->label = "长宽:";
    longPulseLabel->textFont = u8g2_font_wqy12_t_gb2312;
    longPulseLabel->textAlign = LEFT;
    longPulseLabel->verticalAlign = MIDDLE;
    longPulseLabel->bVisible = true;
    addWidget(longPulseLabel);
    
    longPulseEdit = new UIEditableNumber();
    longPulseEdit->x = 92;
    longPulseEdit->y = 13;
    longPulseEdit->width = 36;
    longPulseEdit->height = 12;
    longPulseEdit->setMode(EDITABLE_DECIMAL);
    longPulseEdit->setMaxDigits(4);
    longPulseEdit->setRange(50, 15000);
    longPulseEdit->setValue(currentData.rf.longPulse);
    longPulseEdit->setSelected(false);
    longPulseEdit->textFont = u8g2_font_wqy12_t_gb2312;
    longPulseEdit->bVisible = true;
    addWidget(longPulseEdit);
    
    // 第三行(y=26)：同步 + 位长
    syncPulseLabel = new UILabel();
    syncPulseLabel->x = 0;
    syncPulseLabel->y = 26;
    syncPulseLabel->width = 28;
    syncPulseLabel->height = 12;
    syncPulseLabel->label = "同步:";
    syncPulseLabel->textFont = u8g2_font_wqy12_t_gb2312;
    syncPulseLabel->textAlign = LEFT;
    syncPulseLabel->verticalAlign = MIDDLE;
    syncPulseLabel->bVisible = true;
    addWidget(syncPulseLabel);
    
    syncPulseEdit = new UIEditableNumber();
    syncPulseEdit->x = 28;
    syncPulseEdit->y = 26;
    syncPulseEdit->width = 36;
    syncPulseEdit->height = 12;
    syncPulseEdit->setMode(EDITABLE_DECIMAL);
    syncPulseEdit->setMaxDigits(5);
    syncPulseEdit->setRange(0, UINT16_MAX);
    syncPulseEdit->setValue(currentData.rf.syncPulse);
    syncPulseEdit->setSelected(false);
    syncPulseEdit->textFont = u8g2_font_wqy12_t_gb2312;
    syncPulseEdit->bVisible = true;
    addWidget(syncPulseEdit);
    
    // 第四行(y=39)：数据独占一行
    dataLabel = new UILabel();
    dataLabel->x = 0;
    dataLabel->y = 39;
    dataLabel->width = 28;
    dataLabel->height = 12;
    dataLabel->label = "数据:";
    dataLabel->textFont = u8g2_font_wqy12_t_gb2312;
    dataLabel->textAlign = LEFT;
    dataLabel->verticalAlign = MIDDLE;
    dataLabel->bVisible = true;
    addWidget(dataLabel);
    
    dataEdit = new UIEditableNumber();
    dataEdit->x = 28;
    dataEdit->y = 39;
    dataEdit->width = 100;
    dataEdit->height = 12;
    dataEdit->setMode(EDITABLE_HEX);
    dataEdit->setSelected(false);
    dataEdit->textFont = u8g2_font_wqy12_t_gb2312;
    dataEdit->bVisible = true;
    addWidget(dataEdit);
    
    // 根据当前位长设置数据字段的范围
    updateDataRangeByBitLength();
    
    // 导航栏
    navBar = new UINavBar(0, 52, SCREEN_WIDTH, 12);
    navBar->setMargin(0, 0);
    navBar->setLeftButtonText("返回");
    navBar->setMiddleButtonText("4/6切换");
    navBar->setRightButtonText("下一步");
    navBar->bVisible = true;
    addWidget(navBar);
    
    // ========== 名称编辑状态的UI ==========
    
    // 名称输入框
    nameInput = new UIInput(0, 0, 128, 50);
    nameInput->setTitle("请输入名称");
    nameInput->setText(currentData.name);
    nameInput->setTextAlign(INPUT_LEFT);
    nameInput->bVisible = false;
    addWidget(nameInput);
    
    // 名称编辑的导航栏
    nameNavBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    nameNavBar->setLeftButtonText("返回");
    nameNavBar->setMiddleButtonText("删除");
    nameNavBar->setRightButtonText("保存");
    nameNavBar->bVisible = false;
    addWidget(nameNavBar);
    
    // 初始更新显示
    updateFieldDisplay();
}

void EditDataPage::updateFieldDisplay() {
    // 更新所有字段的选中状态（数值类字段区分：选中=实心，编辑=空心+位高亮）
    freqSelect->bSelected = (currentFieldIndex == FIELD_FREQ);
    bitLengthSelect->bSelected = (currentFieldIndex == FIELD_BITLENGTH);
    shortPulseEdit->setSelected(currentFieldIndex == FIELD_SHORTPULSE);
    shortPulseEdit->setEditing(digitEditing && currentFieldIndex == FIELD_SHORTPULSE);
    longPulseEdit->setSelected(currentFieldIndex == FIELD_LONGPULSE);
    longPulseEdit->setEditing(digitEditing && currentFieldIndex == FIELD_LONGPULSE);
    syncPulseEdit->setSelected(currentFieldIndex == FIELD_SYNCPULSE);
    syncPulseEdit->setEditing(digitEditing && currentFieldIndex == FIELD_SYNCPULSE);
    dataEdit->setSelected(currentFieldIndex == FIELD_DATA);
    dataEdit->setEditing(digitEditing && currentFieldIndex == FIELD_DATA);
    
    // 更新值显示
    char freqBuf[12];
    snprintf(freqBuf, sizeof(freqBuf), "%.2fM", RF_FREQ_TABLE_ALL[currentFreqIndex]);
    freqSelect->value = freqBuf;
    bitLengthSelect->value = String(bitLengthOptions[currentBitLengthIndex]) + "bit";
    shortPulseEdit->setValue(currentData.rf.shortPulse);
    longPulseEdit->setValue(currentData.rf.longPulse);
    syncPulseEdit->setValue(currentData.rf.syncPulse);
    dataEdit->setValue(dataHeadToValue());
}

void EditDataPage::updateDataRangeByBitLength() {
    int bits = bitLengthOptions[currentBitLengthIndex];
    int hexDigits = editHexDigits();

    // 计算最大值
    uint64_t maxValue;
    if (bits >= 64) {
        maxValue = 0xFFFFFFFFFFFFFFFFULL;
    } else {
        maxValue = ((uint64_t)1 << bits) - 1;
    }

    // 更新数据字段的设置
    dataEdit->setMaxDigits(hexDigits);
    dataEdit->setRange(0, maxValue);

    // 确保当前头部值不超过新的最大值
    if (dataHeadToValue() > maxValue) {
        valueToDataHead(maxValue);
    }
    dataEdit->setValue(dataHeadToValue());
}

UIEditableNumber* EditDataPage::activeEditor() {
    switch (currentFieldIndex) {
        case FIELD_SHORTPULSE: return shortPulseEdit;
        case FIELD_LONGPULSE:  return longPulseEdit;
        case FIELD_SYNCPULSE:  return syncPulseEdit;
        case FIELD_DATA:       return dataEdit;
        default:               return nullptr;
    }
}

void EditDataPage::syncEditorToData() {
    switch (currentFieldIndex) {
        case FIELD_SHORTPULSE:
            currentData.rf.shortPulse = shortPulseEdit->getValue();
            break;
        case FIELD_LONGPULSE:
            currentData.rf.longPulse = longPulseEdit->getValue();
            break;
        case FIELD_SYNCPULSE:
            currentData.rf.syncPulse = syncPulseEdit->getValue();
            break;
        case FIELD_DATA:
            valueToDataHead(dataEdit->getValue());
            break;
        default:
            break;
    }
}

void EditDataPage::moveFieldUp() {
    switch (currentFieldIndex) {
        case FIELD_FREQ:
            currentFreqIndex--;
            if (currentFreqIndex < 0) currentFreqIndex = RF_FREQ_COUNT_ALL - 1;
            break;
        case FIELD_BITLENGTH:
            currentBitLengthIndex--;
            if (currentBitLengthIndex < 0) currentBitLengthIndex = BITLENGTH_COUNT - 1;
            updateDataRangeByBitLength();  // 更新数据范围
            break;
        case FIELD_SHORTPULSE:
        case FIELD_LONGPULSE:
        case FIELD_SYNCPULSE:
        case FIELD_DATA:
            if (!digitEditing) {
                digitEditing = true;   // 选中态按2：进入逐位编辑
            } else {
                activeEditor()->incrementDigit();
                syncEditorToData();
            }
            updateFieldDisplay();
            return;
    }
    updateFieldDisplay();
}

void EditDataPage::moveFieldDown() {
    switch (currentFieldIndex) {
        case FIELD_FREQ:
            currentFreqIndex++;
            if (currentFreqIndex >= RF_FREQ_COUNT_ALL) currentFreqIndex = 0;
            break;
        case FIELD_BITLENGTH:
            currentBitLengthIndex++;
            if (currentBitLengthIndex >= BITLENGTH_COUNT) currentBitLengthIndex = 0;
            updateDataRangeByBitLength();  // 更新数据范围
            break;
        case FIELD_SHORTPULSE:
        case FIELD_LONGPULSE:
        case FIELD_SYNCPULSE:
        case FIELD_DATA:
            if (!digitEditing) {
                digitEditing = true;   // 选中态按8：进入逐位编辑
            } else {
                activeEditor()->decrementDigit();
                syncEditorToData();
            }
            updateFieldDisplay();
            return;
    }
    updateFieldDisplay();
}

void EditDataPage::switchToNameEdit() {
    currentState = EDIT_NAME;
    
    // 保存字段到数据
    currentData.kind = SIG_RF;
    currentData.rf.frequency = RF_FREQ_TABLE_ALL[currentFreqIndex];
    currentData.rf.bitCount = bitLengthOptions[currentBitLengthIndex];
    if (digitEditing) digitEditing = false;
    currentData.rf.shortPulse = shortPulseEdit->getValue();
    currentData.rf.longPulse = longPulseEdit->getValue();
    currentData.rf.syncPulse = syncPulseEdit->getValue();
    valueToDataHead(dataEdit->getValue());
    
    // 隐藏字段编辑UI
    if (statusLabel) statusLabel->bVisible = false;
    freqLabel->bVisible = false;
    bitLengthLabel->bVisible = false;
    shortPulseLabel->bVisible = false;
    longPulseLabel->bVisible = false;
    syncPulseLabel->bVisible = false;
    dataLabel->bVisible = false;
    freqSelect->bVisible = false;
    bitLengthSelect->bVisible = false;
    shortPulseEdit->bVisible = false;
    longPulseEdit->bVisible = false;
    syncPulseEdit->bVisible = false;
    dataEdit->bVisible = false;
    navBar->bVisible = false;
    
    // 显示名称编辑UI
    nameInput->bVisible = true;
    nameInput->setText(currentData.name);
    nameNavBar->bVisible = true;
}

void EditDataPage::saveAndReturn() {
    // 保存名称
    currentData.name = nameInput->getText();
    
    // 保存到存储
    dataStore.SaveSignal(dataIndex, currentData);
    
    // 返回上一页
    uiEngine.navigateBack();
}

String EditDataPage::generateDefaultName() {
    return "RF-" + String(dataIndex);
}

void EditDataPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
}

void EditDataPage::update() {
    // 更新当前活动的输入组件
    if (currentState == EDIT_NAME) {
        nameInput->update();
    }
}

void EditDataPage::onButtonBack(void* context) {
    if (currentState == EDIT_FIELDS) {
        navBar->showLeftBlink(1, 80, 80, [this]() {
            uiEngine.navigateBack();
        });
    } else if (currentState == EDIT_NAME) {
        nameNavBar->showLeftBlink(1, 80, 80, [this]() {
            // 返回字段编辑
            currentState = EDIT_FIELDS;
            nameInput->bVisible = false;
            nameNavBar->bVisible = false;
            
            if (statusLabel) statusLabel->bVisible = true;
            freqLabel->bVisible = true;
            bitLengthLabel->bVisible = true;
            shortPulseLabel->bVisible = true;
            longPulseLabel->bVisible = true;
            syncPulseLabel->bVisible = true;
            dataLabel->bVisible = true;
            freqSelect->bVisible = true;
            bitLengthSelect->bVisible = true;
            shortPulseEdit->bVisible = true;
            longPulseEdit->bVisible = true;
            syncPulseEdit->bVisible = true;
            dataEdit->bVisible = true;
            navBar->bVisible = true;
        });
    }
}

void EditDataPage::onButtonEnter(void* context) {
    if (currentState == EDIT_FIELDS) {
        navBar->showRightBlink(1, 80, 80, [this]() {
            switchToNameEdit();
        });
    } else if (currentState == EDIT_NAME) {
        nameNavBar->showRightBlink(1, 80, 80, [this]() {
            saveAndReturn();
        });
    }
}

void EditDataPage::onButtonMenu(void* context) {
    if (currentState == EDIT_NAME) {
        nameInput->deleteChar();
    }
}

void EditDataPage::onButton1(void* context) {
    if (currentState == EDIT_NAME) {
        nameInput->onButtonPress(1);
    }
}

void EditDataPage::onButton2(void* context) {
    if (currentState == EDIT_FIELDS) {
        moveFieldUp();
    } else if (currentState == EDIT_NAME) {
        nameInput->onButtonPress(2);
    }
}

void EditDataPage::onButton3(void* context) {
    if (currentState == EDIT_NAME) {
        nameInput->onButtonPress(3);
    }
}

void EditDataPage::onButton4(void* context) {
    if (currentState == EDIT_FIELDS) {
        // 4键：逐位编辑态=数字光标左移（到头不切字段）；选中态=切上一字段
        UIEditableNumber* ed = activeEditor();
        if (ed != nullptr && digitEditing) {
            ed->moveCursorLeft();
        } else {
            digitEditing = false;
            currentFieldIndex--;
            if (currentFieldIndex < 0) currentFieldIndex = FIELD_COUNT - 1;
            updateFieldDisplay();
        }
    } else if (currentState == EDIT_NAME) {
        nameInput->onButtonPress(4);
    }
}

void EditDataPage::onButton5(void* context) {
    if (currentState == EDIT_FIELDS) {
        // 5键：数值类字段在选中/编辑之间切换（编辑态按5=确认退出）
        if (activeEditor() != nullptr) {
            if (digitEditing) syncEditorToData();
            digitEditing = !digitEditing;
            updateFieldDisplay();
        }
    } else if (currentState == EDIT_NAME) {
        nameInput->onButtonPress(5);
    }
}

void EditDataPage::onButton6(void* context) {
    if (currentState == EDIT_FIELDS) {
        // 6键：逐位编辑态=数字光标右移（到头不切字段）；选中态=切下一字段
        UIEditableNumber* ed = activeEditor();
        if (ed != nullptr && digitEditing) {
            ed->moveCursorRight();
        } else {
            digitEditing = false;
            currentFieldIndex++;
            if (currentFieldIndex >= FIELD_COUNT) currentFieldIndex = 0;
            updateFieldDisplay();
        }
    } else if (currentState == EDIT_NAME) {
        nameInput->onButtonPress(6);
    }
}

void EditDataPage::onButton7(void* context) {
    if (currentState == EDIT_NAME) {
        nameInput->onButtonPress(7);
    }
}

void EditDataPage::onButton8(void* context) {
    if (currentState == EDIT_FIELDS) {
        moveFieldDown();
    } else if (currentState == EDIT_NAME) {
        nameInput->onButtonPress(8);
    }
}

void EditDataPage::onButton9(void* context) {
    if (currentState == EDIT_NAME) {
        nameInput->onButtonPress(9);
    }
}
