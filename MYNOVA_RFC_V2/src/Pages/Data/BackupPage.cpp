/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "BackupPage.h"
#include "../../Services/BackupManager.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/Animation/MessageBoxAnimation.h"
#include "../../GUI/GUIRender.h"

extern UIEngine uiEngine;

BackupPage::BackupPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    currentState(STATE_LIST),
    selectedFile(""),
    listMenu(nullptr),
    actionMenu(nullptr),
    confirmBox(nullptr),
    resultBox(nullptr),
    confirmNavBar(nullptr) {
    initLayout();
}

BackupPage::~BackupPage() {
}

void BackupPage::initLayout() {
    // 备份列表菜单（首项=导出, 其余=TF卡备份文件）
    listMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    listMenu->titleText = "备份管理";
    listMenu->bShowBtnTips = true;
    listMenu->bShowBorder = false;
    listMenu->bShowTitle = false;
    listMenu->bVisible = true;

    listMenu->getNavBar()->setLeftButtonText("返回");
    listMenu->getNavBar()->setRightButtonText("选择");

    refreshList();
    addWidget(listMenu);

    // 恢复选项菜单
    actionMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    actionMenu->titleText = "恢复内容";
    actionMenu->bShowBtnTips = true;
    actionMenu->bShowBorder = false;
    actionMenu->bShowTitle = false;
    actionMenu->bVisible = false;

    actionMenu->getNavBar()->setLeftButtonText("返回");
    actionMenu->getNavBar()->setRightButtonText("确认");

    actionMenu->addMenuItem("1. 恢复全部");
    actionMenu->addMenuItem("2. 仅恢复信号");
    actionMenu->addMenuItem("3. 仅恢复配置");
    addWidget(actionMenu);

    // 恢复确认框
    confirmBox = new UIMessageBox(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    confirmBox->setMessage("恢复将覆盖现有数据，确认？");
    confirmBox->setTitle("恢复确认");
    confirmBox->setButtons("取消", "确认");
    addWidget(confirmBox);

    // 结果提示框
    resultBox = new UIMessageBox(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    resultBox->setMessage("操作完成");
    resultBox->setTitle("提示");
    resultBox->setButtons("返回", "");
    resultBox->bVisible = false;
    addWidget(resultBox);

    // 恢复确认的导航栏
    confirmNavBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    confirmNavBar->setLeftButtonText("取消");
    confirmNavBar->setRightButtonText("恢复");
    confirmNavBar->bVisible = false;
    addWidget(confirmNavBar);
}

// 重新加载备份文件列表
void BackupPage::refreshList() {
    // 清空现有菜单项
    listMenu->clear();

    // 首项固定为"导出备份"
    listMenu->addMenuItem("1. 导出备份到TF卡");

    // 加载TF卡备份文件（时间倒序）
    listedFiles = BackupManager::listBackups();
    if (listedFiles.empty()) {
        listMenu->addMenuItem("-- 暂无备份文件 --");
    } else {
        for (size_t i = 0; i < listedFiles.size(); i++) {
            listMenu->addMenuItem(String(i + 2) + ". " + listedFiles[i]);
        }
    }
}

// 执行导出
void BackupPage::doExport() {
    String path, error;
    bool ok = BackupManager::exportBackup(path, error);

    if (ok) {
        resultBox->setMessage("导出成功: " + path);
    } else {
        resultBox->setMessage("导出失败: " + error);
    }
    resultBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
    currentState = STATE_RESULT;

    // 刷新列表（导出后列表变化, 结果框关闭时可见）
    refreshList();
}

// 执行恢复
void BackupPage::doRestore(bool signals, bool system) {
    String message;
    bool ok = BackupManager::importBackup(selectedFile, signals, system, message);

    resultBox->setMessage(ok ? message : ("恢复失败: " + message));
    resultBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
    currentState = STATE_RESULT;
}

void BackupPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
    resultBox->update();
}

void BackupPage::showPage() {
    UIPage::showPage();
    // 页面显示时刷新文件列表（TF卡可能刚插入或刚导出过）
    refreshList();
}

void BackupPage::onButtonBack(void* context) {
    switch (currentState) {
        case STATE_LIST:
            listMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
                uiEngine.navigateBack();
            });
            break;
        case STATE_ACTION:
            actionMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
                actionMenu->bVisible = false;
                listMenu->bVisible = true;
                currentState = STATE_LIST;
            });
            break;
        case STATE_CONFIRM:
            confirmNavBar->showLeftBlink(1, 80, 80, [this]() {
                confirmBox->hide(true);
                confirmNavBar->bVisible = false;
                actionMenu->bVisible = true;
                currentState = STATE_ACTION;
            });
            break;
        case STATE_RESULT:
            resultBox->showLeftButtonHighLight([this]() {
                resultBox->hide(true);
                listMenu->bVisible = true;
                currentState = STATE_LIST;
            });
            break;
    }
}

