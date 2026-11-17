/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef EDITDATAPAGE_H
#define EDITDATAPAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/Widget/UIInput.h"
#include "../../GUI/Widget/UISelectValue.h"
#include "../../GUI/Widget/UIEditableNumber.h"
#include "../../Services/DataStore.h"
#include <functional>

/**
 * EditDataPage - 编辑/新建射频数据页面
 * CC1101信号字段：频率(10频点) / 位长 / 短脉宽 / 长脉宽 / 同步脉宽 / 数据(HEX)
 * 红外信号不支持字段编辑（时序数据只能通过学习获得）
 */
class EditDataPage : public UIPage {
public:
    // dataIndex: 数据位置索引(1-100)，isNew: 是否是新建
    EditDataPage(int dataIndex, bool isNew);
    ~EditDataPage();
    
    void render(U8G2* u8g2) override;
    void update() override;
    
    // 按钮事件处理
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton1(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton3(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton6(void* context = nullptr) override;
    void onButton7(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;
    void onButton9(void* context = nullptr) override;
    
private:
    enum EditState {
        EDIT_FIELDS,        // 编辑所有字段（一个屏幕显示）
        EDIT_NAME           // 编辑名称
    };
    
    enum FieldIndex {
        FIELD_FREQ = 0,     // 频率（频点循环选择）
        FIELD_SHORTPULSE,   // 短脉宽
        FIELD_LONGPULSE,    // 长脉宽
        FIELD_SYNCPULSE,    // 同步脉宽
        FIELD_BITLENGTH,    // 位长
        FIELD_DATA,         // 数据（HEX，全位宽可编辑）
        FIELD_COUNT         // 字段总数
    };
    
    void initLayout();
    void switchToNameEdit();
    void saveAndReturn();
    void updateFieldDisplay();
    void moveFieldUp();
    void moveFieldDown();
    String generateDefaultName();
    // 当前位长对应的十六进制位数
    int editHexDigits();
    // 取数据整体（MSB first，右对齐）供编辑器用
    uint64_t dataHeadToValue();
    // 把编辑值写回数据（左对齐）
    void valueToDataHead(uint64_t value);
    // 根据位长更新数据字段的范围
    void updateDataRangeByBitLength();
    // 当前字段对应的数值编辑器（频率/位长返回nullptr）
    UIEditableNumber* activeEditor();
    // 把当前编辑器的值同步回 currentData
    void syncEditorToData();
    
    int dataIndex;
    bool isNewData;
    SignalEntry currentData;
    EditState currentState;
    int currentFieldIndex;  // 当前选中的字段索引
    bool digitEditing;      // 数值类字段是否处于逐位编辑模式
    
    // UI组件 - 字段编辑状态
    UILabel* statusLabel;           // 顶部状态标签
    UILabel* freqLabel;             // 频率标题
    UILabel* bitLengthLabel;        // 位长标题
    UILabel* shortPulseLabel;       // 短脉宽标题
    UILabel* longPulseLabel;        // 长脉宽标题
    UILabel* syncPulseLabel;        // 同步脉宽标题
    UILabel* dataLabel;             // 数据标题
    
    UISelectValue* freqSelect;      // 频率选择器（10频点循环）
    UISelectValue* bitLengthSelect; // 位长选择器
    UIEditableNumber* shortPulseEdit;   // 短脉宽编辑器
    UIEditableNumber* longPulseEdit;    // 长脉宽编辑器
    UIEditableNumber* syncPulseEdit;    // 同步脉宽编辑器
    UIEditableNumber* dataEdit;         // 数据编辑器（HEX）
    
    UINavBar* navBar;
    
    // UI组件 - 名称编辑状态
    UIInput* nameInput;
    UINavBar* nameNavBar;
    
    // 位长选项
    static const int BITLENGTH_COUNT = 10;
    const int bitLengthOptions[BITLENGTH_COUNT] = {4, 8, 12, 16, 20, 24, 28, 32, 36, 40};
    
    int currentFreqIndex;       // 频点索引（RF_FREQ_TABLE_ALL下标）
    int currentBitLengthIndex;
    
    // 辅助：查找浮点频率在频点表中的索引
    int findFreqIndex(float freqMHz);
};

#endif
