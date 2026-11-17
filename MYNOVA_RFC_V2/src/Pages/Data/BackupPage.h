/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef BACKUPPAGE_H
#define BACKUPPAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/Widget/UINavBar.h"
#include <vector>

/**
 * BackupPage - 备份管理页面（系统设置子页）
 * 功能：导出全部备份到TF卡 / 从TF卡备份文件恢复（全部/仅信号/仅系统配置）
 */
class BackupPage : public UIPage {
public:
    BackupPage();
    ~BackupPage();

    void render(U8G2* u8g2) override;
    void showPage() override;   // 页面显示时刷新备份文件列表

    // 按钮事件处理
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton1(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    enum BackupState {
        STATE_LIST,             // 备份列表（首项=导出, 其余=备份文件）
        STATE_ACTION,           // 选择恢复内容
        STATE_CONFIRM,          // 恢复确认
        STATE_RESULT            // 操作结果
    };

    void initLayout();
    void refreshList();         // 重新加载TF卡备份文件列表
    void doExport();            // 执行导出
    void doRestore(bool signals, bool system);  // 执行恢复

    BackupState currentState;
    String selectedFile;        // 当前选中的备份文件名
    std::vector<String> listedFiles;

    UIMenu* listMenu;           // 备份列表菜单
    UIMenu* actionMenu;         // 恢复选项菜单
    UIMessageBox* confirmBox;   // 恢复确认框
    UIMessageBox* resultBox;    // 结果提示框
    UINavBar* confirmNavBar;    // 确认框导航栏
};

#endif
