/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "IRHelper.h"
#include "../Bsp/IOPin.h"
#include "../Services/DataStore.h"
#include "AudioPlayer.h"
#include "../Util/TaskConfig.h"
#include "../Util/MemAlloc.h"
#include "../Util/Log.h"

#define LOG_TAG "IR"

// 学习边沿缓冲容量（与存储格式上限一致：8帧×512边沿）
#define IR_EDGE_BUFFER_COUNT  (IR_MAX_FRAMES * IR_MAX_FRAME_EDGES)

// 单帧捕获的最小边沿数（小于此值视为噪声丢弃）
#define IR_MIN_FRAME_EDGES    6

extern DataStore dataStore;

IRHelper::IRHelper() :
    // 第4参save_buffer=true: decode()内部先把捕获拷贝到保存缓冲并立即resume()
    // 恢复捕获——两帧间隔仅略大于超时值时也不会丢失下一帧的开头边沿
    irrecv(PIN_IR_RECV, IR_CAPTURE_BUF_SIZE, IR_CAPTURE_TIMEOUT_MS, true),
    irsend(PIN_IR_SEND),
    learnCmd(0),
    bCaptureActive(false),
    learnTaskHandle(nullptr),
    bReady(false),
    rmtReady(false),
    lastFrameTime(0),
    edges(nullptr)
{
}

IRHelper::~IRHelper()
{
    if (edges != nullptr) {
        free(edges);
        edges = nullptr;
    }
}

void IRHelper::init()
{
    LOG_I("IRHelper::init (IRremoteESP8266 + RMT)");

    // 分配学习结果边沿缓冲（一次性分配，常驻）
    // 8KB大块落PSRAM(学习任务写/页面读, 非ISR路径, 规划§5.2); 失败降级内部RAM
    if (edges == nullptr) {
        edges = (uint16_t*)anyMalloc(IR_EDGE_BUFFER_COUNT * sizeof(uint16_t));
        if (edges == nullptr) {
            LOG_E("IRHelper: 边沿缓冲分配失败!");
            return;
        }
    }

    // 初始化RMT硬件发射通道（仅IR_TX_USE_RMT=1时启用）：
    //   1MHz tick = 1us时序精度；38kHz载波由RMT硬件调制（周期无抖动，
    //   不受WiFi/中断影响，且发射期间零CPU占用）。
    //   S3有4个TX通道，与CC1101的RMT发射(GDO0)各占一个，无冲突。
#if IR_TX_USE_RMT
    if (rmtInit(PIN_IR_SEND, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 1000000)) {
        // 载波调制在高电平（mark）上：LED驱动为高有效
        // 注意: core的carrier_level参数直通IDF的polarity_active_low
        // （core头文件注释"true=positive"与实现相反！），必须传false
        // 才能把38kHz载波调制到mark(level 1)上；传true会调制到space上，
        // 信号整体反相，接收头无法解调（LED看似亮但设备不识别）。
        if (rmtSetCarrier(PIN_IR_SEND, true, false, 38000, 0.5f)) {
            rmtReady = true;
            LOG_I("IRHelper: IR发射使用RMT硬件载波 (38kHz)");
        } else {
            LOG_E("IRHelper: RMT载波配置失败!");
        }
    } else {
        LOG_E("IRHelper: RMT通道初始化失败!");
    }
#endif

    // 初始化软件发射器（IR_TX_USE_RMT=0时的主发射路径; =1时作为降级备胎）
    // 注意: irsend.begin()内部的pinMode()会经periman解除rmtInit()注册的
    // RMT引脚总线（core 3.x引脚总线互斥），RMT通道被拆除→红外管完全不亮。
    // 因此RMT模式下严禁调用begin()，仅在降级路径（RMT初始化失败）时初始化。
#if IR_TX_USE_RMT
    if (!rmtReady) {
        irsend.begin();
        LOG_W("IRHelper: 警告: IR发射降级为软件调制(精度受中断抖动影响)");
    }
#else
    irsend.begin();
#endif

    // 噪声门限：短于12个边沿的未识别捕获视为噪声丢弃（参考IRrecvDumpV3）
    irrecv.setUnknownThreshold(12);

    // 创建红外学习任务：3ms快速轮询decode()，独立于UI的100ms渲染节拍。
    // 必要性：捕获在超时后停止（kStopState），期间到达的边沿会被丢弃；
    // 若靠页面100ms轮询解码，间隔<100ms的连续帧（如双码遥控、空调多帧）
    // 的后续帧开头必然丢失。专用任务将解码延迟压缩到3ms以内。
    // 钉Core1(业务核): 与IR捕获ISR同核, 优先级3=硬时序业务(规划§4.3)
    xTaskCreatePinnedToCore(
        irLearnTaskFunc,
        TASK_IR_NAME,
        TASK_IR_STACK,
        this,
        TASK_PRIO_HW,               // 与射频接收任务同级
        &learnTaskHandle,
        TASK_CORE_APP
    );

    bReady = true;
    LOG_I("IRHelper: 红外收发就绪");
}

