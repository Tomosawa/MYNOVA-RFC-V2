/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// IRLearnPage.cpp - 红外学习页面（动态网格布局）
// 库字段事实（IRremoteESP8266 2.8.6 decode_results）：
//   - value/address/command 与 state[] 是union：普通协议有value（部分含addr/cmd），
//     空调协议只有state[]（value为其前8字节LE视图）；界面按字段存在性动态填充。
#include "IRLearnPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"
#include "../../Drivers/AudioPlayer.h"
#include "../Data/SaveDataPage.h"
#include "IRDataViewerPage.h"
#include "../../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "IRPAGE"

extern UIEngine uiEngine;
extern IRHelper irHelper;
extern GUIRender guiRender;

// 两列网格列位置：标签列 x0，值列 x68（中文字体12px/列，ASCII 6px/字）
#define IR_GRID_COL2_X   68
#define IR_GRID_COL1_W   66
#define IR_GRID_COL2_W   60

IRLearnPage::IRLearnPage(int suggestedSlot) : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    centerLabel(nullptr),
    statusContentLabel(nullptr),
    navBar(nullptr),
    currentState(IR_STATE_LEARNING),
    lastCheckTime(0),
    viewFrameIndex(0),
    lastShownFrames(0),
    lastDotStep(0),
    targetSlot(suggestedSlot) {
    for (int i = 0; i < 4; i++) {
        rows[i][0] = nullptr;
        rows[i][1] = nullptr;
    }
    initLayout();
}

IRLearnPage::~IRLearnPage() {
    stopLearning();
}

void IRLearnPage::initLayout() {
    // 学习中提示：内容区居中（学习完成后隐藏，让位给结果网格）
    centerLabel = new UILabel();
    centerLabel->x = 0;
    centerLabel->y = 14;
    centerLabel->width = SCREEN_WIDTH;
    centerLabel->height = 24;
    centerLabel->label = "等待接收信号";
    centerLabel->textFont = u8g2_font_wqy12_t_gb2312;
    centerLabel->textAlign = CENTER;
    centerLabel->verticalAlign = MIDDLE;
    centerLabel->bVisible = true;
    addWidget(centerLabel);

    // 4行×左右两列结果网格（初始隐藏，学习完成后填充显示）
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            UILabel* lb = new UILabel();
            lb->x = (j == 0) ? 0 : IR_GRID_COL2_X;
            lb->y = i * 12;
            lb->width = (j == 0) ? IR_GRID_COL1_W : IR_GRID_COL2_W;
            lb->height = 12;
            lb->label = "";
            lb->textFont = u8g2_font_wqy12_t_gb2312;
            lb->textAlign = LEFT;
            lb->verticalAlign = MIDDLE;
            lb->bVisible = false;
            addWidget(lb);
            rows[i][j] = lb;
        }
    }

    // 状态行内容标签：紧跟"状态:"前缀（前缀占30px），4x6小字显示英文状态
    statusContentLabel = new UILabel();
    statusContentLabel->x = 30;
    statusContentLabel->y = 36;
    statusContentLabel->width = SCREEN_WIDTH - 30;
    statusContentLabel->height = 12;
    statusContentLabel->label = "";
    statusContentLabel->textFont = u8g2_font_4x6_tf;
    statusContentLabel->textAlign = LEFT;
    statusContentLabel->verticalAlign = MIDDLE;
    statusContentLabel->bVisible = false;
    addWidget(statusContentLabel);

    navBar = new UINavBar(0, 52, SCREEN_WIDTH, 12);
    navBar->setMargin(0, 0);
    navBar->bVisible = true;
    navBar->setLeftButtonText("返回");
    navBar->setMiddleButtonText("");
    navBar->setRightButtonText("");
    addWidget(navBar);

    startLearning();
}

// ──────────────────────────────────────────────────────────────────────────────
// 网格行填充助手
// ──────────────────────────────────────────────────────────────────────────────
void IRLearnPage::rowSplit(int row, const String& left, const String& right) {
    // 第4行改用两列内容时，隐藏紧贴式状态内容标签（互斥显示）
    if (row == 3) statusContentLabel->bVisible = false;
    rows[row][0]->label = left;
    rows[row][0]->bVisible = true;
    rows[row][1]->label = right;
    rows[row][1]->textFont = u8g2_font_wqy12_t_gb2312;
    rows[row][1]->bVisible = true;
}

void IRLearnPage::rowFull(int row, const String& text) {
    if (row == 3) statusContentLabel->bVisible = false;
    rows[row][0]->label = text;
    rows[row][0]->bVisible = true;
    rows[row][1]->bVisible = false;
}

void IRLearnPage::rowPrefixSmall(const String& prefix, const String& content) {
    // 状态行: 前缀"状态:"用第4行左列（中文字体），
    // 内容用独立标签紧跟其后（4x6小字，英文可容纳更多内容）
    rows[3][0]->label = prefix;
    rows[3][0]->bVisible = true;
    rows[3][1]->bVisible = false;
    statusContentLabel->label = content;
    statusContentLabel->bVisible = true;
}

