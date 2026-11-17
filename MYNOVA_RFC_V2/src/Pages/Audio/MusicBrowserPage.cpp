/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// MusicBrowserPage.cpp
#include "MusicBrowserPage.h"
#include "MusicPlayerPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Drivers/AudioPlayer.h"
#include "../../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "BROWSE"

extern UIEngine uiEngine;

// ================= Siji字体图标 =================
// 文件夹57814 空白文件57807 音符57970 TF卡57816 (睡眠时钟57366)
// 绘制: setFont(u8g2_font_siji_t_6x10) + drawGlyph(x, baseline, code)
#define GLYPH_FOLDER  57814
#define GLYPH_NOTE    57970
#define GLYPH_SDCARD  57816

MusicBrowserPage::MusicBrowserPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    navBar = new UINavBar(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12);
    navBar->setLeftButtonText("返回");
    navBar->setRightButtonText("进入");
    addWidget(navBar);
}

MusicBrowserPage::~MusicBrowserPage() {
    // navBar由UIPage基类释放
}

void MusicBrowserPage::showPage() {
    // 从播放页返回时保持浏览状态(BROWSER_BROWSE)
    // 首次进入不阻塞: 先显示"读取TF卡中..."界面, 实际扫描由渲染任务的update()执行
    if (browserState != BROWSER_BROWSE) {
        browserState = BROWSER_INIT;
        initPending = true;
        initPendingAt = millis();
    }
}

void MusicBrowserPage::initHardware() {
    browserState = BROWSER_INIT;  // 渲染任务会显示"读取TF卡中..."界面
    LOG_I("MBP[初始化] 开始检测TF卡/音频硬件");
    AudioInitState st = audioPlayer.init();
    LOG_I("MBP[初始化] 结果=%d(0未init 1OK 2无卡 3无芯片)", (int)st);
    if (st == AUDIO_INIT_OK) {
        currentPath = "/";
        navStack.clear();
        cursor = 0;
        viewStart = 0;
        loadDirectory();
    } else if (st == AUDIO_INIT_NO_CARD) {
        browserState = BROWSER_NO_CARD;
    } else {
        browserState = BROWSER_NO_CODEC;
    }
    initPending = false;
}

void MusicBrowserPage::retryInit() {
    // 热插拔重试: 强制重新检测(状态切到读取中, 由update()延迟执行扫描, 先展示loading)
    browserState = BROWSER_INIT;
    initPending = true;
    initPendingAt = millis();
}

String MusicBrowserPage::joinPath(const String& dir, const String& name) const {
    if (dir == "/") return "/" + name;
    return dir + "/" + name;
}

void MusicBrowserPage::loadDirectory() {
    entries.clear();
    cursor = 0;
    viewStart = 0;
    selFullName = "";   // 失效滚动上下文(render时按新选中项重建)

    File root = audioPlayer.getFS().open(currentPath);
    if (!root || !root.isDirectory()) {
        // 目录打开失败: 可能卡被移除, 强制重新挂载(限制重试次数防死循环)
        audioPlayer.resetCard();
        currentPath = "/";
        navStack.clear();
        if (++loadRetries <= 2) {
            initHardware();  // 内部会再次loadDirectory
        } else {
            browserState = BROWSER_NO_CARD;
        }
        return;
    }
    loadRetries = 0;

    // 第一遍: 收集(文件夹 + 音频文件)
    std::vector<DirEntry> dirs, files;
    File file = root.openNextFile();
    while (file) {
        String name = file.name();  // core 3.x: 只返回文件名
        bool isDir = file.isDirectory();
        file.close();
        if (name != "." && name != ".." && !name.startsWith(".")) {
            DirEntry e;
            e.name = name;
            e.isDir = isDir;
            if (e.isDir) {
                dirs.push_back(e);
            } else if (AudioPlayer::isAudioFile(name)) {
                files.push_back(e);
            }
        }
        file = root.openNextFile();
    }
    root.close();

    // 排序: 名称升序(不区分大小写)
    auto nameLess = [](const DirEntry& a, const DirEntry& b) {
        String la = a.name, lb = b.name;
        la.toLowerCase();
        lb.toLowerCase();
        return la < lb;
    };
    std::sort(dirs.begin(), dirs.end(), nameLess);
    std::sort(files.begin(), files.end(), nameLess);

    // 文件夹在前
    entries.reserve(dirs.size() + files.size());
    for (auto& e : dirs) entries.push_back(e);
    for (auto& e : files) entries.push_back(e);

    browserState = BROWSER_BROWSE;
    LOG_I("MBP[目录] %s: %d文件夹+%d文件",
          currentPath.c_str(), (int)dirs.size(), (int)files.size());
}

