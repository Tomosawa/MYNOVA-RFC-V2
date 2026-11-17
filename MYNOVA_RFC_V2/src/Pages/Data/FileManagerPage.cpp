/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "FileManagerPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Drivers/AudioPlayer.h"
#include "../../GUI/Animation/MessageBoxAnimation.h"
#include <SD_MMC.h>
#include <math.h>

extern UIEngine uiEngine;
extern AudioPlayer audioPlayer;

#define GLYPH_FOLDER  57814
#define GLYPH_FILE    57807
#define GLYPH_SDCARD  57816

FileManagerPage::FileManagerPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    // 浏览页不再需要底部栏:彻底隐藏,避免遮挡最后一行
    navBar = new UINavBar(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12);
    navBar->setLeftButtonText("");
    navBar->setRightButtonText("");
    navBar->bVisible = false;
    addWidget(navBar);

    fileMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    fileMenu->bShowBtnTips = true; fileMenu->bShowBorder=false; fileMenu->bShowTitle=false; fileMenu->bVisible=false;
    fileMenu->getNavBar()->setLeftButtonText("返回"); fileMenu->getNavBar()->setRightButtonText("选择");
    fileMenu->addMenuItem("1.复制"); fileMenu->addMenuItem("2.移动"); fileMenu->addMenuItem("3.重命名"); fileMenu->addMenuItem("4.删除"); fileMenu->addMenuItem("5.属性");
    addWidget(fileMenu);

    sysMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    sysMenu->bShowBtnTips = true; sysMenu->bShowBorder=false; sysMenu->bShowTitle=false; sysMenu->bVisible=false;
    sysMenu->getNavBar()->setLeftButtonText("返回"); sysMenu->getNavBar()->setRightButtonText("选择");
    addWidget(sysMenu);

    pickerMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    pickerMenu->bShowBtnTips = true; pickerMenu->bShowBorder=false; pickerMenu->bShowTitle=false; pickerMenu->bVisible=false;
    pickerMenu->getNavBar()->setLeftButtonText("返回"); pickerMenu->getNavBar()->setRightButtonText("选择");
    addWidget(pickerMenu);

    pickerNavBar = new UINavBar(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12);
    pickerNavBar->setLeftButtonText("返回"); pickerNavBar->setRightButtonText("确定"); pickerNavBar->bVisible=false;
    addWidget(pickerNavBar);

    msgBox = new UIMessageBox(-1,-1,100,50);
    msgBox->setDefaultPosition(SCREEN_WIDTH, SCREEN_HEIGHT, 15);
    msgBox->bShowButtons=false; msgBox->bVisible=false;
    addWidget(msgBox);
    // msgBox 结果提示框不画框内按钮,按返回键关闭即可

    attrBox = new UIMessageBox(0,0,SCREEN_WIDTH,SCREEN_HEIGHT);
    attrBox->setTitle("属性"); attrBox->setButtons("返回",""); attrBox->bVisible=false;
    addWidget(attrBox);

    confirmBox = new UIMessageBox(0,0,SCREEN_WIDTH,SCREEN_HEIGHT-15);
    confirmBox->setTitle("确认"); confirmBox->setButtons("取消","确认"); confirmBox->bVisible=false;
    confirmBox->bShowButtons = false; // 按钮只由 confirmNavBar 绘制,避免框内按钮与底部栏重复重叠
    addWidget(confirmBox);
    confirmNavBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    confirmNavBar->setLeftButtonText("取消"); confirmNavBar->setRightButtonText("删除"); confirmNavBar->bVisible=false;
    addWidget(confirmNavBar);

    nameInput = new UIInput(0,0,128,50);
    nameInput->setTextAlign(INPUT_LEFT); nameInput->bVisible=false;
    addWidget(nameInput);
    inputNavBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    inputNavBar->setLeftButtonText("返回"); inputNavBar->setMiddleButtonText("删除"); inputNavBar->setRightButtonText("确认"); inputNavBar->bVisible=false;
    addWidget(inputNavBar);
}

FileManagerPage::~FileManagerPage() {}

void FileManagerPage::showPage() {
    if (browserState != FM_BROWSE) {
        browserState = FM_INIT;
        initPending = true;
        initPendingAt = millis();
    }
    hideOverlays();
}

// ---------- helpers ----------
String FileManagerPage::joinPath(const String& dir, const String& name) const {
    if (dir == "/") return "/" + name;
    return dir + "/" + name;
}
String FileManagerPage::getParentPath(const String& p) const {
    if (p == "/") return "/";
    int s = p.lastIndexOf('/');
    if (s <= 0) return "/";
    return p.substring(0, s);
}

void FileManagerPage::initHardware() {
    browserState = FM_INIT;
    AudioInitState st = audioPlayer.init();
    if (st == AUDIO_INIT_OK || st == AUDIO_INIT_NO_CODEC) {
        currentPath = "/";
        cursor = 0; viewStart = 0;
        loadDirectory();
    } else if (st == AUDIO_INIT_NO_CARD) {
        browserState = FM_NO_CARD;
    } else {
        browserState = FM_NO_CODEC;
    }
    initPending = false;
}
void FileManagerPage::retryInit() {
    browserState = FM_INIT;
    initPending = true;
    initPendingAt = millis();
}

void FileManagerPage::loadDirectory() {
    entries.clear(); cursor = 0; viewStart = 0; selFullName = "";
    selected.clear(); multiSelectMode = false;
    navBar->bVisible = false;
    File root = audioPlayer.getFS().open(currentPath);
    if (!root || !root.isDirectory()) {
        audioPlayer.resetCard();
        currentPath = "/";
        if (++loadRetries <= 2) { initHardware(); return; }
        browserState = FM_NO_CARD; return;
    }
    loadRetries = 0;
    std::vector<DirEntry> dirs, files;
    File f = root.openNextFile();
    while (f) {
        String name = f.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        bool isDir = f.isDirectory();
        size_t sz = isDir ? 0 : f.size();
        f.close();
        if (name == "." || name == "..") { f = root.openNextFile(); continue; }
        if (!showHidden && isHiddenName(name)) { f = root.openNextFile(); continue; }
        DirEntry e; e.name = name; e.isDir = isDir; e.size = sz;
        if (isDir) dirs.push_back(e); else files.push_back(e);
        f = root.openNextFile();
    }
    root.close();
    auto nameLess = [](const DirEntry& a, const DirEntry& b){ String la=a.name, lb=b.name; la.toLowerCase(); lb.toLowerCase(); return la<lb; };
    auto sizeLess = [](const DirEntry& a, const DirEntry& b){ return a.size > b.size; };
    if (sortMode == SORT_SIZE) {
        std::sort(dirs.begin(), dirs.end(), nameLess);
        std::sort(files.begin(), files.end(), sizeLess);
    } else {
        std::sort(dirs.begin(), dirs.end(), nameLess);
        std::sort(files.begin(), files.end(), nameLess);
    }
    entries.reserve(dirs.size()+files.size());
    for (auto& e: dirs) entries.push_back(e);
    for (auto& e: files) entries.push_back(e);
    selected.assign(entries.size(), false);
    browserState = FM_BROWSE;
}

