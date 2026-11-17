/**
 * api/home.js —— 多主页管理（8主页 × 1-9键布局 × 键位绑定）
 * slots 值：0=空 / 1..100=RF槽位 / 101..200=IR槽位（统一编码）
 */
import http from './http'

// GET /api/home/list
export const listHomes = () => http.get('/api/home/list')

// POST /api/home/create {name, layout}
export const createHome = (name, layout) => http.post('/api/home/create', { name, layout })

// POST /api/home/save {id, name, layout, slots:[9]}
export const saveHome = (profile) => http.post('/api/home/save', profile)

// POST /api/home/delete {id}
export const deleteHome = (id) => http.post('/api/home/delete', { id })

// POST /api/home/activate {id} —— 切换设备当前激活主页（立即生效）
export const activateHome = (id) => http.post('/api/home/activate', { id })
