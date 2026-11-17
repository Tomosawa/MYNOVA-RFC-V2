/**
 * api/backup.js —— TF 卡备份管理
 */
import http from './http'

// GET /api/backup/list
export const listBackups = () => http.get('/api/backup/list')

// POST /api/backup/export —— 全量导出（信号 + 系统配置）
export const exportBackup = () => http.post('/api/backup/export', {}, { timeout: 60000 })

// POST /api/backup/restore {file, signals, system}
export const restoreBackup = (file, signals, system) =>
  http.post('/api/backup/restore', { file, signals, system }, { timeout: 60000 })

// POST /api/backup/delete {file}
export const deleteBackup = (file) => http.post('/api/backup/delete', { file })