void MusicBrowserPage::moveCursor(int delta) {
    if (entries.empty()) return;
    cursor += delta;
    if (cursor < 0) cursor = entries.size() - 1;  // 首尾循环
    if (cursor >= (int)entries.size()) cursor = 0;
    // 视口跟随
    if (cursor < viewStart) viewStart = cursor;
    if (cursor >= viewStart + VISIBLE_ROWS) viewStart = cursor - VISIBLE_ROWS + 1;
    if ((int)entries.size() <= VISIBLE_ROWS) viewStart = 0;
}

void MusicBrowserPage::pageUp() {
    if (entries.empty()) return;
    if (viewStart == 0) {
        // 已在第一页: 光标跳到第一项
        cursor = 0;
        viewStart = 0;
        return;
    }
    int rel = cursor - viewStart;   // 光标保持视口内相对位置
    viewStart = (viewStart - VISIBLE_ROWS < 0) ? 0 : viewStart - VISIBLE_ROWS;
    cursor = viewStart + rel;
}

void MusicBrowserPage::pageDown() {
    if (entries.empty()) return;
    int maxStart = (int)entries.size() - VISIBLE_ROWS;
    if (maxStart < 0) maxStart = 0;
    if (viewStart >= maxStart) {
        // 已在最后一页: 光标跳到最后一项
        cursor = entries.size() - 1;
        viewStart = maxStart;
        return;
    }
    int rel = cursor - viewStart;   // 光标保持视口内相对位置
    viewStart = (viewStart + VISIBLE_ROWS > maxStart) ? maxStart : viewStart + VISIBLE_ROWS;
    cursor = viewStart + rel;
    if (cursor >= (int)entries.size()) cursor = entries.size() - 1;
}

void MusicBrowserPage::enterItem() {
    if (entries.empty() || cursor < 0 || cursor >= (int)entries.size()) return;

    const DirEntry& e = entries[cursor];
    if (e.isDir) {
        LOG_I("MBP[进入] 文件夹 %s", e.name.c_str());
        navStack.push_back({cursor, viewStart});   // 记住进入前的位置, 返回时恢复
        currentPath = joinPath(currentPath, e.name);
        loadDirectory();
        return;
    }

    // 构建当前目录的音频播放列表(全路径)
    std::vector<String> playlist;
    int playIndex = -1;
    for (size_t i = 0; i < entries.size(); i++) {
        if (!entries[i].isDir) {
            if ((int)i == cursor) playIndex = playlist.size();
            playlist.push_back(joinPath(currentPath, entries[i].name));
        }
    }
    if (playIndex < 0 || playlist.empty()) return;

    LOG_I("MBP[进入] 播放列表%d首, 起播idx=%d(%s)",
          (int)playlist.size(), playIndex, e.name.c_str());
    navBar->showRightBlink(1, 80, 80, [this, playlist, playIndex]() {
        uiEngine.navigateTo(new MusicPlayerPage(playlist, playIndex));
    });
}

void MusicBrowserPage::goBack() {
    if (currentPath == "/") {
        // 已在根目录, 退出音乐浏览
        navBar->showLeftBlink(1, 80, 80, [this]() {
            uiEngine.navigateBack();
        });
    } else {
        // 返回上级目录
        int lastSlash = currentPath.lastIndexOf('/');
        currentPath = (lastSlash == 0) ? "/" : currentPath.substring(0, lastSlash);
        loadDirectory();
        restoreNavPos();   // 恢复进入该文件夹前的光标与视口位置
    }
}

