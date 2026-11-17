/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef FILEMANAGERPAGE_H
#define FILEMANAGERPAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/Widget/UIInput.h"
#include "../../GUI/GUIRender.h"
#include <vector>

/**
 * FileManagerPage - TF卡通用文件管理器
 *
 * 交互约定(按需求重构):
 * - 浏览页无底部"返回/进入"栏,列表独占全屏,避免遮挡
 * - Enter = 进入: 文件夹进入下级;文件打开属性页(专业只读页)
 * - Menu = 统一操作菜单: 复制/移动/重命名/删除/属性/新建文件夹/设置/存储信息
 *   (文件夹 Enter 是进入,属性只能从菜单进;文件 Enter 直接进属性页,菜单属性与其同源)
 * - 设置为独立页: 排序 + 隐藏文件开关,2/8移动,4/6改值(Enter不改值)
 * - 属性页: 键值自动折行不截断;超屏时 2/8 上下滚动, 4/6 左右滚动
 * - 存储信息为独立页: 饼图 + 总量/可用/已用/格式
 * - 多选: 浏览页按 1 标记当前项(旧光标保持点亮)并下移;按 3 清除全部标记
 */
class FileManagerPage : public UIPage {
public:
    FileManagerPage();
    ~FileManagerPage() override;

    void showPage() override;
    void update() override;
    void render(U8G2* u8g2) override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
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
    enum FMAction {
        FMA_NONE = 0,
        FMA_UP, FMA_DOWN, FMA_PAGEUP, FMA_PAGEDOWN,
        FMA_LEFT, FMA_RIGHT,
        FMA_ENTER, FMA_BACK, FMA_MENU, FMA_RETRY,
        FMA_MARK, FMA_CLEAR_MARK
    };
    enum FMBrowserState { FM_INIT, FM_NO_CARD, FM_NO_CODEC, FM_BROWSE };
    enum FMOverlay {
        OV_NONE,
        OV_FILE_MENU,     // 保留兼容,实际不再使用
        OV_SYS_MENU,      // 统一操作菜单(Menu键)
        OV_CONFIRM_DELETE,
        OV_INPUT_RENAME,
        OV_INPUT_MKDIR,
        OV_PICKER,
        OV_ATTR,          // 保留兼容(旧弹窗,不再使用)
        OV_PROGRESS,
        OV_ATTR_PAGE,     // 文件属性独立页
        OV_STORAGE_PAGE,  // 存储信息独立页
        OV_SETTINGS_PAGE  // 设置独立页
    };
    struct DirEntry {
        String name;
        bool isDir = false;
        size_t size = 0;
    };

    void initHardware();
    void retryInit();
    void loadDirectory();
    void moveCursor(int delta);
    void pageUp();
    void pageDown();
    void enterItem();
    void goBack();
    void handleBack();

    // 覆盖层
    void openFileMenu(); // 废弃: Enter不再弹菜单
    void openSysMenu();
    void executeFileAction(int idx);
    void executeSysAction(int idx);
    void confirmDelete(bool isBulk);
    void doDeleteSingle();
    void doDeleteBulk();
    void startRename();
    void commitRename();
    void startMkdir();
    void commitMkdir();
    void openAttr(); // 废弃旧弹窗,转调 openAttrPage
    void openAttrPage();
    void openStoragePage();
    void openSettingsPage();
    void exitSettingsPage();
    void toggleMultiSelect(); // 废弃保留
    void toggleSelectCurrent(); // 旧多选模式用
    void toggleSelectSticky(); // 新: 按1标记+下移
    void clearSelection();
    bool hasSelection() const;
    int selectedCount() const;
    void selectAll(bool sel);
    void startCopyOrMove(bool isMove, bool bulk);
    void openPicker(bool isMove, bool bulk);
    void pickerConfirm();
    void doCopyMove(bool isMove, bool bulk);
    bool copyFile(const String& src, const String& dst);
    bool copyDirRecursive(const String& src, const String& dst);
    bool deleteRecursive(const String& path);
    void showResult(const String& msg, const char* title = "提示");
    void hideOverlays();
    void refreshAfterFsOp();