// ──────────────────────────────────────────────────────────────────────────────
// 学习控制（仅置命令标志；捕获启停由学习任务执行，保证IRrecv单线程访问）
// ──────────────────────────────────────────────────────────────────────────────
void IRHelper::EnableReceive()
{
    LOG_I("IRHelper: 开始红外学习");
    learnCmd = 1;   // 学习任务检测到启动沿后将清空学习结果并启动捕获
}

void IRHelper::DisableReceive()
{
    LOG_I("IRHelper: 停止红外学习");
    learnCmd = 0;   // 学习任务将自动停止捕获（学习结果保留供保存流程读取）
}

// ──────────────────────────────────────────────────────────────────────────────
// 红外学习任务：快速轮询decode()
// 捕获在静默超时后停止（kStopState），停止期间到达的边沿会被硬件丢弃，
// 因此必须在下一帧开始前完成解码并resume()——decode()在save_buffer模式
// 内部即时resume()。本任务3ms轮询将丢帧窗口压到最小；页面100ms轮询做不到。
// ──────────────────────────────────────────────────────────────────────────────
void IRHelper::irLearnTaskFunc(void* pvParameters)
{
    IRHelper* self = static_cast<IRHelper*>(pvParameters);

    while (true) {
        if (self->learnCmd == 1) {
            if (!self->bCaptureActive) {
                // 启动沿：清空上次学习结果并启动捕获
                memset((void*)&self->meta, 0, sizeof(self->meta));
                self->lastFrameTime = 0;
                self->irrecv.enableIRIn();
                self->bCaptureActive = true;
                LOG_I("IRLearnTask: 捕获已启动");
            }
            // decode(): 捕获完成时返回true（内部已自动resume恢复捕获）
            if (self->irrecv.decode(&self->results)) {
                self->processCapturedFrame();
            }
            vTaskDelay(pdMS_TO_TICKS(3));
        } else {
            if (self->bCaptureActive) {
                // 停止沿：停止捕获（摘除中断与定时器）
                self->irrecv.disableIRIn();
                self->bCaptureActive = false;
                LOG_I("IRLearnTask: 捕获已停止");
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// 处理一帧新捕获的消息（仅学习任务调用）
// 提取raw时序拼接到edges；save_buffer模式下decode()已自动resume()，
// 此处不可再调用resume()（会清掉正在进行的新捕获）
// 返回true=该帧已存入学习结果
// ──────────────────────────────────────────────────────────────────────────────
bool IRHelper::processCapturedFrame()
{
    bool stored = false;
    unsigned long now = millis();

    do {
        // 重复帧抑制：NEC等协议长按时会连续发送repeat帧，
        // 存储无意义；但活动时间戳照常刷新（信号仍在进行）
        if (results.repeat && meta.frameCount > 0) {
            break;
        }

        // 提取修正后的raw时序数组（us单位mark/space交替，堆分配）
        uint16_t rawLen = getCorrectedRawLength(&results);
        if (rawLen < IR_MIN_FRAME_EDGES) {
            break;   // 过短视为噪声
        }

        // 容量保护：帧槽与边沿总量
        if (meta.frameCount >= IR_MAX_FRAMES) {
            LOG_W("IRHelper: 帧缓冲已满，忽略后续帧");
            break;
        }
        if (meta.totalEdges + rawLen > IR_EDGE_BUFFER_COUNT) {
            LOG_W("IRHelper: 边沿缓冲已满，忽略后续帧");
            break;
        }

        uint16_t* rawArray = resultToRawArray(&results);
        if (rawArray == nullptr) break;

        // 本帧时长(us)：用于从处理时刻差中反推精确帧间隔
        uint32_t durUs = 0;
        for (uint16_t j = 0; j < rawLen; j++) durUs += rawArray[j];

        memcpy(&edges[meta.totalEdges], rawArray, rawLen * sizeof(uint16_t));
        meta.frameLens[meta.frameCount] = rawLen;

        // 精确帧间隔 = 相邻两帧处理时刻差 - 本帧时长
        // （处理时刻 = 帧结束 + 捕获超时 + 轮询延迟；相邻两帧相减后
        //   超时与延迟基本抵消，残余误差仅±几ms，远优于直接用millis差）
        if (meta.frameCount > 0 && lastFrameTime > 0 && now > lastFrameTime) {
            int32_t gap = (int32_t)(now - lastFrameTime) - (int32_t)(durUs / 1000);
            if (gap < 0) gap = 0;
            if (gap > 60000) gap = 60000;
            meta.frameGapsMs[meta.frameCount] = (uint16_t)gap;
        }

        meta.totalEdges += rawLen;
        meta.frameCount++;

        // 协议名（多帧取最后一帧，空调多帧通常同协议）
        String protoName = typeToString(results.decode_type);
        strncpy(meta.protocolName, protoName.c_str(), sizeof(meta.protocolName) - 1);
        meta.protocolName[sizeof(meta.protocolName) - 1] = '\0';

        // ── v2: 保存本帧解码详情（界面展示"具体值"用，发射链路不依赖） ──
        uint8_t fi = meta.frameCount - 1;
        meta.frameBits[fi]  = results.bits;
        meta.frameProto[fi] = (uint8_t)results.decode_type;
        meta.frameValue[fi] = results.value;
        meta.frameAddr[fi]  = results.address;
        meta.frameCmd[fi]   = results.command;
        // 首个空调协议帧的人类可读状态（如 "26C, COOL, FAN3"）；
        // 非空调协议resultAcToString返回空串，此处为空表示展示时序摘要
        if (meta.acStateDesc[0] == '\0') {
            String acDesc = IRAcUtils::resultAcToString(&results);
            if (acDesc.length() > 0) {
                strncpy(meta.acStateDesc, acDesc.c_str(),
                        sizeof(meta.acStateDesc) - 1);
                meta.acStateDesc[sizeof(meta.acStateDesc) - 1] = '\0';
            }
        }

        delete[] rawArray;
        stored = true;

        // UNKNOWN的value是raw时序的FNV哈希指纹（非码值，随捕获抖动变化），
        // 已识别协议的value才是真实码值——日志区分标注避免误读
        if (results.decode_type == decode_type_t::UNKNOWN) {
            LOG_I("IRHelper: 帧%d UNKNOWN 指纹=%08X(非码值) %u边沿",
                  meta.frameCount, (uint32_t)results.value, rawLen);
        } else {
            LOG_I("IRHelper: 帧%d %s 值=%08X %u边沿 %ubits%s",
                  meta.frameCount, protoName.c_str(),
                  (uint32_t)results.value, rawLen,
                  results.bits, results.repeat ? "(repeat)" : "");
        }
    } while (false);

    // 刷新活动时间戳（静默判定基准）；捕获已由decode()内部恢复
    lastFrameTime = now;
    return stored;
}

// ──────────────────────────────────────────────────────────────────────────────
// 轮询：静默超时判定整组完成（帧的接收与处理由学习任务实时进行）
// 逻辑：距最后一帧静默超过 IR_LEARN_SETTLE_MS 仍无新帧 → 整组完成
//       （空调等多帧遥控的帧间隔通常<300ms，普通遥控单帧即完成）
// ──────────────────────────────────────────────────────────────────────────────
bool IRHelper::CheckLearnFinished()
{
    if (learnCmd != 1) return false;

    // 未收到任何帧：无超时（持续等待用户操作）
    if (meta.frameCount == 0 || lastFrameTime == 0) return false;

    // 静默判定：距最后一帧超过结算时间 → 整组完成
    if (millis() - lastFrameTime < IR_LEARN_SETTLE_MS) return false;

    // 停止学习（学习任务将在数毫秒内停止捕获，meta/edges保留有效）
    learnCmd = 0;

    if (meta.totalEdges > 0) {
        LOG_I("IRHelper: 学习完成 %s协议 %d帧 %u边沿",
              meta.protocolName, meta.frameCount, meta.totalEdges);
        SoundAlertBeep(500);  // 学习成功提示音（WM8960滴声）
        return true;
    }

    LOG_W("IRHelper: 学习结果为空");
    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// 发射：逐帧重放（帧间按学习时的时间间隔等待）
// 优先RMT硬件发射（1us精度+硬件载波），RMT不可用时降级软件调制
// ──────────────────────────────────────────────────────────────────────────────
bool IRHelper::SendData(const IRSignalMeta& irMeta, const uint16_t* irEdges)
{
    std::unique_lock<std::mutex> lock(transmitMutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;

    if (!bReady || irMeta.frameCount == 0 || irMeta.frameCount > IR_MAX_FRAMES ||
        irEdges == nullptr || irMeta.totalEdges == 0) {
        LOG_E("IRHelper::SendData: 无效数据");
        return false;
    }

#if IR_TX_USE_RMT
    const char* txMode = rmtReady ? "RMT" : "软件";
#else
    const char* txMode = "软件";
#endif
    LOG_I("[IR TX] %s协议 %d帧 %u边沿 (%s)",
          irMeta.protocolName, irMeta.frameCount, irMeta.totalEdges, txMode);

    uint32_t edgeIdx = 0;
    bool allOk = true;
    for (uint8_t i = 0; i < irMeta.frameCount; i++) {
        uint16_t len = irMeta.frameLens[i];
        if (len == 0 || edgeIdx + len > irMeta.totalEdges) return false;

        // 帧间等待（还原学习时的间隔，保证接收端按序识别）
        if (i > 0 && irMeta.frameGapsMs[i] > 0) {
            delay(irMeta.frameGapsMs[i]);
        }

        bool ok;
#if IR_TX_USE_RMT
        if (rmtReady) {
            ok = sendFrameRMT(&irEdges[edgeIdx], len);
        } else {
            irsend.sendRaw(&irEdges[edgeIdx], len, 38000);
            ok = true;
        }
#else
        // 库软件发射（当前默认路径: 排查RMT发射问题）
        irsend.sendRaw(&irEdges[edgeIdx], len, 38000);
        ok = true;
#endif
        if (!ok) allOk = false;

        edgeIdx += len;
    }

    SoundAlertBeep(100);  // 发送完成提示音（WM8960滴声）
    return allOk;
}

// ──────────────────────────────────────────────────────────────────────────────
// RMT硬件发射一帧
// 边沿数组（us，mark/space交替，偶数下标=mark）→ rmt_data_t符号流：
//   每符号2个相位，每相位最长32767us；超长脉宽自动拆分为连续同电平相位。
//   mark=电平1（RMT硬件在其上叠加38kHz载波），space=电平0。
//   rmtWrite阻塞至硬件发完（超时1s兜底）；数据量可超出通道RAM
//   （驱动自动分批装载，与CC1101驱动同机制）。
// ──────────────────────────────────────────────────────────────────────────────
bool IRHelper::sendFrameRMT(const uint16_t* frameEdges, uint16_t len)
{
    // 符号数上界：单边沿(uint16最大65535us)最坏拆成3个相位(32767+32767+1)，
    // 相位数<=3*len，两相位打包一个符号 → 符号数<=1.5*len，再+1收尾填充
    rmt_data_t* items = (rmt_data_t*)malloc((len * 3 / 2 + 4) * sizeof(rmt_data_t));
    if (items == nullptr) {
        LOG_E("IRHelper::sendFrameRMT: 符号缓冲分配失败");
        return false;
    }

    uint32_t idx = 0;    // 已完成的符号数
    bool slot = false;   // false=下一个相位写duration0, true=写duration1

    for (uint16_t e = 0; e < len; e++) {
        uint32_t remaining = (frameEdges[e] > 0) ? frameEdges[e] : 1;  // 防0时长
        uint8_t level = (e % 2 == 0) ? 1 : 0;   // 偶数下标=mark=高电平(载波)

        // 超长脉宽(>32767us)拆分为多个连续同电平相位
        while (remaining > 0) {
            uint32_t d = (remaining > 32767) ? 32767 : remaining;
            remaining -= d;
            if (!slot) {
                items[idx].duration0 = d;
                items[idx].level0 = level;
                slot = true;
            } else {
                items[idx].duration1 = d;
                items[idx].level1 = level;
                idx++;
                slot = false;
            }
        }
    }

    // 收尾：奇数个相位时补1us低电平填充第二个相位
    if (slot) {
        items[idx].duration1 = 1;
        items[idx].level1 = 0;
        idx++;
    }

    // 硬件发射（阻塞至完成；1s超时兜底防挂死）
    bool ok = rmtWrite(PIN_IR_SEND, items, idx, 1000);
    free(items);

    if (!ok) {
        LOG_E("IRHelper::sendFrameRMT: rmtWrite失败 (%u符号)",
              (unsigned)idx);
    }
    return ok;
}

bool IRHelper::SendFromSlot(int index)
{
    SignalEntry entry = dataStore.ReadSignal(SIG_IR, index);
    if (entry.name.length() == 0) {
        LOG_W("IRHelper::SendFromSlot: 槽位%d无红外信号", index);
        return false;
    }

    // 按需读取边沿数据（堆上临时缓冲，用完即释放）
    uint16_t* buf = (uint16_t*)malloc(entry.ir.totalEdges * sizeof(uint16_t));
    if (buf == nullptr) {
        LOG_E("IRHelper::SendFromSlot: 边沿缓冲分配失败");
        return false;
    }

    bool ok = false;
    // 元数据和边沿从同一次读取取得，避免槽位被同时覆盖时混用两版数据。
    if (dataStore.ReadIREdges(index, buf, entry.ir.totalEdges, &entry.ir)) {
        ok = SendData(entry.ir, buf);
    } else {
        LOG_E("IRHelper::SendFromSlot: 读取边沿数据失败");
    }

    free(buf);
    return ok;
}