void MusicBrowserPage::restoreNavPos() {
    if (navStack.empty()) return;   // 无记录(如异常重挂载后), 保持loadDirectory的默认顶部位置
    NavPos pos = navStack.back();
    navStack.pop_back();

    if (entries.empty()) return;
    // TF卡内容若已变化, 索引可能越界: 收敛到合法范围
    if (pos.cursor < 0) pos.cursor = 0;
    if (pos.cursor >= (int)entries.size()) pos.cursor = entries.size() - 1;

    int maxStart = (int)entries.size() - VISIBLE_ROWS;
    if (maxStart < 0) maxStart = 0;
    if (pos.viewStart < 0) pos.viewStart = 0;
    if (pos.viewStart > maxStart) pos.viewStart = maxStart;

    cursor = pos.cursor;
    viewStart = pos.viewStart;
    // 保证光标可见(与moveCursor相同的视口跟随规则)
    if (cursor < viewStart) viewStart = cursor;
    if (cursor >= viewStart + VISIBLE_ROWS) viewStart = cursor - VISIBLE_ROWS + 1;
}

void MusicBrowserPage::handleBack() {
    if (browserState != BROWSER_BROWSE) {
        // 错误提示状态: 直接退出页面
        navBar->showLeftBlink(1, 80, 80, [this]() {
            uiEngine.navigateBack();
        });
    } else {
        goBack();
    }
}

// ================= 按键(按键任务调用, 只投递请求) =================
// 所有会修改entries/导航栈的操作延迟到渲染任务的update()执行, 避免与render并发崩溃
void MusicBrowserPage::onButtonBack(void* context) {
    pendingAction = BA_BACK;
}

void MusicBrowserPage::onButtonMenu(void* context) {
    if (browserState == BROWSER_NO_CARD || browserState == BROWSER_NO_CODEC) {
        pendingAction = BA_RETRY;   // 热插拔重试
    } else {
        pendingAction = BA_DOWN;
    }
}

void MusicBrowserPage::onButtonEnter(void* context) {
    pendingAction = BA_ENTER;
}

void MusicBrowserPage::onButton2(void* context) {
    pendingAction = BA_UP;
}

void MusicBrowserPage::onButton4(void* context) {
    pendingAction = BA_PAGEUP;
}

void MusicBrowserPage::onButton5(void* context) {
    pendingAction = BA_ENTER;
}

void MusicBrowserPage::onButton6(void* context) {
    pendingAction = BA_PAGEDOWN;
}

void MusicBrowserPage::onButton8(void* context) {
    pendingAction = BA_DOWN;
}

// ================= 帧更新(渲染任务内, render之后调用) =================
void MusicBrowserPage::update() {
    // 延迟初始化: showPage只置位, 等"读取TF卡中..."提示帧已刷到屏幕后才开始扫描
    // (UIEngine::update()先跑动画回调再跑当前页update, 导航帧内会立刻调到本页update;
    //  若不设时间门槛, 扫描会卡死在菜单页的最后一帧, loading提示根本显示不出来)
    if (initPending) {
        if (millis() - initPendingAt < INIT_MIN_LOADING_MS) {
            return;   // loading帧展示期间保持空闲, 让渲染任务先出图
        }
        initPending = false;
        initHardware();
        return;
    }

    int action = pendingAction;
    if (action != BA_NONE) {
        pendingAction = BA_NONE;
        // 命令消费日志: 按键投递(BA_x)后若看不到此行 = 渲染任务卡死
        static const char* baNames[] = {"", "上移", "下移", "上翻页", "下翻页", "进入", "返回", "重试"};
        if (action >= 1 && action <= 7) {
            LOG_D("MBP[命令] 消费%s(state=%d cur=%d/%d)",
                  baNames[action], (int)browserState, cursor, (int)entries.size());
        }
        switch (action) {
            case BA_UP:       if (browserState == BROWSER_BROWSE) moveCursor(-1); break;
            case BA_DOWN:     if (browserState == BROWSER_BROWSE) moveCursor(1); break;
            case BA_PAGEUP:   if (browserState == BROWSER_BROWSE) pageUp(); break;
            case BA_PAGEDOWN: if (browserState == BROWSER_BROWSE) pageDown(); break;
            case BA_ENTER:    if (browserState == BROWSER_BROWSE) enterItem(); break;
            case BA_BACK:     handleBack(); break;
            case BA_RETRY:    retryInit(); break;
            default: break;
        }
    }

    if (browserState == BROWSER_BROWSE) {
        updateSelScroll();
    }
}

