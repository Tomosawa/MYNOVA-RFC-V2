/**
 * stores/device.js —— 设备状态（/api/status 轮询数据 + 在线判定）
 */
import { defineStore } from 'pinia'
import { getStatus } from '@/api/device'

export const useDeviceStore = defineStore('device', {
  state: () => ({
    status: null,   // /api/status 响应
    online: false,
  }),
  getters: {
    batteryColor () {
      const p = this.status ? this.status.batteryPercent : 0
      if (p > 50) return 'success'
      if (p > 20) return 'warning'
      return 'error'
    },
  },
  actions: {
    // 静默刷新：离线不打扰用户（由页面自行展示离线横幅）
    async refresh () {
      try {
        this.status = await getStatus({ _silent: true })
        this.online = true
      } catch (e) {
        this.online = false
      }
    },
  },
})
