/**
 * api/ir.js —— IR 红外学习（Web触发）
 */
import http from './http'

// POST /api/ir/learn/start —— 启动红外学习
export const startIrLearn = () => http.post('/api/ir/learn/start', {})

// GET /api/ir/learn/status —— 轮询学习状态（轮询请传 { _silent: true }）
export const irLearnStatus = (opts) => http.get('/api/ir/learn/status', opts)

// POST /api/ir/learn/cancel —— 取消学习并丢弃草稿
export const cancelIrLearn = () => http.post('/api/ir/learn/cancel', {})

// POST /api/ir/learn/save {"name"} —— 学习结果保存到首个空IR槽位
export const saveIrLearn = (name) => http.post('/api/ir/learn/save', { name })
