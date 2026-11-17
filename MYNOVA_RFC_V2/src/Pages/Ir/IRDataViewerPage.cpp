/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// IRDataViewerPage.cpp - 红外信号数据详情查看器（IRrecvDumpV3风格）
#include "IRDataViewerPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Services/DataStore.h"
#include "../../Util/MemAlloc.h"
#include <IRremoteESP8266.h>   // decode_type_t
#include <IRutils.h>           // typeToString

extern UIEngine uiEngine;
extern DataStore dataStore;

IRDataViewerPage::IRDataViewerPage(const IRSignalMeta* meta, const uint16_t* edges) :
    UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    textArea(nullptr),
    navBar(nullptr),
    metaRef(meta),
    ownedEdges(nullptr),
    edgesRef(edges) {
    initLayout();
}

IRDataViewerPage::IRDataViewerPage(int slotIndex, SignalKind kind) :
    UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    textArea(nullptr),
    navBar(nullptr),
    metaRef(nullptr),
    ownedEdges(nullptr),
    edgesRef(nullptr) {
    // 按指定域读取槽位元数据与边沿数据（边沿按需读入堆缓冲，析构释放）
    SignalEntry entry = dataStore.ReadSignal(kind, slotIndex);
    // 兼容旧调用：若指定域无数据，回退到旧统一索引读取
    if (entry.name.length() == 0) entry = dataStore.ReadSignal(slotIndex);
    if (entry.name.length() > 0 && entry.kind == SIG_IR) {
        metaCopy = entry.ir;
        if (entry.ir.totalEdges > 0) {
            ownedEdges = (uint16_t*)anyMalloc(entry.ir.totalEdges * sizeof(uint16_t));
            if (ownedEdges != nullptr) {
                if (!dataStore.ReadIREdges(slotIndex, ownedEdges, entry.ir.totalEdges)) {
                    free(ownedEdges);
                    ownedEdges = nullptr;
                }
            }
            edgesRef = ownedEdges;
        } else {
            edgesRef = nullptr;
        }
        metaRef = &metaCopy;
    }
    // 无有效数据时metaRef保持nullptr，buildDumpText输出空信号提示
    initLayout();
}

IRDataViewerPage::~IRDataViewerPage() {
    if (ownedEdges != nullptr) {
        free(ownedEdges);
        ownedEdges = nullptr;
    }
}

void IRDataViewerPage::initLayout() {
    // 可滚动dump区（4x6等宽字体：更高信息密度，更适合逐帧时序）
    textArea = new UITextArea();
    textArea->x = 0;
    textArea->y = 0;
    textArea->width = SCREEN_WIDTH;
    textArea->height = SCREEN_HEIGHT - 10 - 3;
    textArea->textFont = u8g2_font_4x6_tf;
    textArea->lineHeight = 6;
    textArea->bVisible = true;
    addWidget(textArea);

    // 底部导航（留出2px空隙，避免与内容框重叠）
    navBar = new UINavBar(0, SCREEN_HEIGHT - 10, SCREEN_WIDTH, 10);
    navBar->setMargin(0, 0);
    navBar->bVisible = true;
    navBar->setLeftButtonText("返回");
    navBar->setMiddleButtonText("下行");
    navBar->setRightButtonText("上页");
    addWidget(navBar);

    buildDumpText();
}