void FileManagerPage::moveCursor(int d) {
    if (entries.empty()) return;
    cursor += d;
    if (cursor < 0) cursor = entries.size()-1;
    if (cursor >= (int)entries.size()) cursor = 0;
    if (cursor < viewStart) viewStart = cursor;
    if (cursor >= viewStart + VISIBLE_ROWS) viewStart = cursor - VISIBLE_ROWS + 1;
    if ((int)entries.size() <= VISIBLE_ROWS) viewStart = 0;
}
void FileManagerPage::pageUp() {
    if (entries.empty()) return;
    if (viewStart == 0) { cursor = 0; return; }
    int rel = cursor - viewStart;
    viewStart = (viewStart - VISIBLE_ROWS < 0) ? 0 : viewStart - VISIBLE_ROWS;
    cursor = viewStart + rel;
}
void FileManagerPage::pageDown() {
    if (entries.empty()) return;
    int maxStart = (int)entries.size() - VISIBLE_ROWS; if (maxStart < 0) maxStart = 0;
    if (viewStart >= maxStart) { cursor = entries.size()-1; viewStart = maxStart; return; }
    int rel = cursor - viewStart;
    viewStart = (viewStart + VISIBLE_ROWS > maxStart) ? maxStart : viewStart + VISIBLE_ROWS;
    cursor = viewStart + rel;
    if (cursor >= (int)entries.size()) cursor = entries.size()-1;
}
void FileManagerPage::enterItem() {
    if (entries.empty() || cursor <0 || cursor >= (int)entries.size()) return;
    const DirEntry& e = entries[cursor];
    if (e.isDir) {
        // 文件夹:直接进入
        currentPath = joinPath(currentPath, e.name);
        loadDirectory();
        return;
    }
    // 文件:Enter 打开专业属性页
    pendingSrcPath = joinPath(currentPath, e.name);
    openAttrPage();
}
void FileManagerPage::goBack() {
    if (currentPath == "/") {
        uiEngine.navigateBack();
    } else {
        currentPath = getParentPath(currentPath);
        loadDirectory();
    }
}
void FileManagerPage::handleBack() {
    if (browserState != FM_BROWSE) {
        uiEngine.navigateBack();
    } else goBack();
}

void FileManagerPage::hideOverlays() {
    fileMenu->bVisible=false; sysMenu->bVisible=false; pickerMenu->bVisible=false;
    pickerNavBar->bVisible=false; confirmBox->bVisible=false; confirmNavBar->bVisible=false;
    attrBox->bVisible=false; nameInput->bVisible=false; inputNavBar->bVisible=false;
    // 三个 MessageBox 可能挂着未完成的缩放动画:先无动画隐藏并取消自动关闭,
    // 再清 bVisible,避免动画回调把已关闭的框又"复活"成幽灵
    msgBox->hide(false); msgBox->cancelAutoClose(); msgBox->bVisible = false;
    attrBox->hide(false); attrBox->cancelAutoClose(); attrBox->bVisible = false;
    confirmBox->hide(false); confirmBox->cancelAutoClose(); confirmBox->bVisible = false;
    overlay = OV_NONE;
}
void FileManagerPage::showResult(const String& msg, const char* title) {
    // 先彻底关掉上一次的结果框(含残留动画),再显示新的,避免"幽灵"残留
    msgBox->hide(false); msgBox->cancelAutoClose();
    msgBox->bVisible = false;
    msgBox->bShowButtons = false; // 不画框内按钮,按返回/Menu键关闭即可
    msgBox->setTitle(title); msgBox->setMessage(msg);
    msgBox->show(MSGBOX_ANIME_ZOOM_CENTER,300);
    msgBox->setAutoClose(2500); // 结果框2.5秒自动消失,不会幽灵常驻挡住Menu
    overlay=OV_PROGRESS;
}
void FileManagerPage::refreshAfterFsOp() {
    loadDirectory();
    hideOverlays();
}

// ---------- sticky multi-select (按1标记 / 按3清除) ----------
bool FileManagerPage::hasSelection() const {
    for (bool b : selected) if (b) return true;
    return false;
}
int FileManagerPage::selectedCount() const {
    int n = 0; for (bool b : selected) if (b) n++; return n;
}
void FileManagerPage::toggleSelectSticky() {
    if (entries.empty() || cursor < 0 || cursor >= (int)selected.size()) return;
    selected[cursor] = !selected[cursor];
    moveCursor(1); // 光标下移,已标记行保持点亮
}
void FileManagerPage::clearSelection() {
    for (size_t i = 0; i < selected.size(); i++) selected[i] = false;
}

// ---------- file menu (废弃,保留兼容) ----------
void FileManagerPage::openFileMenu() {
    // 旧逻辑不再使用:Enter 不再弹菜单
    openAttrPage();
}
void FileManagerPage::executeFileAction(int idx) {
    if (cursor<0 || cursor >= (int)entries.size()) return;
    String src = joinPath(currentPath, entries[cursor].name);
    pendingSrcPath = src;
    fileMenu->bVisible=false;
    switch(idx){
        case 0: openPicker(false,false); break;
        case 1: openPicker(true,false); break;
        case 2: startRename(); break;
        case 3: confirmDelete(false); break;
        case 4: openAttrPage(); break;
        default: overlay=OV_NONE; break;
    }
}

// ---------- 属性页 ----------
String FileManagerPage::extTypeLabel(const String& name, bool isDir) const {
    if (isDir) return "文件夹";
    int dot = name.lastIndexOf('.');
    String ext = (dot >= 0) ? name.substring(dot + 1) : "";
    ext.toLowerCase();
    if (ext == "mp3" || ext == "wav" || ext == "ogg" || ext == "flac" || ext == "aac" || ext == "m4a" || ext == "opus") return "音频文件 (." + ext + ")";
    if (ext == "txt" || ext == "log" || ext == "ini" || ext == "cfg" || ext == "json" || ext == "xml" || ext == "md" || ext == "csv") return "文本文件 (." + ext + ")";
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" || ext == "gif") return "图像文件 (." + ext + ")";
    if (ext == "mp4" || ext == "avi" || ext == "mov") return "视频文件 (." + ext + ")";
    if (ext == "bin" || ext == "elf" || ext == "fw" || ext == "uf2") return "固件/二进制 (." + ext + ")";
    if (ext.length() == 0) return "文件 (无扩展名)";
    return "文件 (." + ext + ")";
}
String FileManagerPage::formatSizeFull(size_t sz) const {
    String s;
    if (sz < 1024) s = String(sz) + " B";
    else if (sz < 1024*1024) s = String(sz/1024.0,1) + " KB";
    else s = String(sz/1024.0/1024.0,2) + " MB";
    s += " (" + String((unsigned long)sz) + " 字节)";
    return s;
}
String FileManagerPage::formatSizeShort(size_t sz) const {
    if (sz < 1024) return String(sz) + "B";
    if (sz < 1024*1024) return String(sz/1024.0,1) + "K";
    return String(sz/1024.0/1024.0,1) + "M";
}
String FileManagerPage::formatMTime(const String& fullPath) const {
    File f = audioPlayer.getFS().open(fullPath, FILE_READ);
    if (!f) return "未知";
    time_t t = f.getLastWrite();
    f.close();
    if (t <= 0) return "未知";
    struct tm* tm = localtime(&t);
    if (!tm) return "未知";
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
    return String(buf);
}
String FileManagerPage::permString(bool isDir) const {
    // FAT 无 UNIX 权限位:展示约定值并注明
    return isDir ? "drwxr-xr-x" : "-rw-r--r--";
}

void FileManagerPage::openAttr() { openAttrPage(); }

