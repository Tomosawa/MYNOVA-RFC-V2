/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "SaveDataPage.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/Animation/MessageBoxAnimation.h"
#include "../../GUI/Animation/AnimationEngine.h"
#include "../../Services/DataStore.h"
#include "../../GUI/UIEngine.h"
#include "../../Lib/RawCaptureSession.h"
#include "../../Services/RawSignalFiles.h"
#include "../../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "SAVE"

extern UIEngine uiEngine;
extern DataStore dataStore;

SaveDataPage::SaveDataPage(const SignalDraft& signalDraft, int suggested, int backSteps) : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
draft(signalDraft),
suggestedSlot(suggested),
savedBackSteps(backSteps > 0 ? backSteps : 2),
currentState(STATE_SELECT) {
    initLayout();
    if (suggestedSlot >= 1) setSuggestedSlot(suggestedSlot);
}

void SaveDataPage::setSuggestedSlot(int slot) {
    suggestedSlot = slot;
    if (!dataListMenu) return;
    int sc = (draft.kind == SIG_IR ? IR_SLOT_COUNT : RF_SLOT_COUNT);
    if (slot < 1 || slot > sc) return;
    dataListMenu->menuSel = slot - 1;
    dataListMenu->menuStart = 0;
    dataListMenu->menuPos = slot - 1;
    int lines = dataListMenu->menuLines;
    if (dataListMenu->bShowTitle) lines--;
    if (dataListMenu->bShowBtnTips) lines--;
    if (dataListMenu->menuPos >= lines) {
        dataListMenu->menuStart = dataListMenu->menuSel - lines + 1;
        dataListMenu->menuPos = lines - 1;
    }
}

SaveDataPage::~SaveDataPage() {
}