void IRLearnPage::hideGrid() {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            rows[i][j]->bVisible = false;
        }
    }
    statusContentLabel->bVisible = false;
}

// ──────────────────────────────────────────────────────────────────────────────
// 码值HEX：普通协议为库解码值；空调协议value是状态前8字节的LE视图，
// 还原为state字节自然序显示（与IRrecvDump等工具口径一致）
// ──────────────────────────────────────────────────────────────────────────────
String IRLearnPage::frameValueHex(uint8_t fi) {
    const IRSignalMeta& m = irHelper.getMeta();
    uint64_t v = m.frameValue[fi];
    char buf[17];

    if (m.acStateDesc[0] != '\0') {
        for (int i = 0; i < 8; i++) {
            snprintf(&buf[i * 2], 3, "%02X", (unsigned)((v >> (8 * i)) & 0xFF));
        }
        buf[16] = '\0';
    } else if (m.frameProto[fi] == IR_PROTO_BYTE_UNKNOWN) {
        // UNKNOWN的value是raw时序哈希（32位）
        snprintf(buf, sizeof(buf), "%08llX",
                 (unsigned long long)(v & 0xFFFFFFFFULL));
    } else {
        snprintf(buf, sizeof(buf), "%llX", (unsigned long long)v);
    }
    return String(buf);
}