void FileManagerPage::buildAttrLines() {
    attrKeys.clear();
    attrVals.clear();
    attrScroll = 0;
    if (cursor < 0 || cursor >= (int)entries.size()) return;
    const DirEntry& e = entries[cursor];
    String full = joinPath(currentPath, e.name);
    attrTitle = e.name;

    // 文件夹:统计直属项数 + 递归大小(限量防卡死)
    size_t dirSize = e.size;
    int childCount = -1;
    if (e.isDir) {
        childCount = 0;
        dirSize = 0;
        std::vector<String> stack;
        stack.push_back(full);
        int guard = 0;
        while (!stack.empty() && guard < 3000) {
            String d = stack.back(); stack.pop_back();
            File dir = audioPlayer.getFS().open(d);
            if (!dir) continue;
            if (!dir.isDirectory()) { dir.close(); continue; }
            File f = dir.openNextFile();
            while (f && guard < 3000) {
                guard++;
                String n = f.name();
                int sl = n.lastIndexOf('/');
                if (sl >= 0) n = n.substring(sl + 1);
                bool isD = f.isDirectory();
                size_t sz = isD ? 0 : f.size();
                f.close();
                if (n == "." || n == "..") { f = dir.openNextFile(); continue; }
                if (d == full) childCount++;
                if (isD) stack.push_back(joinPath(d, n));
                else dirSize += sz;
                f = dir.openNextFile();
            }
            dir.close();
        }
    }

    bool hidden = isHiddenName(e.name);

    // 键值拆开存,渲染时按屏宽自动折行;值太长时支持横向滚动
    attrKeys.clear(); attrVals.clear();
    attrKeys.push_back("名称");       attrVals.push_back(e.name);
    attrKeys.push_back("类型");       attrVals.push_back(extTypeLabel(e.name, e.isDir));
    attrKeys.push_back("位置");       attrVals.push_back(currentPath);
    if (e.isDir) {
        attrKeys.push_back("大小");   attrVals.push_back(formatSizeFull(dirSize) + " (含" + String(childCount) + "项)");
    } else {
        attrKeys.push_back("大小");   attrVals.push_back(formatSizeFull(e.size));
    }
    attrKeys.push_back("修改时间");   attrVals.push_back(formatMTime(full));
    attrKeys.push_back("创建时间");   attrVals.push_back("未知 (FAT不支持)");
    attrKeys.push_back("访问时间");   attrVals.push_back("未知 (FAT不支持)");
    attrKeys.push_back("只读");       attrVals.push_back("否 (FAT无只读位)");
    attrKeys.push_back("隐藏");       attrVals.push_back(hidden ? "是 (点文件)" : "否");
    attrKeys.push_back("权限");       attrVals.push_back(permString(e.isDir) + " (约定值)");
}

// 把键值对折行成显示行:值在键后同行排,超宽折到下一行(续行对齐该键的值列)
// maxW 为正文可用像素宽(调用方已扣除滚动条位),文字绝不排进滚动条下方
static void wrapAttrRows(U8G2* u8g2,
                         const std::vector<String>& keys,
                         const std::vector<String>& vals,
                         std::vector<String>& rows,
                         int maxW) {
    rows.clear();
    for (size_t i = 0; i < keys.size(); i++) {
        String head = keys[i] + ": ";
        int headW = u8g2->getUTF8Width(head.c_str());
        int valW = u8g2->getUTF8Width(vals[i].c_str());
        if (headW + valW <= maxW) {
            rows.push_back(head + vals[i]);
            continue;
        }
        // 放不下:逐字累加折行;续行用空格填充到与值列对齐(每格按空格实际宽度计)
        int indentPx = headW; // 续行起始x对齐"值"列
        String cur = head;
        int curW = headW;
        const String& v = vals[i];
        int pos = 0;
        int vlen = v.length();
        u8g2->setFont(u8g2_font_wqy12_t_gb2312);
        int spaceW = u8g2->getUTF8Width(" ");
        if (spaceW < 1) spaceW = 1;
        while (pos < vlen) {
            int adv = 1;
            // UTF-8:跳过续字节
            while (pos + adv < vlen && (v.charAt(pos + adv) & 0xC0) == 0x80) adv++;
            String ch = v.substring(pos, pos + adv);
            int chW = u8g2->getUTF8Width(ch.c_str());
            if (curW + chW > maxW) {
                rows.push_back(cur);
                cur = "";
                int pad = indentPx / spaceW;
                for (int k = 0; k < pad; k++) cur += ' ';
                curW = pad * spaceW;
            }
            cur += ch;
            curW += chW;
            pos += adv;
        }
        if (cur.length() > 0) rows.push_back(cur);
    }
}

void FileManagerPage::openAttrPage() {
    if (entries.empty() || cursor < 0 || cursor >= (int)entries.size()) return;
    buildAttrLines();
    attrScroll = 0;
    attrHScroll = 0;
    attrRowCount = 0;
    attrMaxHScroll = 0;
    hideOverlays();
    overlay = OV_ATTR_PAGE;
}

void FileManagerPage::openStoragePage() {
    hideOverlays();
    storageFromMenu = true;
    overlay = OV_STORAGE_PAGE;
}

void FileManagerPage::openSettingsPage() {
    hideOverlays();
    settingsSel = 0;
    settingsDirty = false;
    overlay = OV_SETTINGS_PAGE;
}

void FileManagerPage::settingsChangeValue(int delta) {
    if (settingsSel == 0) {
        // 排序: 名称 <-> 大小 <-> 时间(时间回退名称排序,仅改内存状态,不重扫TF卡)
        int m = (int)sortMode + delta;
        if (m < 0) m = 2; if (m > 2) m = 0;
        sortMode = (SortMode)m;
        settingsDirty = true;
    } else {
        showHidden = !showHidden;
        settingsDirty = true;
    }
}

void FileManagerPage::exitSettingsPage() {
    // 设置页退出:只有改过值才重载目录(避免无意义扫卡),并保留进入设置页前的光标
    String keepPath = currentPath;
    hideOverlays();
    if (settingsDirty) {
        currentPath = keepPath;
        loadDirectory();
        hideOverlays();
        settingsDirty = false;
    }
}

// ---------- rename / delete / mkdir ----------
void FileManagerPage::startRename() {
    if (cursor<0 || cursor >= (int)entries.size()) return;
    nameInput->setTitle("重命名");
    nameInput->setText(entries[cursor].name);
    nameInput->bVisible=true; inputNavBar->bVisible=true; overlay=OV_INPUT_RENAME;
}
void FileManagerPage::commitRename() {
    String newName = nameInput->getText(); newName.trim();
    if (newName.length()==0) { showResult("名称不能为空"); return; }
    if (newName.indexOf('/')>=0) { showResult("名称不能含/"); return; }
    String dst = joinPath(currentPath, newName);
    if (audioPlayer.getFS().exists(dst)) { showResult("目标已存在"); return; }
    bool ok = audioPlayer.getFS().rename(pendingSrcPath, dst);
    if (ok) { refreshAfterFsOp(); showResult("重命名成功"); }
    else showResult("重命名失败");
}
void FileManagerPage::confirmDelete(bool isBulk) {
    bulkOpPending = isBulk ? 3 : 0;
    String msg;
    if (isBulk) msg = "删除选中的 " + String(selectedCount()) + " 项?";
    else msg = "删除 " + entries[cursor].name + " ?";
    confirmBox->setMessage(msg); confirmBox->show(MSGBOX_ANIME_ZOOM_CENTER,300); confirmNavBar->bVisible=true; overlay=OV_CONFIRM_DELETE;
}
void FileManagerPage::doDeleteSingle() {
    String p = pendingSrcPath;
    String delName = (cursor>=0 && cursor<(int)entries.size()) ? entries[cursor].name : p.substring(p.lastIndexOf('/')+1);
    bool ok = false;
    File f = audioPlayer.getFS().open(p);
    if (f) { bool isDir = f.isDirectory(); f.close(); if (isDir) ok = deleteRecursive(p); else ok = audioPlayer.getFS().remove(p); }
    hideOverlays();
    // 先回浏览态(目录已变),再弹结果框:refreshAfterFsOp 内部会 hideOverlays,必须先做
    loadDirectory();
    if (ok) showResult("已删除 " + delName); else showResult("删除失败");
}
void FileManagerPage::doDeleteBulk() {
    int delN = selectedCount();
    hideOverlays();
    int cnt=0, fail=0;
    for (size_t i=0;i<entries.size();i++) if (selected[i]) {
        String p = joinPath(currentPath, entries[i].name);
        File f = audioPlayer.getFS().open(p);
        bool isDir=false; if (f){isDir=f.isDirectory(); f.close();}
        bool ok = isDir ? deleteRecursive(p) : audioPlayer.getFS().remove(p);
        if (ok) cnt++; else fail++;
    }
    // 先回浏览态(目录已变),再弹结果框:refreshAfterFsOp 内部会 hideOverlays,必须先做
    loadDirectory();
    if (fail==0) showResult("已删除 " + String(cnt) + " 项");
    else showResult("已删 " + String(cnt) + " 失败 " + String(fail));
    multiSelectMode=false;
}
bool FileManagerPage::deleteRecursive(const String& path) {
    File dir = audioPlayer.getFS().open(path);
    if (!dir) return false;
    if (!dir.isDirectory()) { dir.close(); return audioPlayer.getFS().remove(path); }
    File f = dir.openNextFile();
    while (f) {
        String name = f.name(); int slash=name.lastIndexOf('/'); if(slash>=0) name=name.substring(slash+1);
        String child = joinPath(path, name);
        bool isDir = f.isDirectory(); f.close();
        if (isDir) { if (!deleteRecursive(child)) { dir.close(); return false; } }
        else { if (!audioPlayer.getFS().remove(child)) { dir.close(); return false; } }
        f = dir.openNextFile();
    }
    dir.close();
    return audioPlayer.getFS().rmdir(path);
}