void SaveDataPage::initLayout() {
    // 按待保存类型仅展示对应域的100槽，避免 RF/IR 互相干扰
    int slotCount = (draft.kind == SIG_IR) ? IR_SLOT_COUNT : RF_SLOT_COUNT;
    dataListMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    dataListMenu->titleText = (draft.kind == SIG_IR) ? "选择红外位置" : "选择射频位置";
    dataListMenu->bShowBtnTips = true;
    dataListMenu->bShowBorder = false;
    dataListMenu->bShowTitle = false;
    dataListMenu->bVisible = true;
    dataListMenu->getNavBar()->setLeftButtonText("返回");
    dataListMenu->getNavBar()->setRightButtonText("选择");
    for (int i = 1; i <= slotCount; i++) {
        String itemName = String(i) + ". ";
        SignalEntry entry = dataStore.ReadSignal(draft.kind, i);
        if (entry.name.length() > 0) itemName += entry.name;
        else itemName += "----------";
        dataListMenu->addMenuItem(itemName);
    }
    addWidget(dataListMenu);

    // 初始化提示框（使用默认居中位置，避开底部NavBar）
    messageBox = new UIMessageBox(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    messageBox->setMessage("要覆盖数据吗？");
    messageBox->setTitle("确认覆盖");
    messageBox->setButtons("取消", "覆盖");
    addWidget(messageBox);
    
    // 初始化保存成功提示框
    successMessageBox = new UIMessageBox(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    successMessageBox->setMessage("保存成功！");
    successMessageBox->setTitle("提示");
    successMessageBox->setButtons("返回", "");
    successMessageBox->bVisible = false;
    addWidget(successMessageBox);
    
    // 初始化输入框
    nameInput = new UIInput(0, 0, 128, 50);
    nameInput->setTextAlign(INPUT_LEFT);
    nameInput->setTitle("请输入名称");
    nameInput->bVisible = false;
    addWidget(nameInput);
    
    // 初始化输入状态下的导航栏
    inputNavBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    inputNavBar->setLeftButtonText("返回");
    inputNavBar->setMiddleButtonText("删除");
    inputNavBar->setRightButtonText("确认");
    inputNavBar->bVisible = false;
    addWidget(inputNavBar);
}

void SaveDataPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
}

void SaveDataPage::onButtonBack(void* context) {
    if(currentState == STATE_SELECT)
    {
        // 显示左键闪烁动画，动画完成后执行跳转
        dataListMenu->getNavBar()->showLeftBlink(2, 80, 80, [this]() {
            uiEngine.navigateBack();
        });
    }
    else if(currentState == STATE_MESSASGE)
    {
        messageBox->showLeftButtonHighLight([this]() {
            messageBox->hide(true);
            dataListMenu->bVisible = true;
            currentState = STATE_SELECT;
        });
    }
    else if(currentState == STATE_INPUTNAME)
    {
        // 显示左键闪烁动画，动画完成后返回选择状态
        inputNavBar->showLeftBlink(1, 80, 80, [this]() {
            nameInput->bVisible = false;
            inputNavBar->bVisible = false;
            dataListMenu->bVisible = true;
            dataListMenu->getNavBar()->bVisible = true;
            currentState = STATE_SELECT;
        });
    }
    else if(currentState == STATE_SAVE_FAILED)
    {
        successMessageBox->showLeftButtonHighLight([this]() {
            successMessageBox->hide(true);
            nameInput->bVisible = true;
            inputNavBar->bVisible = true;
            currentState = STATE_INPUTNAME;
        });
    }
    else if(currentState == STATE_SAVE_SUCCESS)
    {
        // 显示左键闪烁动画，动画完成后返回到菜单页面
        successMessageBox->showLeftButtonHighLight([this]() {
            successMessageBox->hide(true);
            // 按入口深度回退（默认 2 层回射频菜单；RAW 流程为 3 层）
            uiEngine.navigateBackSteps(savedBackSteps);
        });
    }
}

void SaveDataPage::onButtonEnter(void* context) {
    // 当前是选择状态，确认选择当前位置后，判断是否覆盖
    if(currentState == STATE_SELECT)
    {
        int selectedIndex = dataListMenu->menuSel + 1; // 菜单索引从0开始，但存储位置从1开始
        // 显示右键闪烁动画，动画完成后切换状态
        dataListMenu->getNavBar()->showRightBlink(2, 80, 80, [this, selectedIndex]() {
            if (dataStore.SignalExists(draft.kind, selectedIndex)) {
                messageBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
                dataListMenu->bVisible = false;
                currentState = STATE_MESSASGE;
            } else {
                dataListMenu->bVisible = false;
                nameInput->bVisible = true;
                inputNavBar->bVisible = true;
                nameInput->setText(generateDefaultName(selectedIndex));
                currentState = STATE_INPUTNAME;
            }
        });
    }else if(currentState == STATE_MESSASGE)
    {
        messageBox->showRightButtonHighLight([this]() {
            messageBox->hide();
            int selectedIndex = dataListMenu->menuSel + 1;
            SignalEntry existing = dataStore.ReadSignal(draft.kind, selectedIndex);
            inputNavBar->bVisible = true;
            nameInput->bVisible = true;
            nameInput->setText(existing.name.length() ? existing.name : generateDefaultName(selectedIndex));
            currentState = STATE_INPUTNAME;
        });
    }else if(currentState == STATE_INPUTNAME)
    {
        // 显示右键闪烁动画，动画完成后保存数据并返回
        inputNavBar->showRightBlink(1, 80, 80, [this]() {
            saveDataAndReturn();
        });
    }
}

void SaveDataPage::onButtonMenu(void* context) {
    if (currentState == STATE_INPUTNAME) {
        // 删除字符不需要等待动画，立即执行
        nameInput->deleteChar();
    }
}
void SaveDataPage::onButton1(void* context) {
    if (currentState == STATE_INPUTNAME) {
        nameInput->onButtonPress(1);
    }
}
void SaveDataPage::onButton2(void* context) {
    if(currentState == STATE_SELECT)
    {
        dataListMenu->moveUp();
    }
    else if (currentState == STATE_INPUTNAME) {
        nameInput->onButtonPress(2);
    }
}
void SaveDataPage::onButton3(void* context) {
    if (currentState == STATE_INPUTNAME) {
        nameInput->onButtonPress(3);
    }
}
void SaveDataPage::onButton4(void* context) {
    if (currentState == STATE_INPUTNAME) {
        nameInput->onButtonPress(4);
    }
}
void SaveDataPage::onButton5(void* context) {
    if (currentState == STATE_INPUTNAME) {
        nameInput->onButtonPress(5);
    }
}
void SaveDataPage::onButton6(void* context) {
    if (currentState == STATE_INPUTNAME) {
        nameInput->onButtonPress(6);
    }
}
void SaveDataPage::onButton7(void* context) {
    if (currentState == STATE_INPUTNAME) {
        nameInput->onButtonPress(7);
    }
}
void SaveDataPage::onButton8(void* context) {
    if(currentState == STATE_SELECT)
    {
        dataListMenu->moveDown();
    }
    else if (currentState == STATE_INPUTNAME) {
        nameInput->onButtonPress(8);
    }
}
void SaveDataPage::onButton9(void* context) {
    if (currentState == STATE_INPUTNAME) {
        nameInput->onButtonPress(9);
    }
}

String SaveDataPage::generateDefaultName(int index) {
    // 默认名带上信号类型前缀（RAW 录制单独前缀）
    String prefix = (draft.kind == SIG_IR) ? "IR-"
                                          : (draft.rawCapture ? "RAW-" : "RF-");
    return prefix + String(index);
}

void SaveDataPage::saveDataAndReturn() {
    int selectedIndex = dataListMenu->menuSel + 1;

    // 组装统一信号条目
    SignalEntry entry;
    entry.name = nameInput->getText();
    entry.kind = draft.kind;

    bool ok = false;
    const char* error = "保存失败";
    if (draft.kind == SIG_RF) {
        // 射频信号：数据为值拷贝
        entry.rf = draft.rf;

        if (draft.rawCapture) {
            DataStore::StorageGuard storage(dataStore);
            if (!storage) {
                error = "存储忙，请重试";
            } else if (!RawSignalFiles::ensureCard()) {
                error = "请插入TF卡";
            } else if (!RawSignalFiles::copyDraft(draft.rawTempPath.c_str(), entry.rf.rawFileId)) {
                error = "请插入或检查TF卡";
            } else {
                ok = dataStore.SaveSignal(selectedIndex, entry);
                if (!ok) RawSignalFiles::remove(selectedIndex, entry.rf.rawFileId);
            }
        } else {
            ok = dataStore.SaveSignal(selectedIndex, entry);
        }
    } else if (draft.kind == SIG_IR && draft.irMeta != nullptr && draft.irEdges != nullptr) {
        // 红外信号：元数据+边沿数据（引用IRHelper内部缓冲）
        entry.ir = *draft.irMeta;
        ok = dataStore.SaveSignal(selectedIndex, entry, draft.irEdges);
    } else {
        LOG_E("SaveDataPage: 无效的信号草稿!");
    }

    LOG_I("[Save] slot=%d kind=%s -> %s",
          selectedIndex, draft.kind == SIG_RF ? "RF" : "IR", ok ? "OK" : "FAIL");
    
    // 隐藏输入框和导航栏
    nameInput->bVisible = false;
    inputNavBar->bVisible = false;
    
    if (ok) {
        // 显示保存成功提示
        successMessageBox->setMessage("保存成功！");
        successMessageBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
    } else {
        // 保存失败提示
        successMessageBox->setMessage(error);
        successMessageBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
    }
    currentState = ok || !draft.rawCapture ? STATE_SAVE_SUCCESS : STATE_SAVE_FAILED;
}