// ──────────────────────────────────────────────────────────────────────────────
// 按当前帧的字段动态填充4行网格
// ──────────────────────────────────────────────────────────────────────────────
void IRLearnPage::refreshLiveRows() {
    const IRSignalMeta& m = irHelper.getMeta();
    if (m.frameCount == 0) return;
    if (viewFrameIndex >= m.frameCount) viewFrameIndex = m.frameCount - 1;
    uint8_t fi = viewFrameIndex;
    char buf[40];

    // 第1行: 协议 | 帧序号
    String proto;
    if (m.frameProto[fi] == IR_PROTO_BYTE_UNKNOWN) {
        proto = "未识别";
    } else {
        proto = typeToString((decode_type_t)m.frameProto[fi]);
        if (proto.length() == 0) proto = m.protocolName[0] ? m.protocolName : "RAW";
    }
    snprintf(buf, sizeof(buf), "帧:%u/%u", fi + 1, m.frameCount);
    rowSplit(0, "协议:" + proto, String(buf));

    // 第2行: 码值（必有；空调为状态前8字节，长值独占整行）
    rowFull(1, "值:" + frameValueHex(fi));

    // 第3行: 位宽 | 边沿数
    snprintf(buf, sizeof(buf), "位宽:%ubit", m.frameBits[fi]);
    String row3L(buf);
    snprintf(buf, sizeof(buf), "边沿:%u", m.frameLens[fi]);
    rowSplit(2, row3L, String(buf));

    // 第4行: 按协议字段动态选择
    uint32_t edgeBase = 0;
    for (uint8_t i = 0; i < fi; i++) edgeBase += m.frameLens[i];
    const uint16_t* e = irHelper.getEdges();

    if (m.acStateDesc[0] != '\0') {
        // 空调协议: 可读状态（英文，4x6小字容纳更多内容）
        String desc = m.acStateDesc;
        if (desc.length() > 24) desc = desc.substring(0, 24) + "..";
        rowPrefixSmall("状态:", desc);
    } else if (m.frameProto[fi] != IR_PROTO_BYTE_UNKNOWN &&
               (m.frameAddr[fi] != 0 || m.frameCmd[fi] != 0)) {
        // 库提供地址/命令时展示（NEC/Sony/Samsung等）
        snprintf(buf, sizeof(buf), "地址:%llX",
                 (unsigned long long)m.frameAddr[fi]);
        String row4L(buf);
        snprintf(buf, sizeof(buf), "命令:%llX",
                 (unsigned long long)m.frameCmd[fi]);
        rowSplit(3, row4L, String(buf));
    } else if (fi > 0) {
        // 多帧普通协议: 帧间隔 + 帧时长
        uint32_t durUs = 0;
        if (e != nullptr) {
            for (uint16_t j = 0; j < m.frameLens[fi]; j++) {
                durUs += e[edgeBase + j];
            }
        }
        snprintf(buf, sizeof(buf), "间隔:%ums", m.frameGapsMs[fi]);
        String row4L(buf);
        snprintf(buf, sizeof(buf), "时长:%lums",
                 (unsigned long)(durUs / 1000));
        rowSplit(3, row4L, String(buf));
    } else {
        // 单帧普通协议/未识别: 首组脉宽（mark/space，辅助判断信号类型）
        if (e != nullptr && m.frameLens[0] >= 2) {
            snprintf(buf, sizeof(buf), "首脉宽:%lu/%luus",
                     (unsigned long)e[0], (unsigned long)e[1]);
        } else {
            snprintf(buf, sizeof(buf), "首脉宽:--");
        }
        rowFull(3, String(buf));
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// 学习控制
// ──────────────────────────────────────────────────────────────────────────────
void IRLearnPage::startLearning() {
    currentState = IR_STATE_LEARNING;
    viewFrameIndex = 0;
    lastShownFrames = 0;
    lastDotStep = 0;

    centerLabel->label = "等待接收信号";
    centerLabel->bVisible = true;
    hideGrid();
    navBar->setMiddleButtonText("");
    navBar->setRightButtonText("");

    // 红外学习期间暂停音乐播放：
    // SD卡读取会短暂禁用flash cache，而IRremoteESP8266在当前sdkconfig下
    // 的timer重武装函数非IRAM，边沿中断与SD读并发可能触发cache访问异常。
    // （学习只需几秒，暂停音乐是最简单可靠的互斥保护；退出页面后手动恢复播放）
    if (audioPlayer.isPlaying() || audioPlayer.isPaused()) {
        audioPlayer.stop();
        LOG_I("IRLearnPage: 已暂停音乐播放（学习期间保护）");
    }

    irHelper.EnableReceive();
    lastCheckTime = millis();
}

void IRLearnPage::stopLearning() {
    if (currentState == IR_STATE_LEARNING) {
        irHelper.DisableReceive();
    }
}

void IRLearnPage::cycleFrame(int delta) {
    const IRSignalMeta& m = irHelper.getMeta();
    if (currentState != IR_STATE_LEARNED || m.frameCount == 0) return;

    int next = (int)viewFrameIndex + delta;
    if (next < 0) next = m.frameCount - 1;
    if (next >= m.frameCount) next = 0;
    viewFrameIndex = (uint8_t)next;
    refreshLiveRows();
}

// ──────────────────────────────────────────────────────────────────────────────
// 状态轮询（100ms渲染节拍）
// 学习中: 内容区居中提示（省略号动画+帧数），收到帧后不显示结果网格；
// 完成后: 提示消失，切换为按字段动态填充的结果网格。
// ──────────────────────────────────────────────────────────────────────────────
void IRLearnPage::updateLearnStatus() {
    if (currentState != IR_STATE_LEARNING) {
        return;
    }

    uint8_t frames = irHelper.GetReceivedFrameCount();
    uint8_t dotStep = (millis() / 500) % 4;

    if (frames != lastShownFrames || dotStep != lastDotStep) {
        lastShownFrames = frames;
        lastDotStep = dotStep;
        String dots;
        for (uint8_t i = 0; i < dotStep; i++) dots += ".";
        if (frames == 0) {
            centerLabel->label = "等待接收信号" + dots;
        } else {
            centerLabel->label = "正在学习 " + String(frames) + "帧" + dots;
        }
    }

    if (millis() - lastCheckTime > 100) {
        lastCheckTime = millis();
        if (irHelper.CheckLearnFinished()) {
            const IRSignalMeta& meta = irHelper.getMeta();
            currentState = IR_STATE_LEARNED;
            centerLabel->bVisible = false;
            navBar->setMiddleButtonText("详情");
            navBar->setRightButtonText("保存");
            viewFrameIndex = 0;
            refreshLiveRows();
            LOG_I("IRLearnPage: 显示学习结果 %s %u帧",
                  meta.protocolName, meta.frameCount);
        }
    }
}

void IRLearnPage::showPage() {
}

void IRLearnPage::render(U8G2* u8g2) {
    // 先更新学习状态
    updateLearnStatus();
    // 然后调用父类的 render 方法来渲染所有组件
    UIPage::render(u8g2);
}

void IRLearnPage::onButtonBack(void* context) {
    navBar->showLeftBlink(1, 80, 80, [this]() {
        stopLearning();
        uiEngine.navigateBack();
    });
}

void IRLearnPage::onButtonEnter(void* context) {
    if (currentState == IR_STATE_LEARNED) {
        navBar->showRightBlink(1, 80, 80, [this]() {
            SignalDraft draft;
            draft.kind = SIG_IR;
            draft.irMeta = &irHelper.getMeta();
            draft.irEdges = irHelper.getEdges();
            SaveDataPage* saveDataPage = new SaveDataPage(draft, targetSlot);
            uiEngine.navigateTo(saveDataPage);
        });
    }
}

void IRLearnPage::onButtonMenu(void* context) {
    // 学习完成后中键进入数据详情查看器（学习期间不响应，
    // 避免查看器读取边沿缓冲时学习任务仍在写入）
    if (currentState == IR_STATE_LEARNED) {
        navBar->showMiddleBlink(1, 80, 80, [this]() {
            uiEngine.navigateTo(new IRDataViewerPage(&irHelper.getMeta(),
                                                     irHelper.getEdges()));
        });
    }
}

void IRLearnPage::onButton1(void* context) {
    if (currentState == IR_STATE_LEARNED) {
        cycleFrame(-1);   // 上一帧
    }
}

void IRLearnPage::onButton8(void* context) {
    if (currentState == IR_STATE_LEARNED) {
        cycleFrame(1);    // 下一帧
    }
}