// ---------- copy/move ----------
bool FileManagerPage::copyFile(const String& src, const String& dst) {
    File in = audioPlayer.getFS().open(src, FILE_READ);
    if (!in) return false;
    File out = audioPlayer.getFS().open(dst, FILE_WRITE);
    if (!out) { in.close(); return false; }
    uint8_t buf[4096];
    while (in.available()) { size_t n = in.read(buf, sizeof(buf)); if (n==0) break; if (out.write(buf,n)!=n){ in.close(); out.close(); return false; } }
    in.close(); out.close(); return true;
}
bool FileManagerPage::copyDirRecursive(const String& src, const String& dst) {
    if (!audioPlayer.getFS().mkdir(dst)) return false;
    File dir = audioPlayer.getFS().open(src);
    if (!dir || !dir.isDirectory()) { dir.close(); return false; }
    File f = dir.openNextFile();
    while (f) {
        String name = f.name(); int slash=name.lastIndexOf('/'); if(slash>=0) name=name.substring(slash+1);
        bool isDir = f.isDirectory(); f.close();
        String s = joinPath(src, name); String d = joinPath(dst, name);
        bool ok = isDir ? copyDirRecursive(s,d) : copyFile(s,d);
        if (!ok) { dir.close(); return false; }
        f = dir.openNextFile();
    }
    dir.close(); return true;
}
void FileManagerPage::startCopyOrMove(bool isMove, bool bulk) { openPicker(isMove, bulk); }
void FileManagerPage::openPicker(bool isMove, bool bulk) {
    bulkOpPending = bulk ? (isMove?2:1) : (isMove?4:5);
    if (!bulk && pendingSrcPath.length() == 0 && cursor >= 0 && cursor < (int)entries.size())
        pendingSrcPath = joinPath(currentPath, entries[cursor].name);
    pickerPath = currentPath;
    pickerEntries.clear();
    File root = audioPlayer.getFS().open(pickerPath);
    if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f){ String n=f.name(); int sl=n.lastIndexOf('/'); if(sl>=0) n=n.substring(sl+1); bool isDir=f.isDirectory(); f.close(); if(isDir && n!="." && n!="..") pickerEntries.push_back({n,true,0}); f=root.openNextFile(); }
        root.close();
    }
    std::sort(pickerEntries.begin(), pickerEntries.end(), [](auto& a, auto& b){ String la=a.name, lb=b.name; la.toLowerCase(); lb.toLowerCase(); return la<lb; });
    for(int i=19;i>=0;i--) pickerMenu->removeMenuItem(i);
    pickerMenu->addMenuItem("[当前目录] " + pickerPath);
    for(auto& e: pickerEntries) pickerMenu->addMenuItem(">"+e.name);
    pickerMenu->menuSel=0; pickerMenu->bVisible=true; pickerNavBar->bVisible=true; overlay=OV_PICKER;
}
void FileManagerPage::pickerConfirm() {
    bool isMove = (bulkOpPending==2 || bulkOpPending==4);
    bool bulk = (bulkOpPending==1 || bulkOpPending==2);
    doCopyMove(isMove, bulk);
}
void FileManagerPage::doCopyMove(bool isMove, bool bulk) {
    hideOverlays();
    String dstDir = pickerPath;
    int okCnt=0,fail=0;
    if (!bulk) {
        String src = pendingSrcPath;
        String base = src.substring(src.lastIndexOf('/')+1);
        String dst = joinPath(dstDir, base);
        if (src == dst) { showResult("不能复制到自身"); return; }
        if (audioPlayer.getFS().exists(dst)) { showResult("目标已存在"); return; }
        File f = audioPlayer.getFS().open(src);
        bool isDir=false; if(f){isDir=f.isDirectory(); f.close();}
        bool ok = false;
        if (isDir) {
            if (dst.startsWith(src + "/")) { showResult("不能复制到子目录"); return; }
            ok = copyDirRecursive(src,dst);
            if (ok && isMove) ok = deleteRecursive(src);
        } else {
            if (isMove) ok = audioPlayer.getFS().rename(src,dst);
            else ok = copyFile(src,dst);
        }
        if (ok) okCnt=1; else fail=1;
    } else {
        for(size_t i=0;i<entries.size();i++) if(selected[i]){
            String src = joinPath(currentPath, entries[i].name);
            String dst = joinPath(dstDir, entries[i].name);
            if (src==dst) { fail++; continue; }
            if (audioPlayer.getFS().exists(dst)) { fail++; continue; }
            File f = audioPlayer.getFS().open(src);
            bool isDir=false; if(f){isDir=f.isDirectory(); f.close();}
            bool ok=false;
            if (isDir){
                if (dst.startsWith(src+"/")) { fail++; continue; }
                ok = copyDirRecursive(src,dst);
                if (ok && isMove) ok = deleteRecursive(src);
            } else {
                if (isMove) ok = audioPlayer.getFS().rename(src,dst);
                else ok = copyFile(src,dst);
            }
            if (ok) okCnt++; else fail++;
        }
        multiSelectMode=false;
    }
    refreshAfterFsOp();
    String act = isMove?"移动":"复制";
    showResult(act + " 完成 " + String(okCnt) + (fail?" 失败"+String(fail):""));
}

