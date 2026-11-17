/**
 * api/rf.js —— RF 实时接收捕获（Web嗅探工作流）
 */
import http from './http'

// POST /api/rf/rx/start —— {"frequency":433.92} 单频点守候 或 {"band":"low"|"high"} 频段扫描
export const startRfRx = (body) => http.post('/api/rf/rx/start', body)

// GET /api/rf/rx/status —— 轮询接收状态/捕获结果（轮询请传 { _silent: true }）
export const rfRxStatus = (opts) => http.get('/api/rf/rx/status', opts)

// POST /api/rf/rx/stop
export const stopRfRx = () => http.post('/api/rf/rx/stop', {})

// POST /api/rf/rx/save {"name"} —— 捕获草稿保存到首个空RF槽位
export const saveRfCapture = (name) => http.post('/api/rf/rx/save', { name })