    // 属性页 / 存储页 / 设置页
    void buildAttrLines();
    void drawAttrPage(U8G2* u8g2);
    void drawStoragePage(U8G2* u8g2);
    void drawSettingsPage(U8G2* u8g2);
    void settingsChangeValue(int delta);
    String extTypeLabel(const String& name, bool isDir) const;
    String formatSizeFull(size_t sz) const;
    String formatSizeShort(size_t sz) const;
    String formatMTime(const String& fullPath) const;
    String permString(bool isDir) const;

    void drawBrowseUI(U8G2* u8g2);
    void drawErrorUI(U8G2* u8g2, const char* title, const char* hint);
    void updateSelScroll();
    String joinPath(const String& dir, const String& name) const;
    String getParentPath(const String& p) const;
    bool isHiddenName(const String& n) const { return n.startsWith("."); }

    // 浏览
    FMBrowserState browserState = FM_INIT;
    String currentPath = "/";
    std::vector<DirEntry> entries;
    int cursor = 0;
    int viewStart = 0;
    int loadRetries = 0;
    bool showHidden = false;
    enum SortMode { SORT_NAME, SORT_SIZE, SORT_TIME } sortMode = SORT_NAME;

    // Picker
    String pickerPath = "/";
    std::vector<DirEntry> pickerEntries;
    int pickerCursor = 0;
    int pickerViewStart = 0;

    // 多选(粘性标记,常驻有效,不再依赖 multiSelectMode)
    bool multiSelectMode = false; // 保留兼容,不再使用
    std::vector<bool> selected;
    int bulkOpPending = 0; // 0 none 1 copy 2 move 3 delete 4 move-single 5 copy-single

    // 统一操作菜单项缓存(解决动态菜单索引映射)
    std::vector<String> opItems;

    // 属性页数据(键值拆开,渲染时按屏宽自动折行;2/8纵滚,4/6横滚)
    std::vector<String> attrKeys;
    std::vector<String> attrVals;
    std::vector<String> attrRows; // 渲染时折行缓存
    int attrScroll = 0;           // 纵向滚动(行)
    int attrHScroll = 0;          // 横向滚动(px)
    int attrRowCount = 0;         // 折行后的总行数(渲染时更新)
    int attrMaxHScroll = 0;       // 可见行中最宽内容的溢出像素(渲染时更新)
    String attrTitle = "";

    // 设置页
    int settingsSel = 0; // 0 排序 1 隐藏文件
    bool settingsDirty = false; // 设置页改过值,退出时才重载目录

    // 存储页
    bool storageFromMenu = false;

    // 输入 + 消息
    UIInput* nameInput = nullptr;
    UINavBar* inputNavBar = nullptr;
    UIMessageBox* msgBox = nullptr;
    UIMessageBox* attrBox = nullptr;
    UIMessageBox* confirmBox = nullptr;
    UINavBar* confirmNavBar = nullptr;
    UINavBar* pickerNavBar = nullptr;

    // 菜单
    UIMenu* fileMenu = nullptr;
    UIMenu* sysMenu = nullptr;
    UIMenu* pickerMenu = nullptr;
    FMOverlay overlay = OV_NONE;
    String pendingSrcPath;
    String pendingNewName;

    // 滚动
    String selFullName = "";
    int selNameWidth = 0;
    int selScrollOffset = 0;
    int selScrollPhase = 0;
    unsigned long selScrollTimer = 0;

    volatile int pendingAction = FMA_NONE;
    bool initPending = false;
    unsigned long initPendingAt = 0;

    UINavBar* navBar = nullptr;

    static const int VISIBLE_ROWS = 4;
    static const int NAME_VIEW_W = 100;
    static const int INIT_MIN_LOADING_MS = 350;
};

#endif