void MusicBrowserPage::updateSelScroll() {
    if (entries.empty() || cursor < 0 || cursor >= (int)entries.size()) return;
    if (selNameWidth <= NAME_VIEW_W) return;   // 名字不超宽无需滚动
    if (entries[cursor].name != selFullName) return;  // render尚未同步新选中项

    int maxOffset = selNameWidth - NAME_VIEW_W;
    unsigned long now = millis();
    switch (selScrollPhase) {
        case 0:  // 起始停留1秒
            if (now - selScrollTimer > 1000) {
                selScrollPhase = 1;
                selScrollTimer = now;
            }
            break;
        case 1:  // 向左滚动
            if (now - selScrollTimer > 120) {
                selScrollTimer = now;
                selScrollOffset += 6;
                if (selScrollOffset >= maxOffset) {
                    selScrollOffset = maxOffset;
                    selScrollPhase = 2;
                    selScrollTimer = now;
                }
            }
            break;
        case 2:  // 尾部停留后回到开头
            if (now - selScrollTimer > 1500) {
                selScrollOffset = 0;
                selScrollPhase = 0;
                selScrollTimer = now;
            }
            break;
    }
}

// ================= 绘制 =================
void MusicBrowserPage::render(U8G2* u8g2) {
    switch (browserState) {
        case BROWSER_INIT:
            drawErrorUI(u8g2, "读取TF卡中...", "请稍候");
            break;
        case BROWSER_NO_CARD:
            drawErrorUI(u8g2, "未插入TF卡", "插卡后按[菜单]键重试");
            break;
        case BROWSER_NO_CODEC:
            drawErrorUI(u8g2, "音频芯片无响应", "请检查硬件后重试");
            break;
        case BROWSER_BROWSE:
            drawBrowseUI(u8g2);
            break;
    }
}

void MusicBrowserPage::drawErrorUI(U8G2* u8g2, const char* title, const char* hint) {
    // TF卡图标居中(Siji 57816)
    u8g2->setFont(u8g2_font_siji_t_6x10);
    int iconW = 8;   // siji 6x10单元格, TF卡实际占宽约8
    u8g2->drawGlyph((SCREEN_WIDTH - iconW) / 2, 22, GLYPH_SDCARD);

    // 主提示
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);
    uint16_t tw = u8g2->getUTF8Width(title);
    u8g2->drawUTF8((SCREEN_WIDTH - tw) / 2, 39, title);

    // 次提示
    uint16_t hw = u8g2->getUTF8Width(hint);
    u8g2->drawUTF8((SCREEN_WIDTH - hw) / 2, 55, hint);
}

// UTF-8安全截断: 超宽时按整字符移除并追加".."
static String truncateUTF8(const String& s, int maxW, U8G2* u8g2) {
    if (u8g2->getUTF8Width(s.c_str()) <= maxW) return s;
    String out = s;
    while (out.length() > 1) {
        // 移除最后一个UTF-8字符(回退到首字节)
        int cut = out.length() - 1;
        while (cut > 0 && (out.charAt(cut) & 0xC0) == 0x80) cut--;
        out = out.substring(0, cut);
        if (u8g2->getUTF8Width((out + "..").c_str()) <= maxW) break;
    }
    return out + "..";
}

