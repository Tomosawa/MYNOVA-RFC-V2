#pragma once
// 兼容的内存加载API上限。RAW录制/文件回放走流式路径，不受此数组长度限制。
#define SUB_RAW_MAX_PULSES 16384