// ---------- unified op menu (Menu键) ----------
void FileManagerPage::openSysMenu() {
    for(int i=19;i>=0;i--) sysMenu->removeMenuItem(i);
    opItems.clear();
    bool bulk = hasSelection();
    if (bulk) {
        opItems.push_back("1.复制(" + String(selectedCount()) + "项)");
        opItems.push_back("2.移动(" + String(selectedCount()) + "项)");
        opItems.push_back("3.重命名");
        opItems.push_back("4.删除(" + String(selectedCount()) + "项)");
        opItems.push_back("5.属性");
    } else {
        opItems.push_back("1.复制");
        opItems.push_back("2.移动");
        opItems.push_back("3.重命名");
        opItems.push_back("4.删除");
        opItems.push_back("5.属性");
    }
    opItems.push_back("6.新建文件夹");
    opItems.push_back("7.设置");
    opItems.push_back("8.存储信息");
    for (auto& s : opItems) sysMenu->addMenuItem(s);
    sysMenu->menuSel=0; sysMenu->bVisible=true; overlay=OV_SYS_MENU;
}
void FileManagerPage::executeSysAction(int idx) {
    sysMenu->bVisible=false;
    if (idx < 0 || idx >= (int)opItems.size()) { overlay=OV_NONE; return; }
    bool bulk = hasSelection();
    switch(idx){
        case 0: // 复制
            if (bulk) { pendingSrcPath=""; openPicker(false,true); }
            else {
                if (entries.empty()) { overlay=OV_NONE; return; }
                pendingSrcPath = joinPath(currentPath, entries[cursor].name);
                openPicker(false,false);
            }
            return;
        case 1: // 移动
            if (bulk) { pendingSrcPath=""; openPicker(true,true); }
            else {
                if (entries.empty()) { overlay=OV_NONE; return; }
                pendingSrcPath = joinPath(currentPath, entries[cursor].name);
                openPicker(true,false);
            }
            return;
        case 2: // 重命名(仅单项)
            if (bulk) { showResult("重命名仅支持单项,请按3清除标记"); return; }
            if (entries.empty()) { overlay=OV_NONE; return; }
            pendingSrcPath = joinPath(currentPath, entries[cursor].name);
            startRename();
            return;
        case 3: // 删除
            if (entries.empty()) { overlay=OV_NONE; return; }
            if (bulk) { confirmDelete(true); }
            else { pendingSrcPath = joinPath(currentPath, entries[cursor].name); confirmDelete(false); }
            return;
        case 4: // 属性(文件/文件夹通用;文件夹 Enter 是进入,只能从这里看属性)
            if (entries.empty()) { overlay=OV_NONE; return; }
            pendingSrcPath = joinPath(currentPath, entries[cursor].name);
            openAttrPage();
            return;
        case 5: startMkdir(); return;
        case 6: openSettingsPage(); return;
        case 7: openStoragePage(); return;
        default: overlay=OV_NONE; return;
    }
}
void FileManagerPage::toggleMultiSelect() { multiSelectMode=true; selected.assign(entries.size(),false); overlay=OV_NONE; }
void FileManagerPage::toggleSelectCurrent(){ if(cursor<0||cursor>=(int)selected.size()) return; selected[cursor]=!selected[cursor]; }
void FileManagerPage::selectAll(bool s){ for(size_t i=0;i<selected.size();i++) selected[i]=s; }
void FileManagerPage::startMkdir(){ nameInput->setTitle("新建文件夹"); nameInput->setText(""); nameInput->bVisible=true; inputNavBar->bVisible=true; overlay=OV_INPUT_MKDIR; }
void FileManagerPage::commitMkdir(){
    String n=nameInput->getText(); n.trim();
    if(n.length()==0){ showResult("名称不能为空"); return; }
    if(n.indexOf('/')>=0){ showResult("名称不能含/"); return; }
    String p = joinPath(currentPath, n);
    if(audioPlayer.getFS().exists(p)){ showResult("已存在"); return; }
    bool ok = audioPlayer.getFS().mkdir(p);
    hideOverlays();
    if(ok){ refreshAfterFsOp(); showResult("已创建"); } else showResult("创建失败");
}

// ---------- update ----------
void FileManagerPage::update(){
    if(initPending){ if(millis()-initPendingAt < INIT_MIN_LOADING_MS) return; initPending=false; initHardware(); return; }
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR){ nameInput->update(); return; }
    if(overlay==OV_ATTR || overlay==OV_PROGRESS || overlay==OV_CONFIRM_DELETE){
        msgBox->update(); attrBox->update(); confirmBox->update();
        // 结果框自动关闭后(2.5秒),overlay 也要回浏览态,否则残留空白模态挡住Menu
        if(overlay==OV_PROGRESS && !msgBox->isVisible() && !msgBox->isAnimating()){ hideOverlays(); }
        return;
    }
    int act = pendingAction;
    if(act != FMA_NONE){ pendingAction=FMA_NONE;
        if(overlay==OV_SYS_MENU){
            if(act==FMA_UP) sysMenu->moveUp();
            else if(act==FMA_DOWN) sysMenu->moveDown();
            else if(act==FMA_ENTER){ int s=sysMenu->menuSel; sysMenu->getNavBar()->showRightBlink(1,80,80,[this,s](){ executeSysAction(s); }); }
            else if(act==FMA_BACK) hideOverlays();
            return;
        }
        if(overlay==OV_FILE_MENU){
            if(act==FMA_UP) fileMenu->moveUp();
            else if(act==FMA_DOWN) fileMenu->moveDown();
            else if(act==FMA_ENTER){ int s=fileMenu->menuSel; fileMenu->getNavBar()->showRightBlink(1,80,80,[this,s](){ executeFileAction(s); }); }
            else if(act==FMA_BACK){ hideOverlays(); }
            return;
        }
        if(overlay==OV_PICKER){
            if(act==FMA_UP) pickerMenu->moveUp();
            else if(act==FMA_DOWN) pickerMenu->moveDown();
            else if(act==FMA_ENTER){
                int s=pickerMenu->menuSel;
                if(s==0){ pickerConfirm(); }
                else {
                    int idx=s-1; if(idx>=0 && idx < (int)pickerEntries.size()){
                        pickerPath = joinPath(pickerPath, pickerEntries[idx].name);
                        pickerEntries.clear();
                        File root = audioPlayer.getFS().open(pickerPath);
                        if(root && root.isDirectory()){
                            File f=root.openNextFile();
                            while(f){ String n=f.name(); int sl=n.lastIndexOf('/'); if(sl>=0) n=n.substring(sl+1); bool isDir=f.isDirectory(); f.close(); if(isDir && n!="."&&n!="..") pickerEntries.push_back({n,true,0}); f=root.openNextFile(); }
                            root.close();
                        }
                        std::sort(pickerEntries.begin(), pickerEntries.end(), [](auto& a, auto& b){ String la=a.name, lb=b.name; la.toLowerCase(); lb.toLowerCase(); return la<lb; });
                        for(int i=19;i>=0;i--) pickerMenu->removeMenuItem(i);
                        pickerMenu->addMenuItem("[当前目录] " + pickerPath);
                        for(auto& e: pickerEntries) pickerMenu->addMenuItem(">"+e.name);
                        pickerMenu->menuSel=0;
                    }
                }
            } else if(act==FMA_BACK){
                if(pickerPath != "/"){ pickerPath=getParentPath(pickerPath); pickerEntries.clear(); File root=audioPlayer.getFS().open(pickerPath); if(root&&root.isDirectory()){ File f=root.openNextFile(); while(f){String n=f.name(); int sl=n.lastIndexOf('/'); if(sl>=0) n=n.substring(sl+1); bool isDir=f.isDirectory(); f.close(); if(isDir && n!="."&&n!="..") pickerEntries.push_back({n,true,0}); f=root.openNextFile();} root.close();} std::sort(pickerEntries.begin(), pickerEntries.end(), [](auto& a, auto& b){String la=a.name, lb=b.name; la.toLowerCase(); lb.toLowerCase(); return la<lb;}); for(int i=19;i>=0;i--) pickerMenu->removeMenuItem(i); pickerMenu->addMenuItem("[当前目录] "+pickerPath); for(auto& e: pickerEntries) pickerMenu->addMenuItem(">"+e.name); pickerMenu->menuSel=0; }
                else hideOverlays();
            } else if(act==FMA_MENU) pickerConfirm();
            return;
        }
        if(overlay==OV_ATTR_PAGE){
            // 行数由最近一次渲染的折行结果决定(update无显示句柄)
            int maxScroll = attrRowCount - 4;
            if (maxScroll < 0) maxScroll = 0;
            if(act==FMA_UP){ if(attrScroll>0) attrScroll--; }
            else if(act==FMA_DOWN){ if(attrScroll<maxScroll) attrScroll++; }
            else if(act==FMA_PAGEUP){ attrScroll-=4; if(attrScroll<0) attrScroll=0; }
            else if(act==FMA_PAGEDOWN){ attrScroll+=4; if(attrScroll>maxScroll) attrScroll=maxScroll; }
            else if(act==FMA_LEFT){ attrHScroll-=8; if(attrHScroll<0) attrHScroll=0; }
            else if(act==FMA_RIGHT){ attrHScroll+=8; if(attrHScroll>attrMaxHScroll) attrHScroll=attrMaxHScroll; }
            else if(act==FMA_ENTER || act==FMA_BACK || act==FMA_MENU) hideOverlays();
            return;
        }
        if(overlay==OV_STORAGE_PAGE){
            if(act==FMA_BACK || act==FMA_ENTER || act==FMA_MENU) hideOverlays();
            return;
        }
        if(overlay==OV_SETTINGS_PAGE){
            if(act==FMA_UP){ settingsSel--; if(settingsSel<0) settingsSel=1; }
            else if(act==FMA_DOWN){ settingsSel++; if(settingsSel>1) settingsSel=0; }
            else if(act==FMA_PAGEUP) settingsChangeValue(-1);
            else if(act==FMA_PAGEDOWN) settingsChangeValue(1);
            else if(act==FMA_BACK) { exitSettingsPage(); }
            else if(act==FMA_ENTER) { /* Enter不改值,保持专业设置语义 */ }
            else if(act==FMA_MENU) { exitSettingsPage(); }
            return;
        }
        // browse
        if(browserState==FM_BROWSE){
            switch(act){
                case FMA_UP: moveCursor(-1); break;
                case FMA_DOWN: moveCursor(1); break;
                case FMA_PAGEUP: pageUp(); break;
                case FMA_PAGEDOWN: pageDown(); break;
                case FMA_ENTER: enterItem(); break;
                case FMA_BACK: handleBack(); break;
                case FMA_MENU: openSysMenu(); break;
                case FMA_MARK: toggleSelectSticky(); break;
                case FMA_CLEAR_MARK: clearSelection(); break;
                case FMA_RETRY: retryInit(); break;
                default:break;
            }
        } else {
            if(act==FMA_BACK) handleBack();
            else if(act==FMA_MENU || act==FMA_RETRY) retryInit();
        }
    }
    if(browserState==FM_BROWSE && overlay==OV_NONE) updateSelScroll();
}

