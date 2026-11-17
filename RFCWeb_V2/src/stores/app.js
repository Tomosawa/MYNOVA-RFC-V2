/**
 * stores/app.js —— 全局应用状态：snackbar 通知 / 主题模式 / 语言
 */
import { defineStore } from 'pinia'

function readStored (key, fallback) {
  try { return localStorage.getItem(key) ?? fallback } catch (e) { return fallback }
}

export const useAppStore = defineStore('app', {
  state: () => ({
    snackbar: false,
    snackbarText: '',
    snackbarColor: 'success',
    // 主题三态：light / dark / system（跟随系统）
    themeMode: readStored('themeMode', 'system'),
    // 语言：chs / en（空 = 首次访问，由 App.vue 依据浏览器语言初始化）
    locale: readStored('locale', ''),
  }),
  actions: {
    notify (text, color = 'success') {
      this.snackbarText = text
      this.snackbarColor = color
      this.snackbar = true
    },
    setThemeMode (mode) {
      this.themeMode = mode
      try { localStorage.setItem('themeMode', mode) } catch (e) { /* 忽略 */ }
    },
    cycleTheme () {
      const order = ['system', 'light', 'dark']
      this.setThemeMode(order[(order.indexOf(this.themeMode) + 1) % order.length])
    },
    setLocale (loc) {
      this.locale = loc
      try { localStorage.setItem('locale', loc) } catch (e) { /* 忽略 */ }
    },
  },
})
