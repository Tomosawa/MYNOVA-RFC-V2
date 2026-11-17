/**
 * api/signals.js —— 信号数据管理（统一槽位：RF=1..100，IR=101..200）
 */
import http from './http'

// GET /api/radiodata/list —— 全部信号
export const listSignals = () => http.get('/api/radiodata/list')

// GET /api/radiodata/get?index=N —— 单条信号完整信息
export const getSignal = (index) => http.get('/api/radiodata/get', { params: { index } })

// POST /api/radiodata/add —— 新增射频信号（红外必须设备/Web学习获得）
export const addSignal = (data) => http.post('/api/radiodata/add', data)

// POST /api/radiodata/update —— RF全量编辑；IR仅改名
export const updateSignal = (data) => http.post('/api/radiodata/update', data)

// POST /api/radiodata/delete
export const deleteSignal = (index) => http.post('/api/radiodata/delete', { index })

// POST /api/radiodata/send —— 按槽位发射
export const sendSignal = (index) => http.post('/api/radiodata/send', { index })