void FileManagerPage::updateSelScroll(){
    if(entries.empty()||cursor<0||cursor>=(int)entries.size()) return;
    if(selNameWidth <= NAME_VIEW_W) return;
    if(entries[cursor].name != selFullName) return;
    int maxOff = selNameWidth - NAME_VIEW_W; unsigned long now=millis();
    switch(selScrollPhase){ case 0: if(now - selScrollTimer > 1000){ selScrollPhase=1; selScrollTimer=now; } break; case 1: if(now - selScrollTimer > 120){ selScrollTimer=now; selScrollOffset+=6; if(selScrollOffset>=maxOff){ selScrollOffset=maxOff; selScrollPhase=2; selScrollTimer=now; } } break; case 2: if(now - selScrollTimer > 1500){ selScrollOffset=0; selScrollPhase=0; selScrollTimer=now; } break; }
}

// ---------- render ----------
void FileManagerPage::render(U8G2* u8g2){
    if(overlay==OV_ATTR_PAGE){ drawAttrPage(u8g2); return; }
    if(overlay==OV_STORAGE_PAGE){ drawStoragePage(u8g2); return; }
    if(overlay==OV_SETTINGS_PAGE){ drawSettingsPage(u8g2); return; }
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR || overlay==OV_CONFIRM_DELETE || overlay==OV_ATTR || overlay==OV_PROGRESS || overlay==OV_FILE_MENU || overlay==OV_SYS_MENU || overlay==OV_PICKER){
        UIPage::render(u8g2); return;
    }
    switch(browserState){
        case FM_INIT: drawErrorUI(u8g2, "读取TF卡中...", "请稍候"); break;
        case FM_NO_CARD: drawErrorUI(u8g2, "未插入TF卡", "插卡后按[菜单]键重试"); break;
        case FM_NO_CODEC: drawErrorUI(u8g2, "音频芯片无响应", "请检查硬件后重试"); break;
        case FM_BROWSE: drawBrowseUI(u8g2); break;
    }
}
void FileManagerPage::drawErrorUI(U8G2* u8g2, const char* title, const char* hint){
    u8g2->setFont(u8g2_font_siji_t_6x10); int iconW=8; u8g2->drawGlyph((SCREEN_WIDTH - iconW)/2, 22, GLYPH_SDCARD);
    u8g2->setFont(u8g2_font_wqy12_t_gb2312); uint16_t tw=u8g2->getUTF8Width(title); u8g2->drawUTF8((SCREEN_WIDTH - tw)/2, 39, title);
    uint16_t hw=u8g2->getUTF8Width(hint); u8g2->drawUTF8((SCREEN_WIDTH - hw)/2, 55, hint);
}
static String truncateUTF8FM(const String& s, int maxW, U8G2* u8g2){ if(u8g2->getUTF8Width(s.c_str())<=maxW) return s; String out=s; while(out.length()>1){ int cut=out.length()-1; while(cut>0 && (out.charAt(cut)&0xC0)==0x80) cut--; out=out.substring(0,cut); if(u8g2->getUTF8Width((out+"..").c_str())<=maxW) break; } return out+".."; }

void FileManagerPage::drawBrowseUI(U8G2* u8g2){
    // 标题栏(无底部栏,列表独占剩余高度)
    u8g2->setFont(u8g2_font_siji_t_6x10); u8g2->drawGlyph(3,11,(currentPath=="/")?GLYPH_SDCARD:GLYPH_FOLDER);
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);
    String title; if(currentPath=="/") title="TF卡"; else { int s=currentPath.lastIndexOf('/'); title=currentPath.substring(s+1); }
    int selN = selectedCount();
    String countText = String(entries.size())+"项" + (selN>0 ? " 已选"+String(selN) : "");
    uint16_t countW=u8g2->getUTF8Width(countText.c_str()); u8g2->drawUTF8(SCREEN_WIDTH-3-countW,11,countText.c_str());
    int maxTitleW = SCREEN_WIDTH - 20 - countW -6; title=truncateUTF8FM(title,maxTitleW,u8g2); u8g2->drawUTF8(17,11,title.c_str());
    u8g2->drawHLine(0,13,SCREEN_WIDTH);
    if(entries.empty()){ u8g2->setFont(u8g2_font_wqy12_t_gb2312); const char* empty="(空目录)"; uint16_t ew=u8g2->getUTF8Width(empty); u8g2->drawUTF8((SCREEN_WIDTH-ew)/2,36,empty);
        // 底部操作提示(不遮挡列表,仅空目录时居中提示)
        u8g2->setFont(u8g2_font_4x6_tf); const char* tip="1:mark 3:clear MENU:op"; uint16_t tw=u8g2->getUTF8Width(tip); u8g2->drawStr((SCREEN_WIDTH-tw)/2,62,tip); return; }
    for(int i=0;i<VISIBLE_ROWS;i++){ int idx=viewStart+i; if(idx>=(int)entries.size()) break; int y=14+i*12; bool isCursor=(idx==cursor); bool marked = (idx>=0 && idx<(int)selected.size() && selected[idx]);
        if(isCursor){
            u8g2->setDrawColor(1); int selW=SCREEN_WIDTH; if((int)entries.size()>VISIBLE_ROWS) selW=SCREEN_WIDTH-6; u8g2->drawBox(0,y,selW,12);
        } else if(marked){
            // 已标记但非光标:空心框保持"亮着"
            u8g2->setDrawColor(1); int selW=SCREEN_WIDTH; if((int)entries.size()>VISIBLE_ROWS) selW=SCREEN_WIDTH-6; u8g2->drawFrame(0,y,selW,12);
        }
        u8g2->setFontMode(1); u8g2->setDrawColor(isCursor?0:1);
        u8g2->setFont(u8g2_font_siji_t_6x10);
        u8g2->drawGlyph(4, y+9, entries[idx].isDir?GLYPH_FOLDER:GLYPH_FILE);
        // 标记勾选
        if(marked){
            u8g2->setFont(u8g2_font_4x6_tf);
            u8g2->drawStr(12, y+9, "x");
            u8g2->setFont(u8g2_font_siji_t_6x10);
        }
        u8g2->setFont(u8g2_font_wqy12_t_gb2312);
        const String& name=entries[idx].name;
        int nameX = marked ? 20 : 18;
        int viewW = NAME_VIEW_W - (marked ? 2 : 0);
        if(isCursor){
            if(selFullName!=name){ selFullName=name; selNameWidth=u8g2->getUTF8Width(name.c_str()); selScrollOffset=0; selScrollPhase=0; selScrollTimer=millis(); }
            if(selNameWidth <= viewW){ u8g2->drawUTF8(nameX, y+10, name.c_str()); }
            else { u8g2->setClipWindow(nameX, y, nameX+viewW, y+12); u8g2->drawUTF8(nameX - selScrollOffset, y+10, name.c_str()); u8g2->setMaxClipWindow(); }
        } else {
            String cut=truncateUTF8FM(name, SCREEN_WIDTH - nameX -8, u8g2); u8g2->drawUTF8(nameX, y+10, cut.c_str());
        }
        u8g2->setFontMode(0); if(isCursor||marked) u8g2->setDrawColor(1);
    }
    if(entries.size()>VISIBLE_ROWS){ int trackY=14,trackH=VISIBLE_ROWS*12; u8g2->drawVLine(SCREEN_WIDTH-3,trackY,trackH); int thumbH=trackH*VISIBLE_ROWS/entries.size(); if(thumbH<6) thumbH=6; int maxStart=entries.size()-VISIBLE_ROWS; int thumbY=trackY+(trackH-thumbH)*viewStart/maxStart; u8g2->drawBox(SCREEN_WIDTH-5,thumbY,5,thumbH); }
}