void BackupPage::onButtonEnter(void* context) {
    switch (currentState) {
        case STATE_LIST: {
            int sel = listMenu->menuSel;
            listMenu->getNavBar()->showRightBlink(1, 80, 80, [this, sel]() {
                if (sel == 0) {
                    // 导出备份
                    doExport();
                } else {
                    // 选择备份文件 -> 恢复选项
                    if (listedFiles.empty()) {
                        resultBox->setMessage("TF卡中无备份文件");
                        resultBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
                        listMenu->bVisible = false;
                        currentState = STATE_RESULT;
                        return;
                    }
                    int fileIdx = sel - 1;
                    if (fileIdx >= (int)listedFiles.size()) return;
                    selectedFile = listedFiles[fileIdx];

                    listMenu->bVisible = false;
                    actionMenu->bVisible = true;
                    currentState = STATE_ACTION;
                }
            });
            break;
        }
        case STATE_ACTION: {
            int actionIdx = actionMenu->menuSel;
            actionMenu->getNavBar()->showRightBlink(1, 80, 80, [this, actionIdx]() {
                if (actionIdx == 0) {
                    // 恢复全部 -> 确认
                    actionMenu->bVisible = false;
                    confirmBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
                    confirmNavBar->bVisible = true;
                    currentState = STATE_CONFIRM;
                } else if (actionIdx == 1) {
                    // 仅恢复信号 -> 确认
                    actionMenu->bVisible = false;
                    confirmBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
                    confirmNavBar->bVisible = true;
                    currentState = STATE_CONFIRM;
                } else {
                    // 仅恢复配置（配置覆盖风险低, 直接执行）
                    doRestore(false, true);
                }
            });
            break;
        }
        case STATE_CONFIRM:
            confirmNavBar->showRightBlink(1, 80, 80, [this]() {
                confirmBox->hide(true);
                confirmNavBar->bVisible = false;
                // 按恢复选项执行: 全部 或 仅信号
                int actionIdx = actionMenu->menuSel;
                doRestore(true, actionIdx == 0);
            });
            break;
        case STATE_RESULT:
            resultBox->showLeftButtonHighLight([this]() {
                resultBox->hide(true);
                listMenu->bVisible = true;
                currentState = STATE_LIST;
            });
            break;
    }
}

void BackupPage::onButtonMenu(void* context) {
    if (currentState == STATE_LIST) {
        listMenu->moveDown();
    }
}

void BackupPage::onButton1(void* context) {
    if (currentState == STATE_LIST) {
        listMenu->moveUp();
    }
}

void BackupPage::onButton2(void* context) {
    if (currentState == STATE_LIST) {
        listMenu->moveUp();
    }
}

void BackupPage::onButton5(void* context) {
    onButtonEnter(context);
}

void BackupPage::onButton8(void* context) {
    if (currentState == STATE_LIST) {
        listMenu->moveDown();
    }
}