void MusicBrowserPage::drawBrowseUI(U8G2* u8g2) {
    // ---- 标题栏: 图标(Siji) + 当前目录 + 项目数; 根目录=TF卡图标57816, 子目录=文件夹57814 ----
    u8g2->setFont(u8g2_font_siji_t_6x10);
    u8g2->drawGlyph(3, 11, (currentPath == "/") ? GLYPH_SDCARD : GLYPH_FOLDER);
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);

    String title;
    if (currentPath == "/") {
        title = "TF卡";
    } else {
        int lastSlash = currentPath.lastIndexOf('/');
        title = currentPath.substring(lastSlash + 1);
    }

    // 右侧项目数, 标题可用宽度随之收窄
    String countText = String(entries.size()) + "项";
    uint16_t countW = u8g2->getUTF8Width(countText.c_str());
    u8g2->drawUTF8(SCREEN_WIDTH - 3 - countW, 11, countText.c_str());

    // 标题截断
    int maxTitleW = SCREEN_WIDTH - 20 - countW - 6;
    title = truncateUTF8(title, maxTitleW, u8g2);
    u8g2->drawUTF8(17, 11, title.c_str());

    // 分隔线
    u8g2->drawHLine(0, 13, SCREEN_WIDTH);

    // ---- 列表区: 4行 x 12px (y14-y62, 底部不留空行) ----
    if (entries.empty()) {
        u8g2->setFont(u8g2_font_wqy12_t_gb2312);
        const char* empty = "(空目录)";
        uint16_t ew = u8g2->getUTF8Width(empty);
        u8g2->drawUTF8((SCREEN_WIDTH - ew) / 2, 36, empty);
        return;
    }

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = viewStart + i;
        if (idx >= (int)entries.size()) break;
        int y = 14 + i * 12;
        bool selected = (idx == cursor);

        if (selected) {
            // 选中行反白光标条: 有滚动条时右侧收窄, 距滚动条至少1像素
            u8g2->setDrawColor(1);
            int selW = SCREEN_WIDTH;
            if ((int)entries.size() > VISIBLE_ROWS) selW = SCREEN_WIDTH - 6;  // 0..121, x122留空
            u8g2->drawBox(0, y, selW, 12);
        }
        // 反色用 drawColor 0/1 + 透明字体模式; XOR在实心模式会整格变黑块(图标变方块)
        u8g2->setFontMode(1);
        u8g2->setDrawColor(selected ? 0 : 1);

        // 图标(Siji): 文件夹57814 / 音符57970
        u8g2->setFont(u8g2_font_siji_t_6x10);
        u8g2->drawGlyph(4, y + 9, entries[idx].isDir ? GLYPH_FOLDER : GLYPH_NOTE);

        // 文件名
        u8g2->setFont(u8g2_font_wqy12_t_gb2312);
        const String& name = entries[idx].name;
        if (selected) {
            // 选中项: 同步滚动上下文(名字变化时重置)
            if (selFullName != name) {
                selFullName = name;
                selNameWidth = u8g2->getUTF8Width(name.c_str());
                selScrollOffset = 0;
                selScrollPhase = 0;
                selScrollTimer = millis();
            }
            if (selNameWidth <= NAME_VIEW_W) {
                // 不超宽: 直接完整显示
                u8g2->drawUTF8(18, y + 10, name.c_str());
            } else {
                // 超宽: 裁剪窗口内滚动显示完整名
                u8g2->setClipWindow(18, y, 18 + NAME_VIEW_W, y + 12);
                u8g2->drawUTF8(18 - selScrollOffset, y + 10, name.c_str());
                u8g2->setMaxClipWindow();
            }
        } else {
            // 非选中行: 静态截断
            String cut = truncateUTF8(name, SCREEN_WIDTH - 20 - 8, u8g2);
            u8g2->drawUTF8(18, y + 10, cut.c_str());
        }
        u8g2->setFontMode(0);

        if (selected) {
            u8g2->setDrawColor(1);
        }
    }

    // ---- 滚动条 ----
    if (entries.size() > VISIBLE_ROWS) {
        int trackY = 14, trackH = VISIBLE_ROWS * 12;
        u8g2->drawVLine(SCREEN_WIDTH - 3, trackY, trackH);
        // 滑块
        int thumbH = trackH * VISIBLE_ROWS / entries.size();
        if (thumbH < 6) thumbH = 6;
        int maxStart = entries.size() - VISIBLE_ROWS;
        int thumbY = trackY + (trackH - thumbH) * viewStart / maxStart;
        u8g2->drawBox(SCREEN_WIDTH - 5, thumbY, 5, thumbH);
    }
}