// ---------- 属性独立页 ----------
void FileManagerPage::drawAttrPage(U8G2* u8g2){
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);
    // 正文区:左侧x=4;右侧给滚动条预留 7px(框线x122 + 滑块x123~127),
    // 折行与横滚都按这个宽度算,文字永远不进滚动条下方
    const int textX = 4;
    const int sbReserve = 7;
    const int viewW = SCREEN_WIDTH - textX - sbReserve - 2;
    // 渲染时按当前字体折行(键后超宽的值自动换到缩进续行)
    wrapAttrRows(u8g2, attrKeys, attrVals, attrRows, viewW);
    attrRowCount = (int)attrRows.size();
    int maxScroll = attrRowCount - 4; if (maxScroll < 0) maxScroll = 0;
    if (attrScroll > maxScroll) attrScroll = maxScroll;

    // 计算可见行最大内容宽度 -> 横向可滚上限
    int widest = 0;
    for (int i = 0; i < 4; i++) {
        int li = attrScroll + i; if (li >= attrRowCount) break;
        int w = u8g2->getUTF8Width(attrRows[li].c_str());
        if (w > widest) widest = w;
    }
    attrMaxHScroll = widest - viewW;
    if (attrMaxHScroll < 0) attrMaxHScroll = 0;
    if (attrHScroll > attrMaxHScroll) attrHScroll = attrMaxHScroll;

    // 标题: 图标 + 文件名(截断) + 行区间
    bool isDir = (cursor>=0 && cursor<(int)entries.size()) ? entries[cursor].isDir : false;
    u8g2->setFont(u8g2_font_siji_t_6x10);
    u8g2->drawGlyph(3,11, isDir?GLYPH_FOLDER:GLYPH_FILE);
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);
    String t = truncateUTF8FM(attrTitle, SCREEN_WIDTH-40, u8g2);
    u8g2->drawUTF8(17,11,t.c_str());
    String pg = "";
    if(attrRowCount>4) pg = String(attrScroll+1)+"-"+String(attrScroll+4)+"/"+String(attrRowCount);
    else if(attrRowCount>0) pg = String(attrRowCount)+"行";
    uint16_t pw = u8g2->getUTF8Width(pg.c_str());
    u8g2->drawUTF8(SCREEN_WIDTH-3-pw,11,pg.c_str());
    u8g2->drawHLine(0,13,SCREEN_WIDTH);

    // 正文:裁剪窗口限定在滚动条左侧,横滚也不进滚动条下方
    for(int i=0;i<4;i++){
        int li = attrScroll+i; if(li>=attrRowCount) break;
        int y = 14+i*12;
        u8g2->setFont(u8g2_font_wqy12_t_gb2312);
        u8g2->setClipWindow(textX, y, textX+viewW, y+12);
        u8g2->drawUTF8(textX - attrHScroll, y+10, attrRows[li].c_str());
        u8g2->setMaxClipWindow();
    }
    // 滚动条
    if(maxScroll > 0){
        int trackY=14,trackH=4*12;
        u8g2->drawVLine(SCREEN_WIDTH-3,trackY,trackH);
        int thumbH=trackH*4/attrRowCount; if(thumbH<6) thumbH=6;
        int thumbY=trackY+(trackH-thumbH)*attrScroll/maxScroll;
        u8g2->drawBox(SCREEN_WIDTH-5,thumbY,5,thumbH);
    }
    // 底部按键提示已去除:返回/2/8/4/6 均为全局约定,无需常驻占行
}

// ---------- 存储信息独立页(饼图) ----------
void FileManagerPage::drawStoragePage(U8G2* u8g2){
    u8g2->setFont(u8g2_font_siji_t_6x10);
    u8g2->drawGlyph(3,11,GLYPH_SDCARD);
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);
    u8g2->drawUTF8(17,11,"存储信息");
    u8g2->drawHLine(0,13,SCREEN_WIDTH);

    uint64_t total = SD_MMC.totalBytes();
    uint64_t used = SD_MMC.usedBytes();
    uint64_t freeB = (total>used)?(total-used):0;
    float ratio = total>0 ? (float)used/(float)total : 0;
    int pct = (int)(ratio*100+0.5f);

    // 左:饼图(圆心22,38 半径17)
    int cx=24, cy=38, r=17;
    u8g2->drawCircle(cx,cy,r);
    // 已用扇形(从12点顺时针)
    int steps = (int)(360*ratio);
    for(int a=0;a<steps;a+=2){
        float rad = (-90 + a) * 3.14159265f / 180.0f;
        int x = cx + (int)(r*cosf(rad));
        int y = cy + (int)(r*sinf(rad));
        u8g2->drawLine(cx,cy,x,y);
    }
    // 中心白点做 donut 效果,百分比写中间(小字体)
    u8g2->setDrawColor(0); u8g2->drawDisc(cx,cy,9); u8g2->setDrawColor(1);
    u8g2->drawCircle(cx,cy,9);
    u8g2->setFont(u8g2_font_4x6_tf);
    char pb[8]; snprintf(pb,sizeof(pb),"%d%%",pct);
    uint16_t pw = u8g2->getUTF8Width(pb);
    u8g2->drawStr(cx-pw/2, cy+2, pb);

    // 右:文字区(小字英文数字 + 中文标签)
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);
    auto fmtMB = [](uint64_t b)->String{
        if(b>=1024ULL*1024ULL*1024ULL) return String(b/1024.0/1024.0/1024.0,2)+"GB";
        return String((unsigned long)(b/1024/1024))+"MB";
    };
    String l1 = "总量:" + fmtMB(total);
    String l2 = "已用:" + fmtMB(used);
    String l3 = "可用:" + fmtMB(freeB);
    // 卡类型(Arduino-ESP32 SD_MMC 仅定义 MMC/SD/SDHC/UNKNOWN)
    String cardT = "卡:SD";
    sdcard_type_t ct = SD_MMC.cardType();
    if(ct==CARD_MMC) cardT="卡:MMC";
    else if(ct==CARD_SD) cardT="卡:SDSC";
    else if(ct==CARD_SDHC) cardT="卡:SDHC";
    else if(ct==CARD_UNKNOWN) cardT="卡:未知";
    // 分区格式:FAT系列(ESP32仅支持FAT/exFAT挂载)
    String fsT = "格式:FAT32/exFAT";
    u8g2->drawUTF8(46, 26, truncateUTF8FM(l1, SCREEN_WIDTH-48, u8g2).c_str());
    u8g2->drawUTF8(46, 38, truncateUTF8FM(l2, SCREEN_WIDTH-48, u8g2).c_str());
    u8g2->drawUTF8(46, 50, truncateUTF8FM(l3, SCREEN_WIDTH-48, u8g2).c_str());
    u8g2->setFont(u8g2_font_4x6_tf);
    u8g2->drawUTF8(46, 60, (cardT+" "+fsT).c_str());
}

