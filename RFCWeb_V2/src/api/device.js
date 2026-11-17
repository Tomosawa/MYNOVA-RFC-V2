/**
 * api/device.js —— 设备状态 / 系统设置 / 重启 / 恢复出厂 / OTA / 日志级别
 */
import http from './http'

// GET /api/status —— 仪表盘数据源（轮询场景请传 { _silent: true }）
export const getStatus = (opts) => http.get('/api/status', opts)

// GET /api/settings —— 读取系统设置
export const getSettings = () => http.get('/api/settings')

// POST /api/settings —— 部分字段更新（仅提交变更键）
export const saveSettings = (settings) => http.post('/api/settings', settings)

// POST /api/reboot —— 重启设备（响应后延迟重启）
export const reboot = () => http.post('/api/reboot', {})

// POST /api/factoryreset —— 恢复出厂（固件要求 {"confirm":"YES"}）
export const factoryReset = () => http.post('/api/factoryreset', { confirm: 'YES' })

// GET /api/ota/info —— 当前运行分区与可写空间
export const getOtaInfo = () => http.get('/api/ota/info')

// GET /api/loglevel —— 不带参数查询；?level=0..4 设置（NVS持久化）
export const getLogLevel = (level) =>
  http.get('/api/loglevel', { params: level != null ? { level } : {} })
