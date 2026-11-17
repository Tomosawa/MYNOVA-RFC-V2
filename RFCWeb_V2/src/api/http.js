/**
 * api/http.js
 *
 * 统一 axios 实例：
 * - 超时 8s
 * - 响应拦截直接返回 response.data，简化调用方
 * - 固件统一失败格式 {"result":"failed","message":...} → 全局 snackbar
 * - 轮询类请求可传 { _silent: true } 跳过全局错误提示（由调用方自行处理）
 */
import axios from 'axios'
import { useAppStore } from '@/stores/app'

const http = axios.create({ timeout: 8000 })

function notifyError (msg) {
  try { useAppStore().notify(msg, 'error') } catch (e) { /* store 未就绪时忽略 */ }
}

http.interceptors.response.use(
  (response) => {
    const d = response.data
    const silent = response.config && response.config._silent
    // 固件业务失败：{"result":"failed","message":"..."}
    if (d && typeof d === 'object' && d.result === 'failed') {
      const msg = d.message || 'Operation failed'
      if (!silent) notifyError(msg)
      return Promise.reject(new Error(msg))
    }
    return d
  },
  (error) => {
    const silent = error.config && error.config._silent
    const msg = (error.response && error.response.data && error.response.data.message)
      || error.message
      || 'Network error'
    if (!silent) notifyError(msg)
    return Promise.reject(new Error(msg))
  },
)

export default http