// ---------- 设置独立页(4/6改值) ----------
void FileManagerPage::drawSettingsPage(U8G2* u8g2){
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);
    const char* ttl = "设置";
    uint16_t tw = u8g2->getUTF8Width(ttl);
    u8g2->drawUTF8((SCREEN_WIDTH-tw)/2, 11, ttl);
    u8g2->drawHLine(0,13,SCREEN_WIDTH);

    String sortV = (sortMode==SORT_NAME)?"名称":(sortMode==SORT_SIZE?"大小":"时间");
    String hidV = showHidden?"开":"关";
    struct Row { const char* label; String val; };
    Row rows[2] = { {"排序", sortV}, {"隐藏文件", hidV} };
    for(int i=0;i<2;i++){
        int y = 16+i*17;
        bool sel = (i==settingsSel);
        if(sel){ u8g2->setDrawColor(1); u8g2->drawBox(0,y,SCREEN_WIDTH,15); }
        u8g2->setFontMode(1); u8g2->setDrawColor(sel?0:1);
        u8g2->setFont(u8g2_font_wqy12_t_gb2312);
        u8g2->drawUTF8(6, y+11, rows[i].label);
        // 右侧值框
        String v = "<"+rows[i].val+">";
        uint16_t vw = u8g2->getUTF8Width(v.c_str());
        int vx = SCREEN_WIDTH-6-vw;
        if(sel){
            u8g2->drawFrame(vx-3, y+2, vw+6, 11);
        }
        u8g2->drawUTF8(vx, y+11, v.c_str());
        u8g2->setFontMode(0); u8g2->setDrawColor(1);
    }
    u8g2->setFont(u8g2_font_4x6_tf);
    u8g2->drawStr(6, 62, "2/8:move 4/6:change");
}

// ---------- buttons ----------
void FileManagerPage::onButtonBack(void* context){
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR){ hideOverlays(); return; }
    if(overlay==OV_ATTR || overlay==OV_PROGRESS){ hideOverlays(); return; }
    if(overlay==OV_ATTR_PAGE || overlay==OV_STORAGE_PAGE){ hideOverlays(); return; }
    if(overlay==OV_SETTINGS_PAGE){ exitSettingsPage(); return; }
    if(overlay==OV_CONFIRM_DELETE){ hideOverlays(); return; }
    if(overlay==OV_FILE_MENU || overlay==OV_SYS_MENU){ hideOverlays(); return; }
    if(overlay==OV_PICKER){ pendingAction=FMA_BACK; return; }
    pendingAction=FMA_BACK;
}
void FileManagerPage::onButtonMenu(void* context){
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR){ nameInput->deleteChar(); return; }
    if(overlay==OV_PROGRESS){ hideOverlays(); return; } // 结果提示框:Menu键直接关闭
    if(overlay==OV_ATTR || overlay==OV_CONFIRM_DELETE) return;
    if(overlay==OV_ATTR_PAGE || overlay==OV_STORAGE_PAGE || overlay==OV_SETTINGS_PAGE) return;
    if(overlay==OV_FILE_MENU || overlay==OV_SYS_MENU || overlay==OV_PICKER) return;
    if(browserState==FM_NO_CARD || browserState==FM_NO_CODEC) pendingAction=FMA_RETRY;
    else pendingAction=FMA_MENU;
}
void FileManagerPage::onButtonEnter(void* context){
    if(overlay==OV_INPUT_RENAME){ inputNavBar->showRightBlink(1,80,80,[this](){ commitRename(); }); return; }
    if(overlay==OV_INPUT_MKDIR){ inputNavBar->showRightBlink(1,80,80,[this](){ commitMkdir(); }); return; }
    if(overlay==OV_ATTR){ attrBox->hide(true); hideOverlays(); return; }
    if(overlay==OV_ATTR_PAGE || overlay==OV_STORAGE_PAGE){ hideOverlays(); return; }
    if(overlay==OV_SETTINGS_PAGE){ return; } // Enter不改值
    if(overlay==OV_PROGRESS){ hideOverlays(); return; }
    if(overlay==OV_CONFIRM_DELETE){
        confirmNavBar->showRightBlink(1,80,80,[this](){
            if(bulkOpPending==3) doDeleteBulk(); else doDeleteSingle();
        }); return;
    }
    pendingAction=FMA_ENTER;
}
void FileManagerPage::onButton2(void* context){
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR) return;
    if(overlay==OV_SETTINGS_PAGE){ pendingAction=FMA_UP; return; }
    pendingAction=FMA_UP;
}
void FileManagerPage::onButton1(void* context){
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR){ nameInput->onButtonPress(1); return; }
    if(overlay==OV_NONE && browserState==FM_BROWSE){ pendingAction=FMA_MARK; return; }
    if(overlay==OV_SYS_MENU || overlay==OV_PICKER || overlay==OV_FILE_MENU) return;
    pendingAction=FMA_UP;
}
void FileManagerPage::onButton3(void* context){
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR){ nameInput->onButtonPress(3); return; }
    if(overlay==OV_NONE && browserState==FM_BROWSE){ pendingAction=FMA_CLEAR_MARK; return; }
}
void FileManagerPage::onButton4(void* context){
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR){ nameInput->onButtonPress(4); return; }
    if(overlay==OV_SETTINGS_PAGE){ pendingAction=FMA_PAGEUP; return; } // 4=改值-
    if(overlay==OV_ATTR_PAGE){ pendingAction=FMA_LEFT; return; }        // 4=横滚左
    pendingAction=FMA_PAGEUP;
}
void FileManagerPage::onButton5(void* context){
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR){ nameInput->onButtonPress(5); return; }
    if(overlay==OV_SETTINGS_PAGE) return; // Enter语义:不改值
    onButtonEnter(context);
}
void FileManagerPage::onButton6(void* context){
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR){ nameInput->onButtonPress(6); return; }
    if(overlay==OV_SETTINGS_PAGE){ pendingAction=FMA_PAGEDOWN; return; } // 6=改值+
    if(overlay==OV_ATTR_PAGE){ pendingAction=FMA_RIGHT; return; }        // 6=横滚右
    pendingAction=FMA_PAGEDOWN;
}
void FileManagerPage::onButton7(void* context){ if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR) nameInput->onButtonPress(7); }
void FileManagerPage::onButton8(void* context){
    if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR){ nameInput->onButtonPress(8); return; }
    if(overlay==OV_SETTINGS_PAGE){ pendingAction=FMA_DOWN; return; }
    pendingAction=FMA_DOWN;
}
void FileManagerPage::onButton9(void* context){ if(overlay==OV_INPUT_RENAME || overlay==OV_INPUT_MKDIR) nameInput->onButtonPress(9); }