// ──────────────────────────────────────────────────────────────────────────────
// 生成单帧的dump文本（协议/位宽/码值/地址/命令/间隔/时长 + RAW时序折行）
// ──────────────────────────────────────────────────────────────────────────────
void IRDataViewerPage::appendFrameDump(String& out, uint8_t fi, uint32_t edgeBase) {
    const IRSignalMeta& m = *metaRef;
    uint16_t len = m.frameLens[fi];
    char line[48];

    // 帧头: F1/2 NEC 32bit
    char proto[24];
    // frameProto为库的decode_type_t枚举值；用typeToString反查协议名
    // （UNKNOWN=-1会得到"UNKNOWN"，与学习页口径一致）
    String protoName = typeToString((decode_type_t)m.frameProto[fi]);
    strncpy(proto, protoName.c_str(), sizeof(proto) - 1);
    proto[sizeof(proto) - 1] = '\0';
    if (m.frameProto[fi] == 0 && m.frameBits[fi] == 0) {
        // v1旧数据无解码详情
        snprintf(proto, sizeof(proto), "RAW");
    }

    snprintf(line, sizeof(line), "F%u/%u %s %ubit", fi + 1, m.frameCount, proto, m.frameBits[fi]);
    out += line;
    out += '\n';

    // 码值行: VAL:1FE807F A:1F8 C:807F（UNKNOWN指纹已标注"~"）
    if (m.frameProto[fi] != 0 || m.frameBits[fi] != 0) {
        bool isUnknown = (m.frameProto[fi] == (uint8_t)decode_type_t::UNKNOWN);
        char valStr[20];
        if (isUnknown) {
            snprintf(valStr, sizeof(valStr), "~%08llX",
                     (unsigned long long)m.frameValue[fi] & 0xFFFFFFFFULL);
        } else {
            snprintf(valStr, sizeof(valStr), "%08llX",
                     (unsigned long long)m.frameValue[fi]);
        }
        if (m.frameAddr[fi] != 0 || m.frameCmd[fi] != 0) {
            snprintf(line, sizeof(line), "VAL:%s A:%X C:%X%s",
                     valStr, m.frameAddr[fi], m.frameCmd[fi],
                     isUnknown ? "(FP)" : "");
        } else {
            snprintf(line, sizeof(line), "VAL:%s%s", valStr, isUnknown ? "(FP)" : "");
        }
        out += line;
        out += '\n';
    }

    // 间隔/时长/边沿行: GAP:42ms DUR:107ms EDG:68
    uint32_t durUs = 0;
    if (edgesRef != nullptr) {
        for (uint16_t j = 0; j < len && edgeBase + j < m.totalEdges; j++) {
            durUs += edgesRef[edgeBase + j];
        }
    }
    snprintf(line, sizeof(line), "GAP:%ums DUR:%lums EDG:%u",
             m.frameGapsMs[fi], (unsigned long)(durUs / 1000), len);
    out += line;
    out += '\n';

    // RAW时序折行（us单位，逗号分隔，参考IRrecvDumpV3输出样式）
    if (edgesRef != nullptr && len > 0) {
        String raw = "RAW:";
        for (uint16_t j = 0; j < len && edgeBase + j < m.totalEdges; j++) {
            char e[8];
            snprintf(e, sizeof(e), "%u", edgesRef[edgeBase + j]);
            if (j > 0) raw += ",";
            raw += e;
            // 单行超宽时折行（5x7等宽下每行约25字符）
            if (raw.length() >= 25) {
                out += raw;
                out += '\n';
                raw = "";
            }
        }
        if (raw.length() > 0) {
            out += raw;
            out += '\n';
        }
    }
    out += '\n';   // 帧间空行分隔
}

// ──────────────────────────────────────────────────────────────────────────────
// 生成完整dump文本（全部帧 + 空调状态 + 协议不识别时的摘要）
// ──────────────────────────────────────────────────────────────────────────────
void IRDataViewerPage::buildDumpText() {
    String out;

    if (metaRef == nullptr || metaRef->frameCount == 0) {
        out = "NO IR DATA\n";
        textArea->setText(out);
        return;
    }

    const IRSignalMeta& m = *metaRef;

    // 空调协议人类可读状态（如有，置顶展示）
    if (m.acStateDesc[0] != '\0') {
        out += "AC:";
        out += m.acStateDesc;
        out += '\n';
    }

    // 逐帧dump
    uint32_t edgeBase = 0;
    for (uint8_t i = 0; i < m.frameCount; i++) {
        appendFrameDump(out, i, edgeBase);
        edgeBase += m.frameLens[i];
    }

    textArea->setText(out);
}

void IRDataViewerPage::scrollUp() {
    textArea->scrollUp();
}

void IRDataViewerPage::scrollDown() {
    textArea->scrollDown();
}

void IRDataViewerPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
}

void IRDataViewerPage::onButtonBack(void* context) {
    navBar->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void IRDataViewerPage::onButtonEnter(void* context) {
    scrollUp();
}

void IRDataViewerPage::onButtonMenu(void* context) {
    scrollDown();
}

void IRDataViewerPage::onButton1(void* context) {
    scrollUp();
}

void IRDataViewerPage::onButton2(void* context) {
    scrollUp();
}

void IRDataViewerPage::onButton4(void* context) {
    // 一次滚动一页（尽量对齐当前可视行数）
    for (int i = 0; i < textArea->visibleLines; i++) scrollUp();
}

void IRDataViewerPage::onButton6(void* context) {
    for (int i = 0; i < textArea->visibleLines; i++) scrollDown();
}

void IRDataViewerPage::onButton8(void* context) {
    scrollDown();
}